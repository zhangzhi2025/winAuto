#pragma once

#include <QJsonValue>
#include <QObject>
#include <QTimer>
#include <QWebSocket>

#include "AgentConnectionSettings.h"

namespace wa::agent {

enum class ConnectionState {
    Disconnected,
    Connecting,
    Starting,
    Resuming,
    Connected,
    Disconnecting,
    Failed
};

enum class TurnState { Idle, Running, Aborting, Succeeded, Aborted, Failed };
enum class EventType { Status, AssistantDelta, AssistantMessage, ToolActivity, Error, ProtocolError };

struct AgentEvent {
    EventType type = EventType::Status;
    QString text;
};

class AgentSession : public QObject {
    Q_OBJECT
public:
    explicit AgentSession(QObject* parent = nullptr);
    ~AgentSession() override;

    ConnectionState connectionState() const { return connectionState_; }
    TurnState turnState() const { return turnState_; }
    bool connected() const { return connectionState_ == ConnectionState::Connected; }
    bool turnActive() const { return turnActive_; }
    QString sessionId() const { return sessionId_; }
    QString workingDirectory() const { return workingDirectory_; }
    QString safeEndpoint() const { return safeUrlDisplay(settings_.webSocketUrl); }

    bool connectToServer(const AgentConnectionSettings& settings);
    bool sendPrompt(const QString& prompt);
    bool abortTurn();
    void disconnectFromServer();

signals:
    void connectionStateChanged(wa::agent::ConnectionState state);
    void turnStateChanged(wa::agent::TurnState state);
    void sessionConfirmed(const QString& sessionId, const QString& workingDirectory);
    void eventReceived(const wa::agent::AgentEvent& event);

private:
    void setConnectionState(ConnectionState state);
    void setTurnState(TurnState state);
    void sendJson(const QJsonObject& message);
    void onTextMessage(const QString& text);
    void handleProtocolError(const QString& reason);
    void failConnection(const QString& reason);
    void finishTurn(TurnState state, const QString& message = {});
    void clearSession();

    QWebSocket socket_;
    QTimer operationTimeout_;
    AgentConnectionSettings settings_;
    ConnectionState connectionState_ = ConnectionState::Disconnected;
    TurnState turnState_ = TurnState::Idle;
    QString sessionId_;
    QString workingDirectory_;
    bool turnActive_ = false;
    bool sawTurnStart_ = false;
    bool sawTurnError_ = false;
    bool sawTurnComplete_ = false;
};

}  // namespace wa::agent

Q_DECLARE_METATYPE(wa::agent::ConnectionState)
Q_DECLARE_METATYPE(wa::agent::TurnState)
Q_DECLARE_METATYPE(wa::agent::AgentEvent)
