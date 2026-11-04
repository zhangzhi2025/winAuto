#pragma once

#include <string>

namespace wa::platform {

enum class CredentialError { None, NotFound, InvalidProvider, StorageFailure, ProtectionFailure };

struct CredentialResult {
    std::string value;
    CredentialError error = CredentialError::None;
    bool ok() const { return error == CredentialError::None; }
};

class ModelCredentialStore {
public:
    static CredentialError store(const std::string& provider, const std::string& secret);
    static CredentialResult load(const std::string& provider);
    static CredentialResult exists(const std::string& provider);
    static CredentialError clear(const std::string& provider);
};

}  // namespace wa::platform
