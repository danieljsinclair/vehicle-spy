// TokenStore.cpp - Token persistence implementation

#include "TokenStore.h"
#include "WiFiManager.h"

namespace esp32_firmware {

TokenStore::TokenStore(IPreferences& prefs, std::string bakedDefault)
    : prefsStore_(prefs)
    , token_(std::move(bakedDefault))
    , bakedDefault_(token_) {
}

const std::string* TokenStore::load() {
    prefsStore_.begin(NVS_TOKEN_NAMESPACE, true);
    std::string value = prefsStore_.getString(NVS_TOKEN_KEY, "");
    prefsStore_.end();
    if (value.empty()) {
        lastLoadedPresent_ = false;
        return nullptr;
    }
    loadedValue_ = std::move(value);
    lastLoadedPresent_ = true;
    return &loadedValue_;
}

bool TokenStore::store(const std::string& token) {
    prefsStore_.begin(NVS_TOKEN_NAMESPACE, false);
    bool success = prefsStore_.putString(NVS_TOKEN_KEY, token) > 0;
    prefsStore_.end();
    if (success) {
        token_ = token;
        lastLoadedPresent_ = true;
    }
    return success;
}

std::string TokenStore::loadOrDefault() {
    auto ptr = load();
    token_ = ptr ? *ptr : bakedDefault_;
    return token_;
}

} // namespace esp32_firmware
