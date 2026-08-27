// TokenStore_test.cpp - Host tests for TokenStore (TDD)

#include "vanilla/TokenStore.h"
#include "TokenStore_test_fixture.h"
#include "vanilla/WiFiManager.h"
#include "mocks/PreferencesMock.h"

#include <gtest/gtest.h>
#include <string>

using esp32_firmware::ITokenStore;
using esp32_firmware::TokenStore;
using esp32_firmware::FakeTokenStore;

class PrefsAdapter : public esp32_firmware::IPreferences {
public:
    explicit PrefsAdapter(esp32_firmware::PreferencesMock& prefs) : prefs_(prefs) {}
    void begin(const char* name, bool readOnly) override { prefs_.begin(name, readOnly); }
    std::string getString(const char* key, const std::string& defaultValue) override {
        return prefs_.getString(key, defaultValue);
    }
    size_t getBytesLength(const char* key) override { return prefs_.getBytesLength(key); }
    size_t putString(const char* key, const std::string& value) override {
        return prefs_.putString(key, value);
    }
    void end() override { prefs_.end(); }
    void clear() override { prefs_.clear(); }
private:
    esp32_firmware::PreferencesMock& prefs_;
};

class FailingPrefsAdapter : public esp32_firmware::IPreferences {
public:
    explicit FailingPrefsAdapter(esp32_firmware::PreferencesMock& prefs) : prefs_(prefs) {}
    void begin(const char* name, bool readOnly) override { prefs_.begin(name, readOnly); }
    std::string getString(const char*, const std::string& defaultValue) override {
        return prefs_.getString("ignored", defaultValue);
    }
    size_t getBytesLength(const char*) override { return prefs_.getBytesLength("ignored"); }
    size_t putString(const char*, const std::string&) override { return 0; }
    void end() override { prefs_.end(); }
    void clear() override { prefs_.clear(); }
private:
    esp32_firmware::PreferencesMock& prefs_;
};

class FakeTokenStoreTest : public ::testing::Test {
protected:
    FakeTokenStore store;
};

TEST_F(FakeTokenStoreTest, LoadEmpty_ReturnsNullptr) {
    EXPECT_FALSE(store.load());
}

TEST_F(FakeTokenStoreTest, LoadStored_ReturnsPointerToToken) {
    store.store("my-secret-token");
    auto result = store.load();
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(*result, "my-secret-token");
}

TEST_F(FakeTokenStoreTest, StoreRoundtrip_LoadReturnsStoredValue) {
    store.store("roundtrip-token");
    auto result = store.load();
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(*result, "roundtrip-token");
}

TEST_F(FakeTokenStoreTest, IsEmpty_TrueBeforeStore_FalseAfterStore) {
    EXPECT_TRUE(store.isEmpty());
    store.store("token");
    EXPECT_FALSE(store.isEmpty());
}

TEST_F(FakeTokenStoreTest, Provider_ReturnsLiveValue) {
    store.store("first-token");
    auto provider = store.provider();
    EXPECT_EQ(provider(), "first-token");
    store.store("second-token");
    EXPECT_EQ(provider(), "second-token");
}

TEST_F(FakeTokenStoreTest, Provider_ReflectsStoreUpdateWithoutRebind) {
    store.store("initial-token");
    auto provider = store.provider();
    store.store("updated-token");
    EXPECT_EQ(provider(), "updated-token");
}

TEST_F(FakeTokenStoreTest, StoreCalls_Incremented) {
    EXPECT_EQ(store.storeCalls, 0);
    store.store("token-a");
    EXPECT_EQ(store.storeCalls, 1);
    store.store("token-b");
    EXPECT_EQ(store.storeCalls, 2);
}

TEST_F(FakeTokenStoreTest, Reset_ClearsState) {
    store.store("token");
    store.reset();
    EXPECT_TRUE(store.isEmpty());
    EXPECT_EQ(store.storeCalls, 0);
    EXPECT_FALSE(store.load());
}

class TokenStoreNvsTest : public ::testing::Test {
protected:
    esp32_firmware::PreferencesMock prefsMock;
    PrefsAdapter     prefsAdapter{prefsMock};
    TokenStore       store{prefsAdapter, "vehicle-sim-2026"};
};

TEST_F(TokenStoreNvsTest, LoadEmptyNvs_ReturnsNullptr) {
    EXPECT_FALSE(store.load());
}

TEST_F(TokenStoreNvsTest, LoadStored_ReturnsValueFromNvs) {
    prefsMock.setValue(TokenStore::NVS_TOKEN_NAMESPACE,
                       TokenStore::NVS_TOKEN_KEY, "stored-token");
    auto result = store.load();
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(*result, "stored-token");
}

TEST_F(TokenStoreNvsTest, StoreRoundtrip_LoadReturnsSameValue) {
    ASSERT_TRUE(store.store("roundtrip-token"));
    auto result = store.load();
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(*result, "roundtrip-token");
}

TEST_F(TokenStoreNvsTest, LoadOrDefault_EmptyNvs_ReturnsBakedDefault) {
    EXPECT_EQ(store.loadOrDefault(), "vehicle-sim-2026");
}

TEST_F(TokenStoreNvsTest, LoadOrDefault_StoredNvs_ReturnsStoredValue) {
    prefsMock.setValue(TokenStore::NVS_TOKEN_NAMESPACE,
                       TokenStore::NVS_TOKEN_KEY, "nvs-token");
    EXPECT_EQ(store.loadOrDefault(), "nvs-token");
}

TEST_F(TokenStoreNvsTest, IsEmpty_TrueWhenNvsEmpty) {
    EXPECT_TRUE(store.isEmpty());
}

TEST_F(TokenStoreNvsTest, IsEmpty_FalseAfterStore) {
    ASSERT_TRUE(store.store("token"));
    EXPECT_FALSE(store.isEmpty());
}

TEST_F(TokenStoreNvsTest, Provider_ReturnsCachedTokenAfterStore) {
    ASSERT_TRUE(store.store("provider-token"));
    auto provider = store.provider();
    EXPECT_EQ(provider(), "provider-token");
}

TEST_F(TokenStoreNvsTest, Provider_ReflectsLoadOrDefaultValue) {
    std::string chosen = store.loadOrDefault();
    if (chosen != "vehicle-sim-2026") { store.store(chosen); }
    EXPECT_EQ(store.provider()(), "vehicle-sim-2026");
}

TEST_F(TokenStoreNvsTest, StoreFailure_DoesNotCorruptCache) {
    store.loadOrDefault();
    FailingPrefsAdapter failingPrefs{prefsMock};
    TokenStore failingStore{failingPrefs, "vehicle-sim-2026"};
    EXPECT_FALSE(failingStore.store("should-fail"));
    EXPECT_EQ(failingStore.cachedToken(), "vehicle-sim-2026");
}
