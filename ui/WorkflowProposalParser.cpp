#include "WorkflowProposalParser.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QStringList>

namespace wa::llm {
namespace {

bool findScriptStep(const std::vector<Step>& steps, const std::string& path, std::string* scriptPath) {
    for (size_t i = 0; i < steps.size(); ++i) {
        const Step& step = steps[i];
        const std::string stepPath = path + "[" + std::to_string(i) + "]";
        if (step.type == StepType::Script) {
            *scriptPath = stepPath + ".type";
            return true;
        }
        if (step.type == StepType::LaunchProgram) {
            const QString executable = QFileInfo(QString::fromUtf8(step.programPath.c_str())).fileName().toLower();
            static const QStringList interpreters{
                QStringLiteral("cmd.exe"),        QStringLiteral("powershell.exe"), QStringLiteral("pwsh.exe"),
                QStringLiteral("wscript.exe"),    QStringLiteral("cscript.exe"),    QStringLiteral("mshta.exe"),
                QStringLiteral("rundll32.exe"),   QStringLiteral("regsvr32.exe"),   QStringLiteral("python.exe"),
                QStringLiteral("pythonw.exe"),    QStringLiteral("lua.exe"),        QStringLiteral("luajit.exe"),
                QStringLiteral("node.exe"),       QStringLiteral("ruby.exe"),       QStringLiteral("perl.exe"),
                QStringLiteral("php.exe"),        QStringLiteral("java.exe"),       QStringLiteral("javaw.exe"),
                QStringLiteral("dotnet.exe"),     QStringLiteral("bash.exe"),       QStringLiteral("sh.exe"),
                QStringLiteral("wsl.exe")};
            const bool versionedInterpreter =
                (executable.startsWith(QStringLiteral("python")) && executable.endsWith(QStringLiteral(".exe"))) ||
                (executable.startsWith(QStringLiteral("lua")) && executable.endsWith(QStringLiteral(".exe")));
            if (interpreters.contains(executable) || versionedInterpreter) {
                *scriptPath = stepPath + ".programPath";
                return true;
            }
        }
        const std::string bodyKey = step.type == StepType::If ? "then" : "body";
        if (findScriptStep(step.body, stepPath + "." + bodyKey, scriptPath)) return true;
        if (findScriptStep(step.elseBody, stepPath + ".else", scriptPath)) return true;
    }
    return false;
}

}  // namespace

WorkflowProposalParseResult parseWorkflowProposal(const QJsonObject& proposal) {
    WorkflowProposalParseResult result;
    if (proposal.value(QStringLiteral("status")).toString() != QLatin1String("ready") ||
        !proposal.value(QStringLiteral("workflow")).isObject()) {
        result.error = QStringLiteral("只有 ready 提案且包含 workflow 对象时才能校验。");
        return result;
    }

    const QByteArray bytes =
        QJsonDocument(proposal.value(QStringLiteral("workflow")).toObject()).toJson(QJsonDocument::Compact);
    auto parsed = readWorkflow(std::string(bytes.constData(), static_cast<size_t>(bytes.size())));
    if (!parsed.ok()) {
        result.error = QStringLiteral("工作流 JSON 无法解析：%1").arg(QString::fromUtf8(parsed.detail.c_str()));
        return result;
    }

    result.workflow = std::move(parsed.value);
    std::string scriptPath;
    if (findScriptStep(result.workflow.steps, "steps", &scriptPath)) {
        result.error = QStringLiteral("模型生成的流程不得包含脚本步骤或通用命令解释器启动步骤。");
        result.validation.issues.push_back(
            Issue{Severity::Error, scriptPath, "model_code_execution_not_allowed",
                  "model-generated code execution steps are not allowed", false});
        return result;
    }
    result.validation = validateWorkflow(result.workflow);
    if (!result.validation.ok()) {
        result.error = QStringLiteral("工作流有 %1 个校验错误。").arg(result.validation.errorCount());
        return result;
    }
    result.valid = true;
    return result;
}

}  // namespace wa::llm
