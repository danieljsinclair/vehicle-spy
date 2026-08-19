// TokenStore.h - Vanilla TCP auth token persistence
// Extracted from FirmwareApp / can-bridge.ino for SRP + testability

#pragma once

#include <string>
#include <functional>

namespace esp32_firmware {

struct IPreferences;

struct ITokenStore {
    virtual ~ITokenStore() = default;
    virtual const std::string* load() = 0;
    virtual bool store(const std::string& token) = 0;
    virtual bool isEmpty() const = 0;
};

class TokenStore : public ITokenStore {
public:
    TokenStore(IPreferences& prefs, std::string bakedDefault);

    const std::string* load() override;
    bool store(const std::string& token) override;
    bool isEmpty() const override { return !lastLoadedPresent_; }
    std::string loadOrDefault();

    std::function<const std::string&()> provider() const {
        return [this]() -> const std::string& { return token_; };
    }

    const std::string& cachedToken() const { return token_; }
    bool lastLoadWasPresent() const { return lastLoadedPresent_; }

    static constexpr const char* NVS_TOKEN_NAMESPACE = "auth";
    static constexpr const char* NVS_TOKEN_KEY        = "token";

private:
    IPreferences& prefsStore_;
    std::string   token_;
    std::string   bakedDefault_;
    bool          lastLoadedPresent_ = false;
    std::string   loadedValue_;
};

} // namespace esp32_firmware
