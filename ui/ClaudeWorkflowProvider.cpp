#include "ClaudeWorkflowProvider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUrl>

#include "WorkflowCapabilities.h"

namespace wa::llm {
namespace {

QJsonObject proposalParameters() {
    QJsonObject properties;
    properties.insert(QStringLiteral("status"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                  {QStringLiteral("enum"), QJsonArray{QStringLiteral("ready"),
                                                                       QStringLiteral("needs_clarification"),
                                                                       QStringLiteral("unsupported")}}});
    properties.insert(QStringLiteral("workflow"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}});
    properties.insert(QStringLiteral("questions"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                  {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}});
    properties.insert(QStringLiteral("unsupported"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                  {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}});
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                       {QStringLiteral("properties"), properties},
                       {QStringLiteral("required"), QJsonArray{QStringLiteral("status")}}};
}

GenerationResult invalidResponse(QString message) {
    GenerationResult result;
    result.error = GenerationErrorCode::InvalidResponse;
    result.message = std::move(message);
    return result;
}

}  // namespace

QNetworkRequest ClaudeWorkflowProvider::createRequest(const GenerationRequest& request, QByteArray* body) {
    QNetworkRequest networkRequest(QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")));
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    networkRequest.setRawHeader("x-api-key", request.connection.apiKey.toUtf8());
    networkRequest.setRawHeader("anthropic-version", "2023-06-01");

    QJsonObject tool{{QStringLiteral("name"), QStringLiteral("propose_workflow")},
                     {QStringLiteral("description"),
                      QStringLiteral("只返回 WinAuto 流程提案数据；不得执行桌面操作或生成脚本。")},
                     {QStringLiteral("input_schema"), proposalParameters()}};
    QJsonObject message{{QStringLiteral("role"), QStringLiteral("user")},
                        {QStringLiteral("content"),
                         QStringLiteral("用户任务：\n%1\n\n当前能力：\n%2\n\n仅当任务可由这些步骤完整表达时返回 ready。"
                                        "缺少必要信息时返回 needs_clarification；任务含不支持能力时返回 unsupported。")
                             .arg(request.taskDescription, capabilitySummary())}};
    QJsonObject payload{{QStringLiteral("model"), request.connection.modelName},
                        {QStringLiteral("max_tokens"), 4096},
                        {QStringLiteral("system"),
                         QStringLiteral("你是 WinAuto 流程规划器。只能提出符合 schemaVersion 1.0 的流程数据。"
                                        "绝不操作应用、执行工具或输出任意代码。")},
                        {QStringLiteral("messages"), QJsonArray{message}},
                        {QStringLiteral("tools"), QJsonArray{tool}},
                        {QStringLiteral("tool_choice"),
                         QJsonObject{{QStringLiteral("type"), QStringLiteral("tool")},
                                     {QStringLiteral("name"), QStringLiteral("propose_workflow")}}}};
    *body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    return networkRequest;
}

GenerationResult ClaudeWorkflowProvider::parseResponse(const QByteArray& response) {
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(response, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return invalidResponse(QStringLiteral("Claude 返回了无法解析的响应。"));
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("stop_reason")).toString() != QLatin1String("tool_use")) {
        return invalidResponse(QStringLiteral("Claude 未通过 propose_workflow 工具返回提案。"));
    }
    const QJsonArray content = root.value(QStringLiteral("content")).toArray();
    QJsonObject arguments;
    int calls = 0;
    for (const QJsonValue& value : content) {
        if (!value.isObject()) continue;
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("type")).toString() != QLatin1String("tool_use")) continue;
        ++calls;
        if (item.value(QStringLiteral("name")).toString() != QLatin1String("propose_workflow") ||
            !item.value(QStringLiteral("input")).isObject()) {
            return invalidResponse(QStringLiteral("Claude 返回了不支持的工具调用。"));
        }
        arguments = item.value(QStringLiteral("input")).toObject();
    }
    if (calls != 1) return invalidResponse(QStringLiteral("Claude 必须恰好返回一次 propose_workflow 调用。"));
    return proposalFromToolArguments(arguments);
}

}  // namespace wa::llm
