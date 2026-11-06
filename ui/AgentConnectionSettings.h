#pragma once

#include <QString>
#include <QUrl>

namespace wa::agent {

struct AgentConnectionSettings {
    QString webSocketUrl;
    QString workingDirectory;
    QString sessionId;
};

struct SettingsValidation {
    AgentConnectionSettings normalized;
    QString error;
    bool ok() const { return error.isEmpty(); }
};

class AgentSettingsStore {
public:
    static AgentConnectionSettings load();
    static bool save(const AgentConnectionSettings& settings, QString* error);
    static bool clear(QString* error);
};

SettingsValidation validateSettings(const AgentConnectionSettings& settings);
QString safeUrlDisplay(const QString& url);
QString redactSensitiveText(const QString& text, const QUrl& sourceUrl = {});

}  // namespace wa::agent
