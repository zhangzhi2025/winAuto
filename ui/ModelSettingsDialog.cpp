#include "ModelSettingsDialog.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "ModelConnectionSettings.h"
#include "WorkflowCapabilities.h"

ModelSettingsDialog::ModelSettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("模型连接设置"));
    setMinimumWidth(580);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    provider_ = new QComboBox(this);
    provider_->addItem(QStringLiteral("Claude"), QStringLiteral("claude"));
    provider_->addItem(QStringLiteral("OpenAI"), QStringLiteral("openai"));
    model_ = new QLineEdit(this);
    model_->setMaxLength(128);
    model_->setPlaceholderText(QStringLiteral("输入服务当前支持的模型名称；不会自动切换模型"));
    credential_ = new QLineEdit(this);
    credential_->setEchoMode(QLineEdit::Password);
    credential_->setMaxLength(1024);
    credential_->setPlaceholderText(QStringLiteral("留空以保留已保存凭据"));
    form->addRow(QStringLiteral("模型服务"), provider_);
    form->addRow(QStringLiteral("模型名称"), model_);
    form->addRow(QStringLiteral("API 凭据"), credential_);
    layout->addLayout(form);

    auto* note = new QLabel(
        QStringLiteral("凭据按当前 Windows 用户使用 DPAPI 加密后保存；同一账户被攻陷时仍可能被读取。"
                       "保存的凭据不会显示，可输入新值替换或清除。"),
        this);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* capabilities = new QLabel(wa::llm::capabilitySummary(), this);
    capabilities->setWordWrap(true);
    layout->addWidget(capabilities);
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    layout->addWidget(status_);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch();
    auto* save = new QPushButton(QStringLiteral("保存设置"), this);
    auto* clear = new QPushButton(QStringLiteral("清除凭据"), this);
    auto* close = new QPushButton(QStringLiteral("关闭"), this);
    buttons->addWidget(save);
    buttons->addWidget(clear);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    connect(provider_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { loadProviderSettings(); });
    connect(save, &QPushButton::clicked, this, &ModelSettingsDialog::saveSettings);
    connect(clear, &QPushButton::clicked, this, &ModelSettingsDialog::clearCredential);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    loadProviderSettings();
}

void ModelSettingsDialog::loadProviderSettings() {
    wa::llm::Provider provider;
    if (!wa::llm::parseProvider(provider_->currentData().toString(), &provider)) return;
    model_->setText(wa::llm::ModelConnectionSettings::load(provider).modelName);
    credential_->clear();
    status_->setText(wa::llm::ModelConnectionSettings::hasCredential(provider)
                         ? QStringLiteral("已保存访问凭据；输入新值可替换，清除按钮可移除。凭据不会回显。")
                         : QStringLiteral("尚未保存访问凭据；输入新值后保存。"));
}

void ModelSettingsDialog::saveSettings() {
    wa::llm::Provider provider;
    if (!wa::llm::parseProvider(provider_->currentData().toString(), &provider)) {
        status_->setText(QStringLiteral("请选择受支持的模型服务。"));
        return;
    }
    QString error;
    if (!wa::llm::ModelConnectionSettings::save(provider, model_->text(), &error)) {
        status_->setText(error);
        return;
    }
    QString secret = credential_->text();
    if (!secret.isEmpty()) {
        if (!wa::llm::ModelConnectionSettings::saveCredential(provider, secret, &error)) {
            secret.fill(QChar::Null);
            status_->setText(error);
            return;
        }
        secret.fill(QChar::Null);
        credential_->clear();
    }
    status_->setText(QStringLiteral("模型设置已保存；当前凭据保持不变或已安全替换。"));
}

void ModelSettingsDialog::clearCredential() {
    wa::llm::Provider provider;
    if (!wa::llm::parseProvider(provider_->currentData().toString(), &provider)) return;
    QString error;
    if (!wa::llm::ModelConnectionSettings::clearCredential(provider, &error)) {
        status_->setText(error);
        return;
    }
    credential_->clear();
    status_->setText(QStringLiteral("已清除该服务的凭据。"));
}
