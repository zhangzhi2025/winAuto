#include "AgentConnectionSettings.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QUrlQuery>

namespace wa::agent {
namespace {

constexpr int kMaxSessionIdLength = 512;

QSettings settingsStore() {
    return QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("WinAuto"),
                     QStringLiteral("WinAuto"));
}

bool validDirectory(const QString& directory, QString* canonical) {
    const QFileInfo info(directory);
    if (!info.exists() || !info.isDir() || !info.isReadable()) return false;
    const QString path = info.canonicalFilePath();
    if (path.isEmpty()) return false;
    QDir dir(path);
    if (!dir.exists()) return false;
    if (canonical) *canonical = QDir::cleanPath(path);
    return true;
}

QString settingsKey(const QString& name) { return QStringLiteral("agent/") + name; }

}  // namespace

AgentConnectionSettings AgentSettingsStore::load() {
    auto store = settingsStore();
    return {store.value(settingsKey(QStringLiteral("webSocketUrl"))).toString(),
            store.value(settingsKey(QStringLiteral("workingDirectory"))).toString(),
            store.value(settingsKey(QStringLiteral("sessionId"))).toString()};
}

bool AgentSettingsStore::save(const AgentConnectionSettings& value, QString* error) {
    const SettingsValidation validation = validateSettings(value);
    if (!validation.ok()) {
        if (error) *error = validation.error;
        return false;
    }
    auto store = settingsStore();
    store.setValue(settingsKey(QStringLiteral("webSocketUrl")), validation.normalized.webSocketUrl);
    store.setValue(settingsKey(QStringLiteral("workingDirectory")), validation.normalized.workingDirectory);
    store.setValue(settingsKey(QStringLiteral("sessionId")), validation.normalized.sessionId);
    store.sync();
    if (store.status() != QSettings::NoError) {
        if (error) *error = QStringLiteral("无法保存 Agent 连接设置。");
        return false;
    }
    if (error) error->clear();
    return true;
}

bool AgentSettingsStore::clear(QString* error) {
    auto store = settingsStore();
    store.remove(QStringLiteral("agent"));
    store.sync();
    if (store.status() != QSettings::NoError) {
        if (error) *error = QStringLiteral("无法清除 Agent 连接设置。");
        return false;
    }
    if (error) error->clear();
    return true;
}

SettingsValidation validateSettings(const AgentConnectionSettings& settings) {
    SettingsValidation result;
    result.normalized = settings;

    QUrl url(settings.webSocketUrl.trimmed());
    if (!url.isValid() || (url.scheme() != QLatin1String("ws") && url.scheme() != QLatin1String("wss")) ||
        url.host().isEmpty()) {
        result.error = QStringLiteral("WebSocket 地址必须是包含主机名的 ws:// 或 wss:// 地址。");
        return result;
    }
    if (!url.userName().isEmpty() || !url.password().isEmpty()) {
        result.error = QStringLiteral("WebSocket 地址不得在 URL 中包含用户名或密码。");
        return result;
    }
    if (url.port(-1) == 0 || url.port(-1) > 65535) {
        result.error = QStringLiteral("WebSocket 地址端口无效。");
        return result;
    }
    url.setFragment(QString());
    result.normalized.webSocketUrl = url.toString(QUrl::FullyEncoded);

    QString canonical;
    if (!validDirectory(settings.workingDirectory.trimmed(), &canonical)) {
        result.error = QStringLiteral("项目目录不存在、不是可访问的目录，或无法规范化。");
        return result;
    }
    result.normalized.workingDirectory = canonical;
    result.normalized.sessionId = settings.sessionId.trimmed();
    if (result.normalized.sessionId.size() > kMaxSessionIdLength ||
        result.normalized.sessionId.contains(QChar::Null)) {
        result.error = QStringLiteral("Session ID 长度无效。");
        return result;
    }
    return result;
}

QString safeUrlDisplay(const QString& value) {
    QUrl url(value);
    if (!url.isValid()) return QStringLiteral("<invalid WebSocket URL>");
    url.setUserName(QString());
    url.setPassword(QString());
    url.setFragment(QString());
    QUrlQuery query(url);
    const QStringList sensitiveKeys{QStringLiteral("token"), QStringLiteral("access_token"),
                                    QStringLiteral("api_key"), QStringLiteral("apikey"),
                                    QStringLiteral("key"), QStringLiteral("secret"),
                                    QStringLiteral("authorization"), QStringLiteral("password")};
    const auto items = query.queryItems(QUrl::FullyDecoded);
    query.clear();
    for (const auto& item : items) {
        const QString key = item.first;
        const QString normalized = key.toLower();
        const bool sensitive = sensitiveKeys.contains(normalized) || normalized.endsWith(QStringLiteral("_token")) ||
                               normalized.endsWith(QStringLiteral("_key"));
        query.addQueryItem(key, sensitive ? QStringLiteral("<redacted>") : item.second);
    }
    url.setQuery(query);
    return url.toString(QUrl::RemovePassword | QUrl::RemoveUserInfo | QUrl::FullyEncoded);
}

QString redactSensitiveText(const QString& text, const QUrl& sourceUrl) {
    QString safe = text.left(2048);
    if (sourceUrl.isValid()) {
        const QString original = sourceUrl.toString(QUrl::FullyEncoded);
        const QString sanitized = safeUrlDisplay(original);
        if (!original.isEmpty()) safe.replace(original, sanitized, Qt::CaseInsensitive);
        if (!sourceUrl.password().isEmpty()) safe.replace(sourceUrl.password(), QStringLiteral("<redacted>"));
        if (!sourceUrl.userName().isEmpty()) safe.replace(sourceUrl.userName(), QStringLiteral("<redacted>"));
    }
    return safe;
}

}  // namespace wa::agent
