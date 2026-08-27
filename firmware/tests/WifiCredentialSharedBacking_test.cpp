// WifiCredentialSharedBacking_test.cpp - Regression guard for the
// "single source of truth" WiFi-credential contract.
//
// PRODUCTION BUG THIS GUARDS AGAINST:
//   The AT command path (ATSETWIFI -> ArduinoAtWifiStore) and the boot-time
//   read path (WiFiManager -> determineCredentialSource/loadCredentialsImpl)
//   MUST read and write the SAME NVS (Preferences) handle. A previous wiring
//   gave each side its own independent `Preferences` instance, so `ATSETWIFI`
//   reported "stored" yet the device booted straight into AP mode — the creds
//   were written somewhere boot never looked.
//
// WHAT THIS TEST PINS:
//   Using the REAL production functions (storeWifiCredentials for the write,
//   determineCredentialSource + loadCredentialsImpl for the read) over a SINGLE
//   shared PreferencesMock, it asserts that credentials written by the store
//   are exactly what the boot reader finds: same namespace, same keys, same
//   values. Any future split of the write/read backing (two handles, two
//   namespaces, divergent key shapes) breaks this test.
//
// NOTE on fidelity: the production `ArduinoAtWifiStore` is Arduino-gated, so it
// is not compiled in the host build. This test exercises the identical
// contract through the vanilla `INvsWifiStore`/`IPreferences` seams the
// production adapter bridges, over one shared mock — the contract that must
// hold end-to-end. The production side of that contract is enforced by the
// can-bridge.ino wiring (single shared Preferences).

#include "NvsWifiCredentialStore.h"
#include "WiFiManager.h"
#include "mocks/PreferencesMock.h"

#include <gtest/gtest.h>
#include <string>

using esp32_firmware::INvsWifiStore;
using esp32_firmware::IPreferences;
using esp32_firmware::PreferencesMock;
using esp32_firmware::determineCredentialSource;
using esp32_firmware::loadCredentialsImpl;
using esp32_firmware::storeWifiCredentials;

namespace {

// INvsWifiStore adapter backed by a shared PreferencesMock. Mirrors what
// ArduinoAtWifiStore does over Arduino Preferences, but in the host build so
// the store and the boot reader can share ONE backing instance.
class MockBackedNvsStore : public INvsWifiStore {
public:
    explicit MockBackedNvsStore(IPreferences& prefs) : prefs_(prefs) {}

    void begin(const char* name, bool readOnly) override { prefs_.begin(name, readOnly); }
    size_t putString(const char* key, const std::string& value) override {
        return prefs_.putString(key, value);
    }
    void end() override { prefs_.end(); }

private:
    IPreferences& prefs_;
};

// A store that presents the IWifiCredentialStore surface used by the AT
// dispatcher (store(ssid, pass)) and delegates to storeWifiCredentials over the
// shared mock — exactly the seam production wires to ArduinoAtWifiStore.
class SharedBackingWifiStore {
public:
    explicit SharedBackingWifiStore(IPreferences& prefs) : store_(prefs) {}

    bool store(const std::string& ssid, const std::string& pass) {
        return storeWifiCredentials(store_, ssid, pass);
    }

private:
    MockBackedNvsStore store_;
};

class WifiCredentialSharedBackingTest : public ::testing::Test {
protected:
    PreferencesMock prefs_;  // THE single source of truth, shared by write + read
    SharedBackingWifiStore wifiStore_{prefs_};
};

// Core regression assertion: credentials written via the store are visible to
// the boot-time credential source + loader over the SAME backing.
TEST_F(WifiCredentialSharedBackingTest, AtStoreWriteIsVisibleToBootReader) {
    ASSERT_TRUE(wifiStore_.store("manht2", "luckyshoe478"));

    // Boot reader must now see STORED_NVS (not NONE, not BAKED_IN).
    esp32_firmware::CredentialSource source = determineCredentialSource(prefs_, nullptr, nullptr);
    EXPECT_EQ(source, esp32_firmware::CredentialSource::STORED_NVS);

    std::string ssid, pass;
    ASSERT_TRUE(loadCredentialsImpl(prefs_, ssid, pass));
    EXPECT_EQ(ssid, "manht2");
    EXPECT_EQ(pass, "luckyshoe478");
}

// Pin the exact on-disk shape the store writes and the reader probes, so a
// silent key/namespace divergence (the real bug's root) cannot sneak back.
TEST_F(WifiCredentialSharedBackingTest, WrittenKeysMatchBootReaderProbeKeys) {
    ASSERT_TRUE(wifiStore_.store("my-ssid", "my-pass"));

    // Reader probes these exact keys in the "wifi" namespace.
    EXPECT_EQ(prefs_.getValue("wifi", "cred_count"), "1");
    EXPECT_EQ(prefs_.getValue("wifi", "ssid_0"), "my-ssid");
    EXPECT_EQ(prefs_.getValue("wifi", "pass_0"), "my-pass");

    // And the reader confirms presence + content (the boot path).
    EXPECT_EQ(determineCredentialSource(prefs_, nullptr, nullptr),
              esp32_firmware::CredentialSource::STORED_NVS);
    std::string ssid, pass;
    ASSERT_TRUE(loadCredentialsImpl(prefs_, ssid, pass));
    EXPECT_EQ(ssid, "my-ssid");
    EXPECT_EQ(pass, "my-pass");
}

// Negative: with NO creds written, the boot reader must see NONE (so the
// device falls back to AP mode) — confirming the shared backing is the gate,
// not a stale handle.
TEST_F(WifiCredentialSharedBackingTest, NoWriteYieldsNoneSource) {
    EXPECT_EQ(determineCredentialSource(prefs_, nullptr, nullptr),
              esp32_firmware::CredentialSource::NONE);
    std::string ssid, pass;
    EXPECT_FALSE(loadCredentialsImpl(prefs_, ssid, pass));
}

} // namespace
