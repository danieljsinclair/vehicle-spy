// TokenStore_test_fixture.h - Test-only FakeTokenStore
// Separated from production TokenStore.h to keep production headers free of test code.

#pragma once

#include "TokenStore.h"

namespace esp32_firmware {

class FakeTokenStore : public ITokenStore {
public:
    const std::string* load() override { return hasToken_ ? &token_ : nullptr; }
    bool store(const std::string& token) override {
        ++storeCalls; token_ = token; hasToken_ = true; return true;
    }
    bool isEmpty() const override { return !hasToken_; }

    void reset() { hasToken_ = false; storeCalls = 0; }
    const std::string& stored() const { return token_; }
    std::function<const std::string&()> provider() const {
        return [this]() -> const std::string& { return token_; };
    }
    int storeCalls = 0;

private:
    bool hasToken_ = false;
    std::string token_;
};

} // namespace esp32_firmware
