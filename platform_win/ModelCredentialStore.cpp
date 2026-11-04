#include "ModelCredentialStore.h"

#include <windows.h>
#include <dpapi.h>

#include <utility>
#include <vector>

namespace wa::platform {
namespace {

constexpr wchar_t kRegistryPath[] = L"Software\\WinAuto\\ModelCredentials";
constexpr DWORD kMaxCredentialBlobBytes = 16 * 1024;

bool validProvider(const std::string& provider) {
    return provider == "claude" || provider == "openai";
}

std::wstring registryValueName(const std::string& provider) {
    return provider == "claude" ? L"claude" : L"openai";
}

}  // namespace

CredentialError ModelCredentialStore::store(const std::string& provider, const std::string& secret) {
    if (!validProvider(provider)) return CredentialError::InvalidProvider;
    if (secret.empty()) return clear(provider);
    if (secret.size() > MAXDWORD) return CredentialError::ProtectionFailure;

    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(secret.data()));
    input.cbData = static_cast<DWORD>(secret.size());
    DATA_BLOB encrypted{};
    if (!CryptProtectData(&input, L"WinAuto model credential", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &encrypted)) {
        return CredentialError::ProtectionFailure;
    }

    HKEY key = nullptr;
    const LONG openResult = RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, nullptr, 0,
                                             KEY_SET_VALUE, nullptr, &key, nullptr);
    if (openResult != ERROR_SUCCESS) {
        LocalFree(encrypted.pbData);
        return CredentialError::StorageFailure;
    }
    const LONG writeResult = RegSetValueExW(key, registryValueName(provider).c_str(), 0, REG_BINARY,
                                             encrypted.pbData, encrypted.cbData);
    RegCloseKey(key);
    LocalFree(encrypted.pbData);
    return writeResult == ERROR_SUCCESS ? CredentialError::None : CredentialError::StorageFailure;
}

CredentialResult ModelCredentialStore::load(const std::string& provider) {
    if (!validProvider(provider)) return {{}, CredentialError::InvalidProvider};

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return {{}, CredentialError::NotFound};
    }
    DWORD type = 0;
    DWORD size = 0;
    LONG result = RegQueryValueExW(key, registryValueName(provider).c_str(), nullptr, &type, nullptr, &size);
    if (result != ERROR_SUCCESS || type != REG_BINARY || size == 0) {
        RegCloseKey(key);
        return {{}, result == ERROR_FILE_NOT_FOUND ? CredentialError::NotFound : CredentialError::StorageFailure};
    }
    if (size > kMaxCredentialBlobBytes) {
        RegCloseKey(key);
        return {{}, CredentialError::StorageFailure};
    }
    std::vector<BYTE> encrypted(size);
    result = RegQueryValueExW(key, registryValueName(provider).c_str(), nullptr, &type, encrypted.data(), &size);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || type != REG_BINARY) return {{}, CredentialError::StorageFailure};

    DATA_BLOB input{};
    input.pbData = encrypted.data();
    input.cbData = size;
    DATA_BLOB decrypted{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &decrypted)) {
        return {{}, CredentialError::ProtectionFailure};
    }
    std::string secret(reinterpret_cast<const char*>(decrypted.pbData), decrypted.cbData);
    SecureZeroMemory(decrypted.pbData, decrypted.cbData);
    LocalFree(decrypted.pbData);
    return {std::move(secret), CredentialError::None};
}

CredentialResult ModelCredentialStore::exists(const std::string& provider) {
    if (!validProvider(provider)) return {{}, CredentialError::InvalidProvider};
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return {{}, CredentialError::NotFound};
    }
    DWORD type = 0;
    DWORD size = 0;
    const LONG result = RegQueryValueExW(key, registryValueName(provider).c_str(), nullptr, &type, nullptr, &size);
    RegCloseKey(key);
    if (result == ERROR_FILE_NOT_FOUND) return {{}, CredentialError::NotFound};
    if (result != ERROR_SUCCESS || type != REG_BINARY || size == 0 || size > kMaxCredentialBlobBytes) {
        return {{}, CredentialError::StorageFailure};
    }
    return {{}, CredentialError::None};
}

CredentialError ModelCredentialStore::clear(const std::string& provider) {
    if (!validProvider(provider)) return CredentialError::InvalidProvider;
    HKEY key = nullptr;
    const LONG openResult = RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_SET_VALUE, &key);
    if (openResult == ERROR_FILE_NOT_FOUND) return CredentialError::None;
    if (openResult != ERROR_SUCCESS) return CredentialError::StorageFailure;
    const LONG deleteResult = RegDeleteValueW(key, registryValueName(provider).c_str());
    RegCloseKey(key);
    return deleteResult == ERROR_SUCCESS || deleteResult == ERROR_FILE_NOT_FOUND ? CredentialError::None
                                                                                  : CredentialError::StorageFailure;
}

}  // namespace wa::platform
