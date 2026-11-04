#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>

#include <QObject>

namespace wa::llm {

enum class Provider { Claude, OpenAI };
enum class ProposalStatus { Ready, NeedsClarification, Unsupported };
enum class GenerationErrorCode {
    None,
    AuthenticationFailed,
    RateLimited,
    NetworkError,
    Timeout,
    Cancelled,
    ProviderError,
    InvalidResponse,
    InvalidWorkflow,
    ConfigurationMissing
};

struct ModelConnection {
    Provider provider = Provider::Claude;
    QString modelName;
    QString apiKey;
};

struct GenerationRequest {
    QString taskDescription;
    ModelConnection connection;
};

struct WorkflowProposal {
    ProposalStatus status = ProposalStatus::Ready;
    QJsonObject workflow;
    QStringList questions;
    QStringList unsupported;
};

struct GenerationResult {
    WorkflowProposal proposal;
    GenerationErrorCode error = GenerationErrorCode::None;
    QString message;

    bool ok() const { return error == GenerationErrorCode::None; }
};

class WorkflowGeneration : public QObject {
    Q_OBJECT
public:
    using Completion = std::function<void(GenerationResult)>;

    explicit WorkflowGeneration(QObject* parent = nullptr);
    bool active() const;
    void start(GenerationRequest request, Completion completion);
    void cancel();

private:
    void sendAttempt();
    void onReplyFinished(QNetworkReply* reply);
    void finish(GenerationResult result);
    void clearSensitiveData();

    QNetworkAccessManager manager_;
    QPointer<QNetworkReply> reply_;
    QTimer timeout_;
    QTimer retryTimer_;
    GenerationRequest request_;
    Completion completion_;
    QByteArray response_;
    int retryCount_ = 0;
    bool active_ = false;
    bool responseTooLarge_ = false;
};

QString providerName(Provider provider);
bool parseProvider(const QString& text, Provider* provider);
GenerationResult proposalFromToolArguments(const QJsonObject& arguments);

}  // namespace wa::llm
