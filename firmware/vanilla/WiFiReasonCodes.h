#pragma once

// WiFiReasonCodes.h - ESP32 WiFi disconnect reason codes
// Extracted from ESP-IDF for host testing compatibility

namespace esp32_firmware {

// WiFi disconnect reason codes (from ESP-IDF esp_wifi.h)
// These are used in WiFiManager::onDisconnected to detect auth failures
constexpr int WIFI_REASON_UNSPECIFIED = 0;
constexpr int WIFI_REASON_AUTH_EXPIRE = 1;
constexpr int WIFI_REASON_AUTH_LEAVE = 2;
constexpr int WIFI_REASON_ASSOC_EXPIRE = 3;
constexpr int WIFI_REASON_ASSOC_TOOMANY = 4;
constexpr int WIFI_REASON_NOT_AUTHED = 5;
constexpr int WIFI_REASON_NOT_ASSOCED = 6;
constexpr int WIFI_REASON_ASSOC_LEAVE = 7;
constexpr int WIFI_REASON_ASSOC_NOT_AUTHED = 8;
constexpr int WIFI_REASON_DISASSOC_PWRCAP_BAD = 9;
constexpr int WIFI_REASON_DISASSOC_SUPCHAN_BAD = 10;
constexpr int WIFI_REASON_IE_INVALID = 11;
constexpr int WIFI_REASON_MIC_FAILURE = 12;
constexpr int WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT = 13;
constexpr int WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT = 14;
constexpr int WIFI_REASON_IE_IN_4WAY_DIFFERS = 15;
constexpr int WIFI_REASON_GROUP_CIPHER_INVALID = 16;
constexpr int WIFI_REASON_PAIRWISE_CIPHER_INVALID = 17;
constexpr int WIFI_REASON_AKMP_INVALID = 18;
constexpr int WIFI_REASON_UNSUPP_RSN_IE_VERSION = 19;
constexpr int WIFI_REASON_INVALID_RSN_IE_CAP = 20;
constexpr int WIFI_REASON_802_1X_AUTH_FAILED = 21;
constexpr int WIFI_REASON_CIPHER_SUITE_REJECTED = 22;
constexpr int WIFI_REASON_BEACON_TIMEOUT = 200;
constexpr int WIFI_REASON_NO_AP_FOUND = 201;
constexpr int WIFI_REASON_AUTH_FAIL = 202;
constexpr int WIFI_REASON_ASSOC_FAIL = 203;
constexpr int WIFI_REASON_HANDSHAKE_TIMEOUT = 204;

} // namespace esp32_firmware
