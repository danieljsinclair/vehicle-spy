// NvsWifiCredentialStore.cpp - Vanilla WiFi credential NVS write logic
// Extracted from can-bridge.ino for host testability

#include "NvsWifiCredentialStore.h"

namespace esp32_firmware {

namespace {
// NVS namespace and key constants (list-shaped schema: cred_count + indexed
// entry[0]). Kept in this translation unit only — callers use the
// storeWifiCredentials() function, not these constants directly.
static constexpr const char* NVS_WIFI_NAMESPACE = "wifi";
static constexpr const char* NVS_WIFI_CRED_COUNT = "cred_count";
static constexpr const char* NVS_WIFI_SSID      = "ssid_0";
static constexpr const char* NVS_WIFI_PASS      = "pass_0";
} // anonymous namespace

bool storeWifiCredentials(INvsWifiStore& store,
                          const std::string& ssid,
                          const std::string& pass) {
    store.begin(NVS_WIFI_NAMESPACE, false);  // read-write
    // Write cred_count first so hasStoredCredentials / determineCredentialSource
    // can gate on a single integer rather than per-key length probes.
    bool success = store.putString(NVS_WIFI_CRED_COUNT, "1") > 0;
    success = success && store.putString(NVS_WIFI_SSID, ssid) > 0;
    success = success && store.putString(NVS_WIFI_PASS, pass) > 0;
    store.end();
    return success;
}

} // namespace esp32_firmware
