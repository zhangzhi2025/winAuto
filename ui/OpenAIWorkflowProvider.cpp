#include "OpenAIWorkflowProvider.h"

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

QNetworkRequest OpenAIWorkflowProvider::createRequest(const GenerationRequest& request, QByteArray* body) {
    QNetworkRequest networkRequest(QUrl(QStringLiteral("https://api.openai.com/v1/responses")));
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    networkRequest.setRawHeader("Authorization", "Bearer " + request.connection.apiKey.toUtf8());

    QJsonObject tool{{QStringLiteral("type"), QStringLiteral("function")},
                     {QStringLiteral("name"), QStringLiteral("propose_workflow")},
                     {QStringLiteral("description"),
                      QStringLiteral("只返回 WinAuto 流程提案数据；不得执行桌面操作或生成脚本。")},
                     {QStringLiteral("parameters"), proposalParameters()}};
    QJsonObject payload{{QStringLiteral("model"), request.connection.modelName},
                        {QStringLiteral("instructions"),
                         QStringLiteral("你是 WinAuto 流程规划器。只能提出符合 schemaVersion 1.0 的流程数据。"
                                        "绝不操作应用、执行工具或输出任意代码。\n当前能力：%1")
                             .arg(capabilitySummary())},
                        {QStringLiteral("input"), request.taskDescription},
                        {QStringLiteral("tools"), QJsonArray{tool}},
                        {QStringLiteral("tool_choice"),
                         QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
                                     {QStringLiteral("name"), QStringLiteral("propose_workflow")}}},
                        {QStringLiteral("max_output_tokens"), 4096}};
    *body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    return networkRequest;
}

GenerationResult OpenAIWorkflowProvider::parseResponse(const QByteArray& response) {
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(response, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return invalidResponse(QStringLiteral("OpenAI 返回了无法解析的响应。"));
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("status")).toString() != QLatin1String("completed")) {
        return invalidResponse(QStringLiteral("OpenAI 响应未完整完成。"));
    }
    const QJsonArray output = root.value(QStringLiteral("output")).toArray();
    QJsonObject arguments;
    int calls = 0;
    for (const QJsonValue& value : output) {
        if (!value.isObject()) continue;
        const QJsonObject item = value.toObject();
        const QString type = item.value(QStringLiteral("type")).toString();
        if (type != QLatin1String("function_call")) continue;
        ++calls;
        if (item.value(QStringLiteral("name")).toString() != QLatin1String("propose_workflow")) {
            return invalidResponse(QStringLiteral("OpenAI 返回了不支持的工具调用。"));
        }
        QJsonParseError argumentsError{};
        const QJsonDocument argumentsDocument =
            QJsonDocument::fromJson(item.value(QStringLiteral("arguments")).toString().toUtf8(), &argumentsError);
        if (argumentsError.error != QJsonParseError::NoError || !argumentsDocument.isObject()) {
            return invalidResponse(QStringLiteral("OpenAI 的工具参数不是有效对象。"));
        }
        arguments = argumentsDocument.object();
    }
    if (calls != 1) return invalidResponse(QStringLiteral("OpenAI 必须恰好返回一次 propose_workflow 调用。"));
    return proposalFromToolArguments(arguments);
}

}  // namespace wa::llm
