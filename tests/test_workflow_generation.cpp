#include "testing.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "ClaudeWorkflowProvider.h"
#include "OpenAIWorkflowProvider.h"
#include "WorkflowGeneration.h"
#include "WorkflowProposalParser.h"

using namespace wa::llm;

namespace {

GenerationRequest sampleRequest(Provider provider) {
    GenerationRequest request;
    request.taskDescription = QStringLiteral("打开记事本并等待");
    request.connection.provider = provider;
    request.connection.modelName = QStringLiteral("test-model");
    request.connection.apiKey = QStringLiteral("test-key");
    return request;
}

QJsonObject validWorkflow() {
    return QJsonObject{{QStringLiteral("schemaVersion"), QStringLiteral("1.0")},
                       {QStringLiteral("id"), QStringLiteral("WF-GENERATED")},
                       {QStringLiteral("name"), QStringLiteral("Generated workflow")},
                       {QStringLiteral("windows"), QJsonObject{}},
                       {QStringLiteral("steps"), QJsonArray{}}};
}

}  // namespace

TEST(providerRequestsKeepSecretsOutOfBodiesAndUseHttps) {
    QByteArray claudeBody;
    const QNetworkRequest claudeRequest = ClaudeWorkflowProvider::createRequest(
        sampleRequest(Provider::Claude), &claudeBody);
    CHECK(claudeRequest.url().scheme() == QStringLiteral("https"));
    CHECK(claudeRequest.rawHeader("x-api-key") == QByteArray("test-key"));
    CHECK(!claudeBody.contains("test-key"));
    CHECK(claudeBody.contains("propose_workflow"));

    QByteArray openAiBody;
    const QNetworkRequest openAiRequest = OpenAIWorkflowProvider::createRequest(
        sampleRequest(Provider::OpenAI), &openAiBody);
    CHECK(openAiRequest.url().scheme() == QStringLiteral("https"));
    CHECK(openAiRequest.rawHeader("Authorization") == QByteArray("Bearer test-key"));
    CHECK(!openAiBody.contains("test-key"));
    CHECK(openAiBody.contains("propose_workflow"));
}

TEST(claudeParsesOnlyTheExpectedToolProposal) {
    const QJsonObject arguments{{QStringLiteral("status"), QStringLiteral("ready")},
                                {QStringLiteral("workflow"), validWorkflow()}};
    const QJsonObject response{{QStringLiteral("stop_reason"), QStringLiteral("tool_use")},
                               {QStringLiteral("content"),
                                QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("tool_use")},
                                                       {QStringLiteral("name"), QStringLiteral("propose_workflow")},
                                                       {QStringLiteral("input"), arguments}}}}};
    const GenerationResult parsed =
        ClaudeWorkflowProvider::parseResponse(QJsonDocument(response).toJson(QJsonDocument::Compact));
    CHECK(parsed.ok());
    CHECK_EQ(parsed.proposal.status, ProposalStatus::Ready);
    CHECK(parsed.proposal.workflow.value(QStringLiteral("name")).toString() == QStringLiteral("Generated workflow"));

    const GenerationResult plainText = ClaudeWorkflowProvider::parseResponse(
        R"({"stop_reason":"end_turn","content":[{"type":"text","text":"workflow"}]})");
    CHECK_EQ(plainText.error, GenerationErrorCode::InvalidResponse);
}

TEST(openAiParsesOnlyTheExpectedFunctionCall) {
    const QJsonObject arguments{{QStringLiteral("status"), QStringLiteral("needs_clarification")},
                                {QStringLiteral("questions"), QJsonArray{QStringLiteral("目标窗口名称是什么？")}}};
    const QJsonObject response{
        {QStringLiteral("status"), QStringLiteral("completed")},
        {QStringLiteral("output"),
         QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("function_call")},
                                {QStringLiteral("name"), QStringLiteral("propose_workflow")},
                                {QStringLiteral("arguments"),
                                 QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact))}}}}};
    const GenerationResult parsed =
        OpenAIWorkflowProvider::parseResponse(QJsonDocument(response).toJson(QJsonDocument::Compact));
    CHECK(parsed.ok());
    CHECK_EQ(parsed.proposal.status, ProposalStatus::NeedsClarification);
    CHECK_EQ(parsed.proposal.questions.size(), 1);
}

TEST(proposalRejectsUnknownAndContradictoryFields) {
    const GenerationResult unknown = proposalFromToolArguments(
        QJsonObject{{QStringLiteral("status"), QStringLiteral("ready")},
                    {QStringLiteral("workflow"), validWorkflow()},
                    {QStringLiteral("script"), QStringLiteral("arbitrary code")}});
    CHECK_EQ(unknown.error, GenerationErrorCode::InvalidResponse);

    const GenerationResult conflict = proposalFromToolArguments(
        QJsonObject{{QStringLiteral("status"), QStringLiteral("ready")},
                    {QStringLiteral("workflow"), validWorkflow()},
                    {QStringLiteral("unsupported"), QJsonArray{QStringLiteral("Excel")}}});
    CHECK_EQ(conflict.error, GenerationErrorCode::InvalidResponse);
}

TEST(workflowProposalUsesCoreParsingAndSemanticValidation) {
    const QJsonObject workflow{{QStringLiteral("schemaVersion"), QStringLiteral("1.0")},
                               {QStringLiteral("id"), QStringLiteral("WF-GENERATED")},
                               {QStringLiteral("name"), QStringLiteral("Generated workflow")},
                               {QStringLiteral("windows"),
                                QJsonObject{{QStringLiteral("main"),
                                             QJsonObject{{QStringLiteral("processName"),
                                                          QStringLiteral("notepad.exe")}}}}},
                               {QStringLiteral("steps"),
                                QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("S1")},
                                                       {QStringLiteral("type"), QStringLiteral("activate_window")},
                                                       {QStringLiteral("window"), QStringLiteral("main")}}}}};
    const WorkflowProposalParseResult valid = parseWorkflowProposal(
        QJsonObject{{QStringLiteral("status"), QStringLiteral("ready")}, {QStringLiteral("workflow"), workflow}});
    CHECK(valid.valid);
    CHECK(valid.validation.ok());
    CHECK_EQ(valid.workflow.steps.size(), static_cast<size_t>(1));

    QJsonObject invalidWorkflow = workflow;
    invalidWorkflow.insert(QStringLiteral("steps"),
                           QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("S1")},
                                                  {QStringLiteral("type"), QStringLiteral("unknown_action")},
                                                  {QStringLiteral("window"), QStringLiteral("main")}}});
    const auto invalid = parseWorkflowProposal(
        QJsonObject{{QStringLiteral("status"), QStringLiteral("ready")},
                    {QStringLiteral("workflow"), invalidWorkflow}});
    CHECK(!invalid.valid);
    CHECK(!invalid.validation.ok());
    CHECK(!invalid.validation.issues.empty());
    CHECK_CONTAINS(invalid.validation.issues.front().path, "steps[0]");

    QJsonObject unknownFieldWorkflow = workflow;
    unknownFieldWorkflow.insert(QStringLiteral("arbitraryCode"), QStringLiteral("not allowed"));
    const auto unknownField = parseWorkflowProposal(
        QJsonObject{{QStringLiteral("status"), QStringLiteral("ready")},
                    {QStringLiteral("workflow"), unknownFieldWorkflow}});
    CHECK(!unknownField.valid);
    CHECK(!unknownField.validation.ok());

    QJsonObject scriptWorkflow = workflow;
    scriptWorkflow.insert(QStringLiteral("steps"),
                          QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("S1")},
                                                 {QStringLiteral("type"), QStringLiteral("script")},
                                                 {QStringLiteral("window"), QStringLiteral("main")},
                                                 {QStringLiteral("language"), QStringLiteral("python")},
                                                 {QStringLiteral("code"), QStringLiteral("print('no')")}}});
    const auto generatedScript = parseWorkflowProposal(
        QJsonObject{{QStringLiteral("status"), QStringLiteral("ready")}, {QStringLiteral("workflow"), scriptWorkflow}});
    CHECK(!generatedScript.valid);
    CHECK_CONTAINS(generatedScript.validation.issues.front().path, "steps[0].type");

    QJsonObject interpreterWorkflow = workflow;
    interpreterWorkflow.insert(QStringLiteral("steps"),
                               QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("S1")},
                                                      {QStringLiteral("type"), QStringLiteral("launch_program")},
                                                      {QStringLiteral("programPath"), QStringLiteral("C:/Windows/System32/cmd.exe")},
                                                      {QStringLiteral("arguments"), QStringLiteral("/c whoami")}}});
    const auto generatedInterpreter = parseWorkflowProposal(
        QJsonObject{{QStringLiteral("status"), QStringLiteral("ready")}, {QStringLiteral("workflow"), interpreterWorkflow}});
    CHECK(!generatedInterpreter.valid);
    CHECK_CONTAINS(generatedInterpreter.validation.issues.front().path, "steps[0].programPath");
}
