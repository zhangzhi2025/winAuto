#include "WorkflowGeneration.h"

#include <QJsonArray>
#include <QNetworkRequest>
#include <QUrl>

#include "ClaudeWorkflowProvider.h"
#include "OpenAIWorkflowProvider.h"
#include "WorkflowCapabilities.h"

namespace wa::llm {
namespace {

constexpr int kMaxTaskCharacters = 10000;
constexpr qsizetype kMaxResponseBytes = 2 * 1024 * 1024;
constexpr int kMaxRateLimitRetries = 2;
constexpr int kRequestTimeoutMs = 120000;

bool validCredential(const QString& credential) {
    if (credential.isEmpty() || credential.size() > 1024) return false;
    for (const QChar character : credential) {
        const ushort value = character.unicode();
        if (value < 0x21 || value > 0x7e) return false;
    }
    return true;
}

GenerationResult failure(GenerationErrorCode code, const QString& message) {
    GenerationResult result;
    result.error = code;
    result.message = message;
    return result;
}

bool readStringArray(const QJsonValue& value, bool required, QStringList* output) {
    if (value.isUndefined()) return !required;
    if (!value.isArray()) return false;
    const QJsonArray array = value.toArray();
    if (array.size() > 32) return false;
    for (const QJsonValue& item : array) {
        if (!item.isString()) return false;
        const QString text = item.toString().trimmed();
        if (text.isEmpty() || text.size() > 2048) return false;
        output->push_back(text);
    }
    return true;
}

}  // namespace

QString providerName(Provider provider) {
    return provider == Provider::Claude ? QStringLiteral("claude") : QStringLiteral("openai");
}

bool parseProvider(const QString& text, Provider* provider) {
    if (!provider) return false;
    if (text == QLatin1String("claude")) {
        *provider = Provider::Claude;
        return true;
    }
    if (text == QLatin1String("openai")) {
        *provider = Provider::OpenAI;
        return true;
    }
    return false;
}

GenerationResult proposalFromToolArguments(const QJsonObject& arguments) {
    const QStringList allowed{QStringLiteral("status"), QStringLiteral("workflow"), QStringLiteral("questions"),
                              QStringLiteral("unsupported")};
    for (auto it = arguments.constBegin(); it != arguments.constEnd(); ++it) {
        if (!allowed.contains(it.key())) {
            return failure(GenerationErrorCode::InvalidResponse, QStringLiteral("模型提案包含未知字段。"));
        }
    }

    const QString status = arguments.value(QStringLiteral("status")).toString();
    WorkflowProposal proposal;
    if (status == QLatin1String("ready")) {
        if (!arguments.value(QStringLiteral("workflow")).isObject() ||
            arguments.value(QStringLiteral("workflow")).toObject().isEmpty()) {
            return failure(GenerationErrorCode::InvalidResponse, QStringLiteral("模型提案缺少工作流对象。"));
        }
        if (!readStringArray(arguments.value(QStringLiteral("questions")), false, &proposal.questions) ||
            !readStringArray(arguments.value(QStringLiteral("unsupported")), false, &proposal.unsupported) ||
            !proposal.questions.isEmpty() || !proposal.unsupported.isEmpty()) {
            return failure(GenerationErrorCode::InvalidResponse, QStringLiteral("ready 提案不能同时包含澄清问题或不支持项。"));
        }
        proposal.status = ProposalStatus::Ready;
        proposal.workflow = arguments.value(QStringLiteral("workflow")).toObject();
    } else if (status == QLatin1String("needs_clarification")) {
        if (!arguments.value(QStringLiteral("workflow")).isUndefined() ||
            !readStringArray(arguments.value(QStringLiteral("questions")), true, &proposal.questions) ||
            proposal.questions.isEmpty() ||
            !readStringArray(arguments.value(QStringLiteral("unsupported")), false, &proposal.unsupported) ||
            !proposal.unsupported.isEmpty()) {
            return failure(GenerationErrorCode::InvalidResponse, QStringLiteral("澄清提案必须包含问题且不能包含工作流。"));
        }
        proposal.status = ProposalStatus::NeedsClarification;
    } else if (status == QLatin1String("unsupported")) {
        if (!arguments.value(QStringLiteral("workflow")).isUndefined() ||
            !readStringArray(arguments.value(QStringLiteral("unsupported")), true, &proposal.unsupported) ||
            proposal.unsupported.isEmpty() ||
            !readStringArray(arguments.value(QStringLiteral("questions")), false, &proposal.questions) ||
            !proposal.questions.isEmpty()) {
            return failure(GenerationErrorCode::InvalidResponse,
                           QStringLiteral("unsupported 提案必须列出不支持能力且不能包含工作流。"));
        }
        proposal.status = ProposalStatus::Unsupported;
    } else {
        return failure(GenerationErrorCode::InvalidResponse, QStringLiteral("模型提案状态无效。"));
    }

    GenerationResult result;
    result.proposal = std::move(proposal);
    return result;
}

WorkflowGeneration::WorkflowGeneration(QObject* parent) : QObject(parent) {
    timeout_.setSingleShot(true);
    retryTimer_.setSingleShot(true);
    connect(&timeout_, &QTimer::timeout, this, [this] {
        if (!active_) return;
        if (reply_) reply_->abort();
        finish(failure(GenerationErrorCode::Timeout, QStringLiteral("模型请求超时。")));
    });
    connect(&retryTimer_, &QTimer::timeout, this, [this] {
        if (active_) sendAttempt();
    });
}

bool WorkflowGeneration::active() const { return active_; }

void WorkflowGeneration::start(GenerationRequest request, Completion completion) {
    if (active_) {
        if (completion) completion(failure(GenerationErrorCode::ProviderError, QStringLiteral("已有生成请求正在运行。")));
        return;
    }
    if (request.taskDescription.trimmed().isEmpty() || request.taskDescription.size() > kMaxTaskCharacters) {
        if (completion) completion(failure(GenerationErrorCode::InvalidResponse,
                                           QStringLiteral("任务描述必须为 1 至 %1 个字符。").arg(kMaxTaskCharacters)));
        return;
    }
    if (request.connection.modelName.trimmed().isEmpty() || !validCredential(request.connection.apiKey)) {
        if (completion) completion(failure(GenerationErrorCode::ConfigurationMissing, QStringLiteral("请先配置模型和访问凭据。")));
        return;
    }

    request_ = std::move(request);
    completion_ = std::move(completion);
    retryCount_ = 0;
    response_.clear();
    responseTooLarge_ = false;
    active_ = true;
    timeout_.start(kRequestTimeoutMs);
    sendAttempt();
}

void WorkflowGeneration::sendAttempt() {
    if (!active_) return;
    response_.clear();
    responseTooLarge_ = false;
    QByteArray body;
    QNetworkRequest networkRequest =
        request_.connection.provider == Provider::Claude
            ? ClaudeWorkflowProvider::createRequest(request_, &body)
            : OpenAIWorkflowProvider::createRequest(request_, &body);
    networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    reply_ = manager_.post(networkRequest, body);
    connect(reply_, &QIODevice::readyRead, this, [this] {
        if (!active_ || !reply_) return;
        const QByteArray chunk = reply_->readAll();
        if (chunk.size() > kMaxResponseBytes - response_.size()) {
            responseTooLarge_ = true;
            reply_->abort();
            return;
        }
        response_.append(chunk);
    });
    connect(reply_, &QNetworkReply::finished, this, [this, current = reply_.data()] { onReplyFinished(current); });
}

void WorkflowGeneration::onReplyFinished(QNetworkReply* reply) {
    if (!reply) return;
    if (!active_ || reply_ != reply) {
        reply->deleteLater();
        return;
    }
    const QByteArray remaining = reply->readAll();
    if (remaining.size() > kMaxResponseBytes - response_.size()) responseTooLarge_ = true;
    else response_.append(remaining);

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto networkError = reply->error();
    int retryDelay = 0;
    if (httpStatus == 429 && retryCount_ < kMaxRateLimitRetries) {
        bool ok = false;
        const qlonglong serverDelay = QString::fromLatin1(reply->rawHeader("Retry-After")).trimmed().toLongLong(&ok);
        retryDelay = ok && serverDelay > 0 ? (serverDelay >= 5 ? 5000 : static_cast<int>(serverDelay) * 1000)
                                           : (retryCount_ + 1) * 1000;
    }
    reply->deleteLater();
    reply_ = nullptr;

    if (responseTooLarge_) {
        finish(failure(GenerationErrorCode::InvalidResponse, QStringLiteral("模型响应超过 2 MiB 限制。")));
        return;
    }
    if (retryDelay > 0) {
        ++retryCount_;
        retryTimer_.start(retryDelay);
        return;
    }
    if (httpStatus == 401 || httpStatus == 403) {
        finish(failure(GenerationErrorCode::AuthenticationFailed, QStringLiteral("模型服务拒绝了访问凭据。")));
        return;
    }
    if (httpStatus == 429) {
        finish(failure(GenerationErrorCode::RateLimited, QStringLiteral("模型服务限流，请稍后重试。")));
        return;
    }
    if (httpStatus < 200 || httpStatus >= 300) {
        if (httpStatus == 0 || networkError != QNetworkReply::NoError) {
            finish(failure(GenerationErrorCode::NetworkError, QStringLiteral("无法连接模型服务。")));
        } else {
            finish(failure(GenerationErrorCode::ProviderError, QStringLiteral("模型服务返回错误（HTTP %1）。").arg(httpStatus)));
        }
        return;
    }
    if (networkError != QNetworkReply::NoError) {
        finish(failure(GenerationErrorCode::NetworkError, QStringLiteral("模型服务连接中断。")));
        return;
    }

    GenerationResult result = request_.connection.provider == Provider::Claude
                                  ? ClaudeWorkflowProvider::parseResponse(response_)
                                  : OpenAIWorkflowProvider::parseResponse(response_);
    finish(std::move(result));
}

void WorkflowGeneration::cancel() {
    if (!active_) return;
    if (reply_) reply_->abort();
    finish(failure(GenerationErrorCode::Cancelled, QStringLiteral("已取消生成。")));
}

void WorkflowGeneration::finish(GenerationResult result) {
    if (!active_) return;
    active_ = false;
    timeout_.stop();
    retryTimer_.stop();
    if (reply_) {
        reply_->abort();
        reply_->deleteLater();
        reply_ = nullptr;
    }
    clearSensitiveData();
    Completion completion = std::move(completion_);
    if (completion) completion(std::move(result));
}

void WorkflowGeneration::clearSensitiveData() {
    request_.connection.apiKey.fill(QChar::Null);
    request_.connection.apiKey.clear();
    request_.taskDescription.clear();
    response_.fill('\0');
    response_.clear();
}

}  // namespace wa::llm
