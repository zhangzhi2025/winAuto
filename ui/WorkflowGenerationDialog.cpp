#include "WorkflowGenerationDialog.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QJsonDocument>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "ModelConnectionSettings.h"
#include "WorkflowCapabilities.h"

WorkflowGenerationDialog::WorkflowGenerationDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("生成流程草稿"));
    resize(760, 680);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    provider_ = new QComboBox(this);
    provider_->addItem(QStringLiteral("Claude"), QStringLiteral("claude"));
    provider_->addItem(QStringLiteral("OpenAI"), QStringLiteral("openai"));
    model_ = new QLineEdit(this);
    apiKey_ = new QLineEdit(this);
    apiKey_->setEchoMode(QLineEdit::Password);
    apiKey_->setPlaceholderText(QStringLiteral("留空则使用已保存的 Windows 用户凭据"));
    form->addRow(QStringLiteral("模型服务"), provider_);
    form->addRow(QStringLiteral("模型名称"), model_);
    form->addRow(QStringLiteral("API 凭据"), apiKey_);
    layout->addLayout(form);

    auto* capabilities = new QLabel(wa::llm::capabilitySummary(), this);
    capabilities->setWordWrap(true);
    layout->addWidget(capabilities);

    task_ = new QPlainTextEdit(this);
    task_->setPlaceholderText(QStringLiteral("描述需要创建的桌面自动化流程…"));
    task_->setMaximumBlockCount(200);
    layout->addWidget(new QLabel(QStringLiteral("任务描述"), this));
    layout->addWidget(task_, 1);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    output_ = new QPlainTextEdit(this);
    output_->setPlaceholderText(QStringLiteral("模型工作流会显示在此处，可编辑后校验；只有通过校验才能接受。"));
    output_->setPlaceholderText(QStringLiteral("模型提案将在此显示；提案尚未经过流程校验，不会载入或执行。"));
    layout->addWidget(output_, 2);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch();
    generateButton_ = new QPushButton(QStringLiteral("生成提案"), this);
    validateButton_ = new QPushButton(QStringLiteral("校验草稿"), this);
    acceptButton_ = new QPushButton(QStringLiteral("接受并载入编辑器"), this);
    cancelButton_ = new QPushButton(QStringLiteral("关闭"), this);
    buttons->addWidget(generateButton_);
    buttons->addWidget(validateButton_);
    buttons->addWidget(acceptButton_);
    buttons->addWidget(cancelButton_);
    layout->addLayout(buttons);

    generation_ = new wa::llm::WorkflowGeneration(this);
    connect(provider_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { loadProviderSettings(); });
    connect(generateButton_, &QPushButton::clicked, this, &WorkflowGenerationDialog::generate);
    connect(validateButton_, &QPushButton::clicked, this, &WorkflowGenerationDialog::validateDraft);
    connect(acceptButton_, &QPushButton::clicked, this, &WorkflowGenerationDialog::acceptDraft);
    connect(cancelButton_, &QPushButton::clicked, this, &WorkflowGenerationDialog::cancelOrClose);
    connect(output_, &QPlainTextEdit::textChanged, this, [this] {
        acceptedWorkflow_.reset();
        acceptButton_->setEnabled(false);
    });
    validateButton_->setEnabled(false);
    acceptButton_->setEnabled(false);
    loadProviderSettings();
}

WorkflowGenerationDialog::~WorkflowGenerationDialog() {
    if (generation_ && generation_->active()) generation_->cancel();
    apiKey_->clear();
}

void WorkflowGenerationDialog::loadProviderSettings() {
    wa::llm::Provider provider;
    if (!wa::llm::parseProvider(provider_->currentData().toString(), &provider)) return;
    model_->setText(wa::llm::ModelConnectionSettings::load(provider).modelName);
}

void WorkflowGenerationDialog::generate() {
    wa::llm::Provider provider;
    if (!wa::llm::parseProvider(provider_->currentData().toString(), &provider)) {
        QMessageBox::warning(this, QStringLiteral("模型配置无效"), QStringLiteral("请选择受支持的模型服务。"));
        return;
    }
    const QString modelName = model_->text().trimmed();
    if (modelName.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("需要模型配置"), QStringLiteral("请输入模型名称。"));
        model_->setFocus();
        return;
    }
    QString error;
    if (!wa::llm::ModelConnectionSettings::save(provider, modelName, &error)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), error);
        return;
    }

    QString secret = apiKey_->text();
    if (!secret.isEmpty()) {
        if (!wa::llm::ModelConnectionSettings::saveCredential(provider, secret, &error)) {
            secret.fill(QChar::Null);
            QMessageBox::warning(this, QStringLiteral("凭据保存失败"), error);
            return;
        }
        apiKey_->clear();
    } else {
        secret = wa::llm::ModelConnectionSettings::takeCredential(provider, &error);
        if (secret.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("需要模型配置"),
                                     QStringLiteral("请提供 API 凭据后再生成。"));
            apiKey_->setFocus();
            return;
        }
    }
    const QString description = task_->toPlainText().trimmed();
    if (description.isEmpty() || description.size() > 10000) {
        secret.fill(QChar::Null);
        QMessageBox::information(this, QStringLiteral("任务描述无效"),
                                 QStringLiteral("任务描述必须为 1 至 10000 个字符。"));
        task_->setFocus();
        return;
    }

    wa::llm::GenerationRequest request;
    request.taskDescription = description;
    request.connection.provider = provider;
    request.connection.modelName = modelName;
    request.connection.apiKey = secret;
    secret.fill(QChar::Null);
    apiKey_->clear();
    output_->clear();
    currentProposal_ = {};
    validateButton_->setEnabled(false);
    acceptButton_->setEnabled(false);
    status_->setText(QStringLiteral("正在请求模型；现有流程不会被修改。"));
    generateButton_->setEnabled(false);
    provider_->setEnabled(false);
    model_->setEnabled(false);
    cancelButton_->setText(QStringLiteral("取消请求"));
    generation_->start(std::move(request), [this](wa::llm::GenerationResult result) {
        provider_->setEnabled(true);
        model_->setEnabled(true);
        generateButton_->setEnabled(true);
        cancelButton_->setText(QStringLiteral("关闭"));
        showResult(result);
    });
}

void WorkflowGenerationDialog::cancelOrClose() {
    if (generation_->active()) {
        generation_->cancel();
        return;
    }
    reject();
}

void WorkflowGenerationDialog::showResult(const wa::llm::GenerationResult& result) {
    if (!result.ok()) {
        status_->setText(result.message);
        return;
    }
    switch (result.proposal.status) {
        case wa::llm::ProposalStatus::Ready:
            currentProposal_ = QJsonObject{{QStringLiteral("status"), QStringLiteral("ready")},
                                            {QStringLiteral("workflow"), result.proposal.workflow}};
            {
                const QSignalBlocker blocker(output_);
                output_->setPlainText(QString::fromUtf8(
                    QJsonDocument(result.proposal.workflow).toJson(QJsonDocument::Indented)));
            }
            validateButton_->setEnabled(true);
            validateDraft();
            break;
        case wa::llm::ProposalStatus::NeedsClarification:
            status_->setText(QStringLiteral("需要补充信息：\n%1").arg(result.proposal.questions.join(QLatin1Char('\n'))));
            output_->clear();
            validateButton_->setEnabled(false);
            break;
        case wa::llm::ProposalStatus::Unsupported:
            status_->setText(QStringLiteral("当前版本不支持以下任务内容：\n%1")
                                 .arg(result.proposal.unsupported.join(QLatin1Char('\n'))));
            output_->clear();
            validateButton_->setEnabled(false);
            break;
    }
}

void WorkflowGenerationDialog::validateDraft() {
    if (currentProposal_.isEmpty()) return;
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(output_->toPlainText().toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        acceptedWorkflow_.reset();
        acceptButton_->setEnabled(false);
        status_->setText(QStringLiteral("草稿 JSON 无法解析：%1").arg(parseError.errorString()));
        return;
    }

    QJsonObject proposal = currentProposal_;
    proposal.insert(QStringLiteral("workflow"), document.object());
    const auto parsed = wa::llm::parseWorkflowProposal(proposal);
    showValidation(parsed);
}

void WorkflowGenerationDialog::showValidation(const wa::llm::WorkflowProposalParseResult& result) {
    acceptedWorkflow_.reset();
    acceptButton_->setEnabled(false);
    if (!result.valid) {
        QString detail = result.error;
        int shown = 0;
        for (const auto& issue : result.validation.issues) {
            if (issue.severity != wa::Severity::Error) continue;
            detail += QStringLiteral("\n%1：%2").arg(QString::fromUtf8(issue.path.c_str()),
                                                      QString::fromUtf8(issue.message.c_str()));
            if (++shown >= 20) break;
        }
        status_->setText(detail);
        return;
    }
    acceptedWorkflow_ = result.workflow;
    acceptButton_->setEnabled(true);
    status_->setText(QStringLiteral("本地流程校验通过；接受后只载入编辑器，不会自动运行。"));
}

void WorkflowGenerationDialog::acceptDraft() {
    if (!acceptedWorkflow_) return;
    accept();
}
