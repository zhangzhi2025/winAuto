#include "AgentConnectionSettings.h"
#include "AgentSession.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QWebSocketServer>

#include <functional>
#include <iostream>

namespace {

bool waitFor(const std::function<bool()>& condition, int timeoutMs = 3000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }
    return condition();
}

bool check(bool condition, const char* message) {
    if (condition) return true;
    std::cerr << "FAIL: " << message << '\n';
    return false;
}

QJsonObject objectFromText(const QString& text) {
    return QJsonDocument::fromJson(text.toUtf8()).object();
}

bool testSettingsValidationAndIsolation(const QString& settingsRoot) {
    QTemporaryDir directory;
    if (!check(directory.isValid(), "temporary directory should be available")) return false;

    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsRoot);
    QSettings modelSettings(QSettings::IniFormat, QSettings::UserScope,
                            QStringLiteral("WinAuto"), QStringLiteral("WinAuto"));
    modelSettings.setValue(QStringLiteral("openai/model"), QStringLiteral("model-before"));
    modelSettings.setValue(QStringLiteral("claude/model"), QStringLiteral("claude-before"));
    modelSettings.sync();

    const wa::agent::AgentConnectionSettings settings{
        QStringLiteral("ws://localhost:8765"), directory.path(), QStringLiteral("session-123")};
    QString error;
    if (!check(wa::agent::AgentSettingsStore::save(settings, &error), "valid settings should save")) return false;
    const auto loaded = wa::agent::AgentSettingsStore::load();
    if (!check(loaded.webSocketUrl == settings.webSocketUrl, "URL should persist")) return false;
    if (!check(loaded.sessionId == settings.sessionId, "session ID should persist")) return false;
    if (!check(QDir(loaded.workingDirectory).canonicalPath() == directory.path(),
               "working directory should be canonical")) return false;

    const auto invalidUrl = wa::agent::validateSettings(
        {QStringLiteral("https://localhost"), directory.path(), {}});
    if (!check(!invalidUrl.ok(), "non-WebSocket URL should be rejected")) return false;
    const auto invalidDirectory = wa::agent::validateSettings(
        {QStringLiteral("ws://localhost:8765"), directory.path() + QStringLiteral("/missing"), {}});
    if (!check(!invalidDirectory.ok(), "missing directory should be rejected")) return false;
    const QString safeUrl = wa::agent::safeUrlDisplay(
        QStringLiteral("wss://user:password@example.test/agent?access_token=secret&mode=chat#fragment"));
    if (!check(!safeUrl.contains(QStringLiteral("password")) && !safeUrl.contains(QStringLiteral("secret")) &&
                   !safeUrl.contains(QStringLiteral("fragment")) && safeUrl.contains(QStringLiteral("mode=chat")),
               "URL display should redact credentials and sensitive query values")) return false;

    if (!check(wa::agent::AgentSettingsStore::clear(&error), "Agent settings should clear")) return false;
    modelSettings.sync();
    if (!check(modelSettings.value(QStringLiteral("openai/model")).toString() == QStringLiteral("model-before"),
               "clearing Agent settings must preserve OpenAI model settings")) return false;
    if (!check(modelSettings.value(QStringLiteral("claude/model")).toString() == QStringLiteral("claude-before"),
               "clearing Agent settings must preserve Claude model settings")) return false;
    return check(!modelSettings.contains(QStringLiteral("agent/webSocketUrl")),
                 "Agent values use their own settings prefix");
}

bool testStartChatAbortAndDisconnect() {
    QTemporaryDir directory;
    QWebSocketServer server(QStringLiteral("agent test"), QWebSocketServer::NonSecureMode);
    if (!check(directory.isValid(), "temporary directory should be available")) return false;
    if (!check(server.listen(QHostAddress::LocalHost, 0), "test WebSocket server should listen")) return false;

    QWebSocket* peer = nullptr;
    QStringList received;
    QObject::connect(&server, &QWebSocketServer::newConnection, &server, [&] {
        peer = server.nextPendingConnection();
        QObject::connect(peer, &QWebSocket::textMessageReceived, &server, [&](const QString& text) {
            received.append(text);
            const QJsonObject request = objectFromText(text);
            if (request.value(QStringLiteral("type")).toString() == QLatin1String("start")) {
                peer->sendTextMessage(QJsonDocument(QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("session")},
                    {QStringLiteral("action"), QStringLiteral("started")},
                    {QStringLiteral("session_id"), QStringLiteral("new-session")},
                    {QStringLiteral("working_directory"), directory.path()}}).toJson(QJsonDocument::Compact));
            } else if (request.value(QStringLiteral("type")).toString() == QLatin1String("chat")) {
                peer->sendTextMessage(QStringLiteral("{\"type\":\"turn_start\",\"session_id\":\"new-session\"}"));
                peer->sendTextMessage(QStringLiteral("{\"type\":\"delta\",\"text\":\"streamed\"}"));
                peer->sendTextMessage(QStringLiteral("{\"type\":\"tool_event\",\"event\":{\"name\":\"read\"}}"));
            } else if (request.value(QStringLiteral("type")).toString() == QLatin1String("abort")) {
                peer->sendTextMessage(QStringLiteral("{\"type\":\"status\",\"status\":\"aborted\"}"));
            } else if (request.value(QStringLiteral("type")).toString() == QLatin1String("disconnect")) {
                peer->sendTextMessage(QStringLiteral("{\"type\":\"status\",\"status\":\"disconnected\"}"));
            }
        });
    });

    wa::agent::AgentSession session;
    QString confirmedId;
    QString confirmedDirectory;
    QObject::connect(&session, &wa::agent::AgentSession::sessionConfirmed, &session,
                     [&](const QString& id, const QString& path) {
                         confirmedId = id;
                         confirmedDirectory = path;
                     });
    const auto settings = wa::agent::AgentConnectionSettings{
        QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort()), directory.path(), {}};
    if (!check(session.connectToServer(settings), "session should accept valid connection settings")) return false;
    if (!check(waitFor([&] { return session.connected(); }), "start session should be confirmed")) return false;
    if (!check(confirmedId == QStringLiteral("new-session"), "only server-confirmed ID should be active")) return false;
    if (!check(confirmedDirectory == directory.path(), "server directory should be reported")) return false;

    int deltas = 0;
    int toolEvents = 0;
    QObject::connect(&session, &wa::agent::AgentSession::eventReceived, &session,
                     [&](const wa::agent::AgentEvent& event) {
                         if (event.type == wa::agent::EventType::AssistantDelta) ++deltas;
                         if (event.type == wa::agent::EventType::ToolActivity) ++toolEvents;
                     });
    if (!check(session.sendPrompt(QStringLiteral("first task")), "first prompt should be accepted")) return false;
    if (!check(!session.sendPrompt(QStringLiteral("duplicate")), "concurrent prompt should be rejected")) return false;
    if (!check(waitFor([&] { return received.size() >= 2; }), "start and chat requests should reach server")) return false;
    if (!check(objectFromText(received.at(1)).value(QStringLiteral("type")).toString() == QLatin1String("chat"),
               "prompt should use the chat protocol message")) return false;
    if (!check(waitFor([&] { return deltas == 1 && toolEvents == 1; }),
               "streamed text and tool activity should be decoded")) return false;

    if (!check(session.abortTurn(), "active request should be abortable")) return false;
    if (!check(waitFor([&] { return !session.turnActive(); }), "abort confirmation should end active turn")) return false;
    if (!check(session.turnState() == wa::agent::TurnState::Aborted, "abort should be reported as aborted")) return false;

    session.disconnectFromServer();
    if (!check(waitFor([&] { return session.connectionState() == wa::agent::ConnectionState::Disconnected; }),
               "disconnect confirmation should clear the session")) return false;
    if (!check(session.sessionId().isEmpty(), "disconnected session ID should be cleared")) return false;
    return true;
}

bool testResumeAndProtocolFailure() {
    QWebSocketServer server(QStringLiteral("agent resume test"), QWebSocketServer::NonSecureMode);
    if (!check(server.listen(QHostAddress::LocalHost, 0), "resume test server should listen")) return false;
    QTemporaryDir directory;
    QWebSocket* peer = nullptr;
    QObject::connect(&server, &QWebSocketServer::newConnection, &server, [&] {
        peer = server.nextPendingConnection();
        QObject::connect(peer, &QWebSocket::textMessageReceived, &server, [&](const QString& text) {
            const QJsonObject request = objectFromText(text);
            if (request.value(QStringLiteral("type")).toString() == QLatin1String("resume")) {
                peer->sendTextMessage(QJsonDocument(QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("session")},
                    {QStringLiteral("action"), QStringLiteral("resumed")},
                    {QStringLiteral("session_id"), request.value(QStringLiteral("session_id"))}}).toJson(QJsonDocument::Compact));
            } else if (request.value(QStringLiteral("type")).toString() == QLatin1String("ping")) {
                peer->sendTextMessage(QStringLiteral("{not-json"));
            }
        });
    });

    wa::agent::AgentSession session;
    const auto settings = wa::agent::AgentConnectionSettings{
        QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort()), directory.path(),
        QStringLiteral("existing-session")};
    if (!check(session.connectToServer(settings), "resume settings should be accepted")) return false;
    if (!check(waitFor([&] { return session.connected(); }), "resume should be confirmed")) return false;
    if (!check(session.sessionId() == QStringLiteral("existing-session"), "resume should preserve confirmed ID")) return false;

    int protocolErrors = 0;
    QObject::connect(&session, &wa::agent::AgentSession::eventReceived, &session,
                     [&](const wa::agent::AgentEvent& event) {
                         if (event.type == wa::agent::EventType::ProtocolError) ++protocolErrors;
                     });
    peer->sendTextMessage(QStringLiteral("{\"type\":\"unknown\"}"));
    if (!check(waitFor([&] { return protocolErrors > 0; }), "unknown message should be rejected explicitly")) return false;
    if (!check(waitFor([&] { return session.connectionState() == wa::agent::ConnectionState::Disconnected; }),
               "protocol failure should close and clear the session")) return false;
    return check(session.sessionId().isEmpty(), "protocol failure should clear stale session ID");
}

bool testTurnTerminalSemantics() {
    QWebSocketServer server(QStringLiteral("agent turn test"), QWebSocketServer::NonSecureMode);
    if (!check(server.listen(QHostAddress::LocalHost, 0), "turn test server should listen")) return false;
    QTemporaryDir directory;
    QWebSocket* peer = nullptr;
    QObject::connect(&server, &QWebSocketServer::newConnection, &server, [&] {
        peer = server.nextPendingConnection();
        QObject::connect(peer, &QWebSocket::textMessageReceived, &server, [&](const QString& text) {
            const QJsonObject request = objectFromText(text);
            const QString type = request.value(QStringLiteral("type")).toString();
            if (type == QLatin1String("start")) {
                peer->sendTextMessage(QJsonDocument(QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("session")},
                    {QStringLiteral("action"), QStringLiteral("started")},
                    {QStringLiteral("session_id"), QStringLiteral("turn-session")},
                    {QStringLiteral("working_directory"), directory.path()}}).toJson(QJsonDocument::Compact));
            } else if (type == QLatin1String("chat")) {
                peer->sendTextMessage(QStringLiteral("{\"type\":\"turn_start\",\"session_id\":\"turn-session\"}"));
                const QString prompt = request.value(QStringLiteral("prompt")).toString();
                if (prompt == QLatin1String("error")) {
                    peer->sendTextMessage(QStringLiteral("{\"type\":\"error\",\"error\":\"failed\"}"));
                    peer->sendTextMessage(QStringLiteral("{\"type\":\"turn_complete\"}"));
                } else if (prompt == QLatin1String("incomplete")) {
                    peer->sendTextMessage(QStringLiteral("{\"type\":\"turn_end\"}"));
                    return;
                } else {
                    peer->sendTextMessage(QStringLiteral("{\"type\":\"turn_complete\"}"));
                }
                peer->sendTextMessage(QStringLiteral("{\"type\":\"turn_end\"}"));
            }
        });
    });

    wa::agent::AgentSession session;
    const auto settings = wa::agent::AgentConnectionSettings{
        QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort()), directory.path(), {}};
    if (!check(session.connectToServer(settings), "turn session should connect")) return false;
    if (!check(waitFor([&] { return session.connected(); }), "turn session should be confirmed")) return false;

    if (!check(session.sendPrompt(QStringLiteral("success")), "success prompt should be accepted")) return false;
    if (!check(waitFor([&] { return !session.turnActive(); }), "successful turn should end")) return false;
    if (!check(session.turnState() == wa::agent::TurnState::Succeeded, "turn_complete should mean success")) return false;

    if (!check(session.sendPrompt(QStringLiteral("error")), "error prompt should be accepted")) return false;
    if (!check(waitFor([&] { return !session.turnActive(); }), "errored turn should end")) return false;
    if (!check(session.turnState() == wa::agent::TurnState::Failed,
               "an earlier error must not be overwritten by turn_complete")) return false;

    if (!check(session.sendPrompt(QStringLiteral("incomplete")), "incomplete prompt should be accepted")) return false;
    if (!check(waitFor([&] { return !session.turnActive(); }), "turn_end should clean up the turn")) return false;
    return check(session.turnState() == wa::agent::TurnState::Failed,
                 "turn_end without turn_complete must not report success");
}

bool testRemoteCloseAndSessionTimeout() {
    QWebSocketServer server(QStringLiteral("agent failure test"), QWebSocketServer::NonSecureMode);
    if (!check(server.listen(QHostAddress::LocalHost, 0), "failure test server should listen")) return false;
    QTemporaryDir directory;
    QWebSocket* peer = nullptr;
    QObject::connect(&server, &QWebSocketServer::newConnection, &server, [&] {
        peer = server.nextPendingConnection();
        QObject::connect(peer, &QWebSocket::textMessageReceived, &server, [&](const QString& text) {
            const QJsonObject request = objectFromText(text);
            const QString type = request.value(QStringLiteral("type")).toString();
            if (type == QLatin1String("start")) {
                peer->sendTextMessage(QJsonDocument(QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("session")},
                    {QStringLiteral("action"), QStringLiteral("started")},
                    {QStringLiteral("session_id"), QStringLiteral("remote-close-session")},
                    {QStringLiteral("working_directory"), directory.path()}}).toJson(QJsonDocument::Compact));
            } else if (type == QLatin1String("chat")) {
                peer->sendTextMessage(QStringLiteral("{\"type\":\"turn_start\",\"session_id\":\"remote-close-session\"}"));
                peer->close();
            }
        });
    });

    wa::agent::AgentSession session;
    const auto settings = wa::agent::AgentConnectionSettings{
        QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort()), directory.path(), {}};
    if (!check(session.connectToServer(settings), "remote-close session should connect")) return false;
    if (!check(waitFor([&] { return session.connected(); }), "remote-close session should be confirmed")) return false;
    if (!check(session.sendPrompt(QStringLiteral("remote close")), "remote-close prompt should be accepted")) return false;
    if (!check(waitFor([&] { return session.connectionState() == wa::agent::ConnectionState::Disconnected; }),
               "remote close should clear the connection")) return false;
    if (!check(session.turnState() == wa::agent::TurnState::Failed,
               "remote close during a turn must fail rather than succeed")) return false;

    QWebSocketServer silentServer(QStringLiteral("agent timeout test"), QWebSocketServer::NonSecureMode);
    if (!check(silentServer.listen(QHostAddress::LocalHost, 0), "timeout test server should listen")) return false;
    QObject::connect(&silentServer, &QWebSocketServer::newConnection, &silentServer, [&] {
        auto* silentPeer = silentServer.nextPendingConnection();
        QObject::connect(silentPeer, &QWebSocket::textMessageReceived, &silentServer,
                         [](const QString&) {});
    });
    wa::agent::AgentSession timedSession;
    const auto timeoutSettings = wa::agent::AgentConnectionSettings{
        QStringLiteral("ws://127.0.0.1:%1").arg(silentServer.serverPort()), directory.path(), {}};
    if (!check(timedSession.connectToServer(timeoutSettings), "timeout session should connect")) return false;
    if (!check(waitFor([&] { return timedSession.connectionState() == wa::agent::ConnectionState::Disconnected; },
                       18000),
               "missing session confirmation should time out and close")) return false;
    return check(timedSession.sessionId().isEmpty(), "timeout should not retain a stale session ID");
}

bool testRejectedResume() {
    QWebSocketServer server(QStringLiteral("agent rejected resume"), QWebSocketServer::NonSecureMode);
    if (!check(server.listen(QHostAddress::LocalHost, 0), "rejected-resume server should listen")) return false;
    QTemporaryDir directory;
    QObject::connect(&server, &QWebSocketServer::newConnection, &server, [&] {
        auto* peer = server.nextPendingConnection();
        QObject::connect(peer, &QWebSocket::textMessageReceived, peer, [peer](const QString&) {
            peer->sendTextMessage(QStringLiteral("{\"type\":\"error\",\"error\":\"session not found\"}"));
        });
    });

    wa::agent::AgentSession session;
    const auto settings = wa::agent::AgentConnectionSettings{
        QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort()), directory.path(),
        QStringLiteral("missing-session")};
    if (!check(session.connectToServer(settings), "rejected-resume request should start")) return false;
    if (!check(waitFor([&] { return session.connectionState() == wa::agent::ConnectionState::Disconnected; }),
               "server rejection should close the failed resume")) return false;
    return check(session.sessionId().isEmpty(), "failed restore must not leave an active session ID");
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir settingsRoot;
    if (!settingsRoot.isValid()) return 1;
    if (!testSettingsValidationAndIsolation(settingsRoot.path())) return 1;
    if (!testStartChatAbortAndDisconnect()) return 1;
    if (!testResumeAndProtocolFailure()) return 1;
    if (!testTurnTerminalSemantics()) return 1;
    if (!testRemoteCloseAndSessionTimeout()) return 1;
    if (!testRejectedResume()) return 1;
    std::cout << "Agent session tests passed\n";
    return 0;
}
