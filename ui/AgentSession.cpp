#include "AgentSession.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUrl>

namespace wa::agent {
namespace {

constexpr int kConnectTimeoutMs = 15000;
constexpr int kSessionTimeoutMs = 15000;
constexpr int kTurnTimeoutMs = 180000;
constexpr int kAbortTimeoutMs = 20000;
constexpr int kMaxFrameBytes = 2 * 1024 * 1024;
constexpr int kMaxPromptBytes = 1024 * 1024;
constexpr int kMaxDisplayText = 64 * 1024;

QString bounded(const QString& value, int maxLength = kMaxDisplayText) { return value.left(maxLength); }

}  // namespace

AgentSession::AgentSession(QObject* parent) : QObject(parent) {
    qRegisterMetaType<ConnectionState>();
    qRegisterMetaType<TurnState>();
    qRegisterMetaType<AgentEvent>();
    operationTimeout_.setSingleShot(true);
    connect(&operationTimeout_, &QTimer::timeout, this, [this] {
        switch (connectionState_) {
            case ConnectionState::Connecting:
            case ConnectionState::Starting:
            case ConnectionState::Resuming:
                failConnection(QStringLiteral("Agent 连接或会话确认超时。"));
                return;
            case ConnectionState::Disconnecting:
                if (turnActive_) finishTurn(TurnState::Failed, QStringLiteral("断开超时，本轮未确认完成。"));
                socket_.close();
                clearSession();
                setConnectionState(ConnectionState::Disconnected);
                return;
            default: break;
        }
        if (turnState_ == TurnState::Aborting) {
            finishTurn(TurnState::Failed, QStringLiteral("Agent 未确认中止请求，连接已断开。"));
            failConnection(QStringLiteral("中止请求超时，已断开 Agent 会话。"));
        } else if (turnActive_) {
            failConnection(QStringLiteral("Agent 请求超时；连接已关闭以避免重复提交。"));
        }
    });

    connect(&socket_, &QWebSocket::connected, this, [this] {
        if (connectionState_ != ConnectionState::Connecting) return;
        if (settings_.sessionId.isEmpty()) {
            setConnectionState(ConnectionState::Starting);
            sendJson(QJsonObject{{QStringLiteral("type"), QStringLiteral("start")},
                                 {QStringLiteral("working_directory"), settings_.workingDirectory}});
        } else {
            setConnectionState(ConnectionState::Resuming);
            sendJson(QJsonObject{{QStringLiteral("type"), QStringLiteral("resume")},
                                 {QStringLiteral("session_id"), settings_.sessionId}});
        }
        operationTimeout_.start(kSessionTimeoutMs);
    });
    connect(&socket_, &QWebSocket::textMessageReceived, this, &AgentSession::onTextMessage);
    connect(&socket_, &QWebSocket::disconnected, this, [this] {
        operationTimeout_.stop();
        if (turnActive_) finishTurn(TurnState::Failed, QStringLiteral("Agent 连接已断开，本轮未确认完成。"));
        clearSession();
        setConnectionState(ConnectionState::Disconnected);
        emit eventReceived({EventType::Status, QStringLiteral("已断开 Agent。")});
    });
    connect(&socket_, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this,
            [this](QAbstractSocket::SocketError) {
                if (connectionState_ == ConnectionState::Disconnected) return;
                const QString error = redactSensitiveText(socket_.errorString(), socket_.requestUrl());
                if (connectionState_ == ConnectionState::Connecting || connectionState_ == ConnectionState::Starting ||
                    connectionState_ == ConnectionState::Resuming) {
                    failConnection(QStringLiteral("Agent 连接失败：%1").arg(error));
                } else {
                    emit eventReceived({EventType::Error, QStringLiteral("Agent 传输错误：%1").arg(error)});
                }
            });
}

AgentSession::~AgentSession() {
    operationTimeout_.stop();
    socket_.abort();
    clearSession();
}

bool AgentSession::connectToServer(const AgentConnectionSettings& settings) {
    if (connectionState_ != ConnectionState::Disconnected && connectionState_ != ConnectionState::Failed) {
        emit eventReceived({EventType::Error, QStringLiteral("Agent 会话尚未断开。")});
        return false;
    }
    const SettingsValidation validation = validateSettings(settings);
    if (!validation.ok()) {
        emit eventReceived({EventType::Error, validation.error});
        setConnectionState(ConnectionState::Failed);
        return false;
    }
    settings_ = validation.normalized;
    clearSession();
    sawTurnError_ = false;
    sawTurnComplete_ = false;
    sawTurnStart_ = false;
    setConnectionState(ConnectionState::Connecting);
    operationTimeout_.start(kConnectTimeoutMs);
    socket_.open(QUrl(settings_.webSocketUrl));
    return true;
}

bool AgentSession::sendPrompt(const QString& prompt) {
    if (!connected()) {
        emit eventReceived({EventType::Error, QStringLiteral("请先连接 Agent 会话。")});
        return false;
    }
    if (turnActive_) {
        emit eventReceived({EventType::Error, QStringLiteral("当前请求尚未结束，不能并发提交。")});
        return false;
    }
    const QString normalized = prompt.trimmed();
    if (normalized.isEmpty() || normalized.toUtf8().size() > kMaxPromptBytes) {
        emit eventReceived({EventType::Error, QStringLiteral("请求不能为空且不得超过 1 MiB。")});
        return false;
    }
    turnActive_ = true;
    sawTurnStart_ = false;
    sawTurnError_ = false;
    sawTurnComplete_ = false;
    setTurnState(TurnState::Running);
    sendJson(QJsonObject{{QStringLiteral("type"), QStringLiteral("chat")},
                         {QStringLiteral("prompt"), normalized}});
    operationTimeout_.start(kTurnTimeoutMs);
    emit eventReceived({EventType::Status, QStringLiteral("正在处理请求。")});
    return true;
}

bool AgentSession::abortTurn() {
    if (!turnActive_ || turnState_ != TurnState::Running) return false;
    setTurnState(TurnState::Aborting);
    sendJson(QJsonObject{{QStringLiteral("type"), QStringLiteral("abort")}});
    operationTimeout_.start(kAbortTimeoutMs);
    emit eventReceived({EventType::Status, QStringLiteral("正在请求 Agent 中止。")});
    return true;
}

void AgentSession::disconnectFromServer() {
    if (socket_.state() == QAbstractSocket::UnconnectedState) {
        clearSession();
        setConnectionState(ConnectionState::Disconnected);
        return;
    }
    setConnectionState(ConnectionState::Disconnecting);
    if (turnActive_ && turnState_ == TurnState::Running) {
        setTurnState(TurnState::Aborting);
        sendJson(QJsonObject{{QStringLiteral("type"), QStringLiteral("abort")}});
    }
    sendJson(QJsonObject{{QStringLiteral("type"), QStringLiteral("disconnect")}});
    operationTimeout_.start(kSessionTimeoutMs);
}

void AgentSession::setConnectionState(ConnectionState state) {
    if (connectionState_ == state) return;
    connectionState_ = state;
    emit connectionStateChanged(state);
}

void AgentSession::setTurnState(TurnState state) {
    if (turnState_ == state) return;
    turnState_ = state;
    emit turnStateChanged(state);
}

void AgentSession::sendJson(const QJsonObject& message) {
    const QByteArray frame = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (frame.size() > kMaxFrameBytes || socket_.state() != QAbstractSocket::ConnectedState) {
        handleProtocolError(QStringLiteral("无法发送消息：连接未就绪或消息超出大小限制。"));
        return;
    }
    socket_.sendTextMessage(QString::fromUtf8(frame));
}

void AgentSession::onTextMessage(const QString& text) {
    const QByteArray utf8 = text.toUtf8();
    if (utf8.size() > kMaxFrameBytes) {
        handleProtocolError(QStringLiteral("Agent 消息超过 2 MiB 限制。"));
        return;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(utf8, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        handleProtocolError(QStringLiteral("Agent 返回了无效 JSON 对象。"));
        return;
    }
    const QJsonObject message = document.object();
    const QString type = message.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("session")) {
        const QString action = message.value(QStringLiteral("action")).toString();
        const QString id = message.value(QStringLiteral("session_id")).toString().trimmed();
        if (id.isEmpty() || id.size() > 512 ||
            (action == QLatin1String("resumed") && id != settings_.sessionId) ||
            (action == QLatin1String("started") && connectionState_ != ConnectionState::Starting) ||
            (action == QLatin1String("resumed") && connectionState_ != ConnectionState::Resuming) ||
            (action != QLatin1String("started") && action != QLatin1String("resumed"))) {
            handleProtocolError(QStringLiteral("Agent 会话确认消息无效或状态不匹配。"));
            return;
        }
        QString actualDirectory;
        if (action == QLatin1String("started")) {
            const QJsonValue directoryValue = message.value(QStringLiteral("working_directory"));
            if (!directoryValue.isString() || directoryValue.toString().trimmed().isEmpty()) {
                handleProtocolError(QStringLiteral("Agent 新建会话确认缺少实际工作目录。"));
                return;
            }
            actualDirectory = QDir::cleanPath(directoryValue.toString());
        } else {
            actualDirectory = QStringLiteral("服务未返回恢复会话的实际目录");
        }
        operationTimeout_.stop();
        sessionId_ = id;
        workingDirectory_ = actualDirectory;
        setConnectionState(ConnectionState::Connected);
        setTurnState(TurnState::Idle);
        emit sessionConfirmed(sessionId_, workingDirectory_);
        emit eventReceived({EventType::Status, QStringLiteral("Agent 会话已连接。")});
        return;
    }
    if (type == QLatin1String("turn_start")) {
        if (!turnActive_ || message.value(QStringLiteral("session_id")).toString() != sessionId_) {
            handleProtocolError(QStringLiteral("收到当前会话之外的 turn_start。"));
            return;
        }
        if (sawTurnStart_) {
            handleProtocolError(QStringLiteral("当前请求重复收到 turn_start。"));
            return;
        }
        sawTurnStart_ = true;
        emit eventReceived({EventType::Status, QStringLiteral("Agent 已开始处理。")});
    } else if (type == QLatin1String("delta") || type == QLatin1String("message")) {
        if (!turnActive_ || !sawTurnStart_ || !message.value(QStringLiteral("text")).isString()) {
            handleProtocolError(QStringLiteral("Agent 文本事件缺少有效 text 字段。"));
            return;
        }
        const EventType eventType = type == QLatin1String("delta") ? EventType::AssistantDelta
                                                                    : EventType::AssistantMessage;
        emit eventReceived({eventType, bounded(message.value(QStringLiteral("text")).toString())});
    } else if (type == QLatin1String("tool_event")) {
        if (!turnActive_ || !sawTurnStart_ || !message.contains(QStringLiteral("event"))) {
            handleProtocolError(QStringLiteral("Agent tool_event 缺少 event 字段。"));
            return;
        }
        const QJsonValue value = message.value(QStringLiteral("event"));
        QString display;
        if (value.isObject()) {
            display = QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
        } else if (value.isArray()) {
            display = QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
        } else {
            display = value.toVariant().toString();
        }
        emit eventReceived({EventType::ToolActivity, bounded(display, 4096)});
    } else if (type == QLatin1String("error")) {
        if (!message.value(QStringLiteral("error")).isString()) {
            handleProtocolError(QStringLiteral("Agent error 事件缺少有效错误文本。"));
            return;
        }
        const QString safeError = redactSensitiveText(message.value(QStringLiteral("error")).toString(),
                                                      QUrl(settings_.webSocketUrl));
        emit eventReceived({EventType::Error, safeError});
        if (turnActive_) {
            sawTurnError_ = true;
            setTurnState(TurnState::Failed);
        } else if (connectionState_ == ConnectionState::Starting ||
                   connectionState_ == ConnectionState::Resuming) {
            failConnection(safeError);
        }
    } else if (type == QLatin1String("turn_complete")) {
        if (!turnActive_ || !sawTurnStart_) {
            handleProtocolError(QStringLiteral("没有活动请求时收到 turn_complete。"));
            return;
        }
        sawTurnComplete_ = true;
        operationTimeout_.stop();
        setTurnState(sawTurnError_ ? TurnState::Failed : TurnState::Succeeded);
        emit eventReceived({EventType::Status, sawTurnError_ ? QStringLiteral("请求失败。")
                                                             : QStringLiteral("请求已完成。")});
    } else if (type == QLatin1String("turn_end")) {
        if (!turnActive_) return;
        operationTimeout_.stop();
        if (!sawTurnComplete_) {
            if (!sawTurnError_) {
                emit eventReceived({EventType::ProtocolError,
                                    QStringLiteral("收到 turn_end，但没有成功完成确认。")});
                setTurnState(TurnState::Failed);
            }
            if (turnState_ == TurnState::Aborting) setTurnState(TurnState::Failed);
        }
        turnActive_ = false;
    } else if (type == QLatin1String("status")) {
        const QString status = message.value(QStringLiteral("status")).toString();
        if (status == QLatin1String("aborted")) {
            if (turnActive_ && turnState_ == TurnState::Aborting) {
                operationTimeout_.stop();
                turnActive_ = false;
                setTurnState(TurnState::Aborted);
                emit eventReceived({EventType::Status, QStringLiteral("请求已中止，会话仍保持连接。")});
            } else {
                emit eventReceived({EventType::Status, QStringLiteral("Agent 已确认中止。")});
            }
        } else if (status == QLatin1String("disconnected")) {
            operationTimeout_.stop();
            if (turnActive_) finishTurn(TurnState::Failed, QStringLiteral("会话断开，本轮未确认完成。"));
            clearSession();
            setConnectionState(ConnectionState::Disconnected);
            socket_.close();
        } else {
            handleProtocolError(QStringLiteral("Agent status 值未知。"));
        }
    } else if (type == QLatin1String("pong")) {
        emit eventReceived({EventType::Status, QStringLiteral("Agent 服务在线。")});
    } else {
        handleProtocolError(QStringLiteral("Agent 返回未知消息类型。"));
    }
}

void AgentSession::handleProtocolError(const QString& reason) {
    emit eventReceived({EventType::ProtocolError, reason});
    if (turnActive_) finishTurn(TurnState::Failed, reason);
    if (connectionState_ == ConnectionState::Connecting || connectionState_ == ConnectionState::Starting ||
        connectionState_ == ConnectionState::Resuming) {
        failConnection(reason);
    } else if (socket_.state() == QAbstractSocket::ConnectedState) {
        socket_.close(QWebSocketProtocol::CloseCodeProtocolError, QStringLiteral("protocol error"));
    }
}

void AgentSession::failConnection(const QString& reason) {
    operationTimeout_.stop();
    emit eventReceived({EventType::Error, reason});
    if (turnActive_) finishTurn(TurnState::Failed, reason);
    clearSession();
    setConnectionState(ConnectionState::Failed);
    socket_.abort();
}

void AgentSession::finishTurn(TurnState state, const QString& message) {
    operationTimeout_.stop();
    turnActive_ = false;
    setTurnState(state);
    if (!message.isEmpty()) emit eventReceived({EventType::Error, message});
}

void AgentSession::clearSession() {
    sessionId_.clear();
    workingDirectory_.clear();
    turnActive_ = false;
    sawTurnError_ = false;
    sawTurnStart_ = false;
    sawTurnComplete_ = false;
}

}  // namespace wa::agent
