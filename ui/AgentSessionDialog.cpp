#include "AgentSessionDialog.h"

#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr int kMaxHistoryCharacters = 1024 * 1024;

QString connectionStateName(wa::agent::ConnectionState state) {
    using wa::agent::ConnectionState;
    switch (state) {
        case ConnectionState::Disconnected: return QStringLiteral("未连接");
        case ConnectionState::Connecting: return QStringLiteral("正在连接");
        case ConnectionState::Starting: return QStringLiteral("正在创建会话");
        case ConnectionState::Resuming: return QStringLiteral("正在恢复会话");
        case ConnectionState::Connected: return QStringLiteral("已连接");
        case ConnectionState::Disconnecting: return QStringLiteral("正在断开");
        case ConnectionState::Failed: return QStringLiteral("连接失败");
    }
    return QStringLiteral("未知状态");
}

QString eventPrefix(wa::agent::EventType type) {
    using wa::agent::EventType;
    switch (type) {
        case EventType::Status: return QStringLiteral("[状态] ");
        case EventType::AssistantDelta: return QString();
        case EventType::AssistantMessage: return QStringLiteral("\n[Agent] ");
        case EventType::ToolActivity: return QStringLiteral("\n[工具活动] ");
        case EventType::Error: return QStringLiteral("\n[错误] ");
        case EventType::ProtocolError: return QStringLiteral("\n[协议错误] ");
    }
    return {};
}

}  // namespace

AgentSessionDialog::AgentSessionDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Agent WebSocket 会话"));
    resize(860, 680);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    url_ = new QLineEdit(this);
    url_->setPlaceholderText(QStringLiteral("ws://127.0.0.1:8765"));
    directory_ = new QLineEdit(this);
    auto* browse = new QPushButton(QStringLiteral("浏览…"), this);
    auto* directoryRow = new QHBoxLayout();
    directoryRow->addWidget(directory_, 1);
    directoryRow->addWidget(browse);
    sessionId_ = new QLineEdit(this);
    sessionId_->setPlaceholderText(QStringLiteral("留空以创建新会话；填写以恢复"));
    form->addRow(QStringLiteral("WebSocket 地址"), url_);
    form->addRow(QStringLiteral("Agent 项目目录"), directoryRow);
    form->addRow(QStringLiteral("Session ID"), sessionId_);
    layout->addLayout(form);

    auto* settingsButtons = new QHBoxLayout();
    auto* save = new QPushButton(QStringLiteral("保存设置"), this);
    auto* clear = new QPushButton(QStringLiteral("清除设置"), this);
    settingsButtons->addStretch();
    settingsButtons->addWidget(save);
    settingsButtons->addWidget(clear);
    layout->addLayout(settingsButtons);

    status_ = new QLabel(QStringLiteral("尚未连接"), this);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    sessionDetails_ = new QLabel(this);
    sessionDetails_->setWordWrap(true);
    layout->addWidget(sessionDetails_);
    history_ = new QPlainTextEdit(this);
    history_->setReadOnly(true);
    history_->setMaximumBlockCount(1000);
    layout->addWidget(history_, 1);

    prompt_ = new QLineEdit(this);
    prompt_->setPlaceholderText(QStringLiteral("输入 Agent 任务…"));
    prompt_->setMaxLength(1024 * 1024);
    auto* chatRow = new QHBoxLayout();
    chatRow->addWidget(prompt_, 1);
    send_ = new QPushButton(QStringLiteral("发送"), this);
    abort_ = new QPushButton(QStringLiteral("中止"), this);
    chatRow->addWidget(send_);
    chatRow->addWidget(abort_);
    layout->addLayout(chatRow);

    auto* connectionButtons = new QHBoxLayout();
    connectionButtons->addStretch();
    connect_ = new QPushButton(QStringLiteral("连接 / 开始或恢复"), this);
    disconnect_ = new QPushButton(QStringLiteral("断开"), this);
    auto* close = new QPushButton(QStringLiteral("关闭"), this);
    connectionButtons->addWidget(connect_);
    connectionButtons->addWidget(disconnect_);
    connectionButtons->addWidget(close);
    layout->addLayout(connectionButtons);

    session_ = new wa::agent::AgentSession(this);
    const auto saved = wa::agent::AgentSettingsStore::load();
    url_->setText(saved.webSocketUrl);
    directory_->setText(saved.workingDirectory);
    sessionId_->setText(saved.sessionId);
    send_->setEnabled(false);
    abort_->setEnabled(false);
    disconnect_->setEnabled(false);

    connect(browse, &QPushButton::clicked, this, &AgentSessionDialog::browseDirectory);
    connect(save, &QPushButton::clicked, this, &AgentSessionDialog::saveSettings);
    connect(clear, &QPushButton::clicked, this, &AgentSessionDialog::clearSettings);
    connect(connect_, &QPushButton::clicked, this, &AgentSessionDialog::connectSession);
    connect(disconnect_, &QPushButton::clicked, this, &AgentSessionDialog::disconnectSession);
    connect(send_, &QPushButton::clicked, this, &AgentSessionDialog::sendPrompt);
    connect(prompt_, &QLineEdit::returnPressed, this, &AgentSessionDialog::sendPrompt);
    connect(abort_, &QPushButton::clicked, this, &AgentSessionDialog::abortTurn);
    connect(close, &QPushButton::clicked, this, &QWidget::close);
    connect(session_, &wa::agent::AgentSession::eventReceived, this, &AgentSessionDialog::onEvent);
    connect(session_, &wa::agent::AgentSession::connectionStateChanged, this,
            [this](wa::agent::ConnectionState state) {
                onConnectionState(state);
                if (closePending_ && (state == wa::agent::ConnectionState::Disconnected ||
                                      state == wa::agent::ConnectionState::Failed)) {
                    closePending_ = false;
                    QTimer::singleShot(0, this, &QWidget::close);
                }
            });
    connect(session_, &wa::agent::AgentSession::turnStateChanged, this, [this](wa::agent::TurnState state) {
        send_->setEnabled(session_->connected() && !session_->turnActive());
        abort_->setEnabled(session_->turnActive() && state == wa::agent::TurnState::Running);
    });
    connect(session_, &wa::agent::AgentSession::sessionConfirmed, this,
            [this](const QString& id, const QString& workingDirectory) {
                sessionId_->setText(id);
                sessionDetails_->setText(QStringLiteral("活动会话：%1\n实际工作目录：%2")
                                             .arg(id, workingDirectory));
            });
}

AgentSessionDialog::~AgentSessionDialog() {
    if (session_) session_->disconnectFromServer();
}

void AgentSessionDialog::reject() {
    close();
}

wa::agent::AgentConnectionSettings AgentSessionDialog::formSettings() const {
    return {url_->text(), directory_->text(), sessionId_->text()};
}

void AgentSessionDialog::browseDirectory() {
    const QString selected = QFileDialog::getExistingDirectory(this, QStringLiteral("选择 Agent 项目目录"),
                                                               directory_->text());
    if (!selected.isEmpty()) directory_->setText(selected);
}

void AgentSessionDialog::saveSettings() {
    QString error;
    if (!wa::agent::AgentSettingsStore::save(formSettings(), &error)) {
        QMessageBox::warning(this, QStringLiteral("保存 Agent 设置失败"), error);
        return;
    }
    status_->setText(QStringLiteral("Agent 设置已保存。\n地址：%1").arg(wa::agent::safeUrlDisplay(url_->text())));
}

void AgentSessionDialog::clearSettings() {
    QString error;
    if (!wa::agent::AgentSettingsStore::clear(&error)) {
        QMessageBox::warning(this, QStringLiteral("清除 Agent 设置失败"), error);
        return;
    }
    url_->clear();
    directory_->clear();
    sessionId_->clear();
    sessionDetails_->clear();
    status_->setText(QStringLiteral("已清除 Agent 设置。"));
}

void AgentSessionDialog::connectSession() {
    const auto settings = formSettings();
    const auto validation = wa::agent::validateSettings(settings);
    if (!validation.ok()) {
        QMessageBox::warning(this, QStringLiteral("Agent 设置无效"), validation.error);
        return;
    }
    const QFileInfo directory(validation.normalized.workingDirectory);
    const auto answer = QMessageBox::question(
        this, QStringLiteral("检查 Agent 工作目录"),
        QStringLiteral("Agent 服务可能通过工具访问以下目录：\n\n%1\n\n确认连接？")
            .arg(QDir::toNativeSeparators(directory.canonicalFilePath())),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) return;

    QString error;
    if (!wa::agent::AgentSettingsStore::save(validation.normalized, &error)) {
        QMessageBox::warning(this, QStringLiteral("保存 Agent 设置失败"), error);
        return;
    }
    history_->clear();
    if (!session_->connectToServer(validation.normalized)) return;
    connect_->setEnabled(false);
    status_->setText(QStringLiteral("正在连接：%1").arg(wa::agent::safeUrlDisplay(validation.normalized.webSocketUrl)));
}

void AgentSessionDialog::disconnectSession() {
    session_->disconnectFromServer();
    disconnect_->setEnabled(false);
    status_->setText(QStringLiteral("正在断开 Agent 会话…"));
}

void AgentSessionDialog::sendPrompt() {
    const QString prompt = prompt_->text().trimmed();
    if (prompt.isEmpty()) {
        status_->setText(QStringLiteral("请求不能为空。"));
        return;
    }
    if (!session_->sendPrompt(prompt)) return;
    appendHistory(QStringLiteral("\n[你] %1").arg(prompt));
    prompt_->clear();
}

void AgentSessionDialog::abortTurn() {
    if (!session_->turnActive()) return;
    if (QMessageBox::question(this, QStringLiteral("中止 Agent 请求"),
                              QStringLiteral("请求 Agent 中止当前操作？"),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }
    session_->abortTurn();
}

void AgentSessionDialog::onEvent(const wa::agent::AgentEvent& event) {
    if (event.type == wa::agent::EventType::Status) status_->setText(event.text);
    const QString line = eventPrefix(event.type) + event.text;
    if (!line.isEmpty()) appendHistory(line);
}

void AgentSessionDialog::onConnectionState(wa::agent::ConnectionState state) {
    const bool active = state == wa::agent::ConnectionState::Connecting ||
                        state == wa::agent::ConnectionState::Starting ||
                        state == wa::agent::ConnectionState::Resuming ||
                        state == wa::agent::ConnectionState::Disconnecting;
    connect_->setEnabled(!active && state != wa::agent::ConnectionState::Connected);
    disconnect_->setEnabled(state == wa::agent::ConnectionState::Connected);
    send_->setEnabled(state == wa::agent::ConnectionState::Connected && !session_->turnActive());
    if (state == wa::agent::ConnectionState::Disconnected || state == wa::agent::ConnectionState::Failed) {
        sessionDetails_->clear();
        status_->setText(connectionStateName(state));
    }
}

void AgentSessionDialog::appendHistory(const QString& text) {
    history_->moveCursor(QTextCursor::End);
    history_->insertPlainText(text);
    const int excess = history_->toPlainText().size() - kMaxHistoryCharacters;
    if (excess > 0) {
        QTextCursor trim(history_->document());
        trim.setPosition(0);
        trim.setPosition(excess, QTextCursor::KeepAnchor);
        trim.removeSelectedText();
        const int nextLine = history_->toPlainText().indexOf(QLatin1Char('\n'));
        if (nextLine >= 0) {
            trim.setPosition(0);
            trim.setPosition(nextLine + 1, QTextCursor::KeepAnchor);
            trim.removeSelectedText();
        }
    }
    history_->ensureCursorVisible();
}

void AgentSessionDialog::closeEvent(QCloseEvent* event) {
    const auto state = session_->connectionState();
    if (state != wa::agent::ConnectionState::Disconnected && state != wa::agent::ConnectionState::Failed) {
        event->ignore();
        if (closePending_) return;
        closePending_ = true;
        connect_->setEnabled(false);
        disconnect_->setEnabled(false);
        send_->setEnabled(false);
        abort_->setEnabled(false);
        prompt_->setEnabled(false);
        status_->setText(QStringLiteral("正在安全断开 Agent 会话后关闭窗口…"));
        session_->disconnectFromServer();
        return;
    }
    event->accept();
}
