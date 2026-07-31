// NvsWifiCredentialStore.cpp - Vanilla WiFi credential NVS write logic
// Extracted from can-bridge.ino for host testability

#include "NvsWifiCredentialStore.h"

namespace esp32_firmware {

namespace {
// NVS namespace and key constants (mirrors the inline definitions formerly
// in can-bridge.ino). Kept in this translation unit only — callers use the
// storeWifiCredentials() function, not these constants directly.
static constexpr const char* NVS_WIFI_NAMESPACE = "wifi";
static constexpr const char* NVS_WIFI_SSID      = "ssid";
static constexpr const char* NVS_WIFI_PASS      = "pass";
} // anonymous namespace

bool storeWifiCredentials(INvsWifiStore& store,
                          const std::string& ssid,
                          const std::string& pass) {
    store.begin(NVS_WIFI_NAMESPACE, false);  // read-write
    bool success = store.putString(NVS_WIFI_SSID, ssid) > 0;
    success = success && store.putString(NVS_WIFI_PASS, pass) > 0;
    store.end();
    return success;
}

} // namespace esp32_firmware
