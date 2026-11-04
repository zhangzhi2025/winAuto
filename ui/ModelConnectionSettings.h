#pragma once

#include <QString>

#include "WorkflowGeneration.h"

namespace wa::llm {

struct ConnectionSettingsResult {
    QString modelName;
    bool configured = false;
    QString error;
};

class ModelConnectionSettings {
public:
    static ConnectionSettingsResult load(Provider provider);
    static bool save(Provider provider, const QString& modelName, QString* error);
    static bool hasCredential(Provider provider);
    static bool clearCredential(Provider provider, QString* error);
    static bool saveCredential(Provider provider, const QString& secret, QString* error);
    static QString takeCredential(Provider provider, QString* error);
};

}  // namespace wa::llm
