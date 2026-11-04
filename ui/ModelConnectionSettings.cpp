#include "ModelConnectionSettings.h"

#include <QSettings>

#include "ModelCredentialStore.h"

namespace wa::llm {
namespace {

QSettings settings() {
    return QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("WinAuto"),
                     QStringLiteral("WinAuto"));
}

std::string providerKey(Provider provider) {
    return provider == Provider::Claude ? "claude" : "openai";
}

QString credentialError(wa::platform::CredentialError error) {
    switch (error) {
        case wa::platform::CredentialError::None: return {};
        case wa::platform::CredentialError::NotFound: return QStringLiteral("尚未配置访问凭据。");
        case wa::platform::CredentialError::InvalidProvider: return QStringLiteral("不支持的模型服务。");
        case wa::platform::CredentialError::ProtectionFailure: return QStringLiteral("Windows 无法保护或读取该凭据。");
        case wa::platform::CredentialError::StorageFailure: return QStringLiteral("无法访问凭据存储。");
    }
    return QStringLiteral("凭据存储发生错误。");
}

}  // namespace

ConnectionSettingsResult ModelConnectionSettings::load(Provider provider) {
    auto store = settings();
    const QString key = provider == Provider::Claude ? QStringLiteral("claude/model")
                                                     : QStringLiteral("openai/model");
    const QString model = store.value(key).toString().trimmed();
    return {model, !model.isEmpty(), {}};
}

bool ModelConnectionSettings::save(Provider provider, const QString& modelName, QString* error) {
    const QString model = modelName.trimmed();
    if (model.isEmpty() || model.size() > 128 || model.contains(QChar::Null)) {
        if (error) *error = QStringLiteral("模型名称必须为 1 至 128 个字符。");
        return false;
    }
    auto store = settings();
    store.setValue(provider == Provider::Claude ? QStringLiteral("claude/model") : QStringLiteral("openai/model"),
                   model);
    store.sync();
    if (store.status() != QSettings::NoError) {
        if (error) *error = QStringLiteral("无法保存模型连接设置。");
        return false;
    }
    if (error) error->clear();
    return true;
}

bool ModelConnectionSettings::clearCredential(Provider provider, QString* error) {
    const auto result = wa::platform::ModelCredentialStore::clear(providerKey(provider));
    if (result != wa::platform::CredentialError::None) {
        if (error) *error = credentialError(result);
        return false;
    }
    if (error) error->clear();
    return true;
}

bool ModelConnectionSettings::hasCredential(Provider provider) {
    return wa::platform::ModelCredentialStore::exists(providerKey(provider)).ok();
}

bool ModelConnectionSettings::saveCredential(Provider provider, const QString& secret, QString* error) {
    if (secret.isEmpty() || secret.size() > 1024) {
        if (error) *error = QStringLiteral("访问凭据长度无效。");
        return false;
    }
    for (const QChar character : secret) {
        const ushort value = character.unicode();
        if (value < 0x21 || value > 0x7e) {
            if (error) *error = QStringLiteral("访问凭据必须为不含空白和控制字符的可见 ASCII 文本。");
            return false;
        }
    }
    QByteArray utf8 = secret.toUtf8();
    std::string credential(utf8.constData(), static_cast<size_t>(utf8.size()));
    const auto result = wa::platform::ModelCredentialStore::store(providerKey(provider), credential);
    credential.assign(credential.size(), '\0');
    if (result != wa::platform::CredentialError::None) {
        utf8.fill('\0');
        if (error) *error = credentialError(result);
        return false;
    }
    utf8.fill('\0');
    if (error) error->clear();
    return true;
}

QString ModelConnectionSettings::takeCredential(Provider provider, QString* error) {
    auto result = wa::platform::ModelCredentialStore::load(providerKey(provider));
    if (!result.ok()) {
        if (error) *error = credentialError(result.error);
        return {};
    }
    QString secret = QString::fromUtf8(result.value.data(), static_cast<int>(result.value.size()));
    result.value.assign(result.value.size(), '\0');
    if (error) error->clear();
    return secret;
}

}  // namespace wa::llm
