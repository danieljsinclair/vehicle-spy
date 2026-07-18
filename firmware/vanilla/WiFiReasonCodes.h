#pragma once

// WiFiReasonCodes.h - ESP32 WiFi disconnect reason codes
// Extracted from ESP-IDF for host testing compatibility

namespace esp32_firmware {

// WiFi disconnect reason codes — MUST match the upstream ESP-IDF
// `wifi_err_reason_t` enum exactly. `info.wifi_sta_disconnected.reason`
// (can-bridge.ino:537) carries this RAW value, so any divergence here is a
// silent misclassification bug: a `reason == WIFI_REASON_*` comparison that
// disagrees with the hardware-delivered code never fires. The previous values
// were off-by-one for codes 1–8 and 13/21/22 (e.g. header said AUTH_EXPIRE=1
// but real hardware delivers 2) — corrected to the true ESP-IDF numbering.
constexpr int WIFI_REASON_UNSPECIFIED = 1;
constexpr int WIFI_REASON_AUTH_EXPIRE = 2;
constexpr int WIFI_REASON_AUTH_LEAVE = 3;
constexpr int WIFI_REASON_ASSOC_EXPIRE = 4;
constexpr int WIFI_REASON_ASSOC_TOOMANY = 5;
constexpr int WIFI_REASON_NOT_AUTHED = 6;
constexpr int WIFI_REASON_NOT_ASSOCED = 7;
constexpr int WIFI_REASON_ASSOC_LEAVE = 8;
constexpr int WIFI_REASON_ASSOC_NOT_AUTHED = 9;
constexpr int WIFI_REASON_DISASSOC_PWRCAP_BAD = 10;
constexpr int WIFI_REASON_DISASSOC_SUPCHAN_BAD = 11;
constexpr int WIFI_REASON_BSS_TRANSITION_DISASSOC = 12;
constexpr int WIFI_REASON_IE_INVALID = 13;
constexpr int WIFI_REASON_MIC_FAILURE = 14;
constexpr int WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT = 15;
constexpr int WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT = 16;
constexpr int WIFI_REASON_IE_IN_4WAY_DIFFERS = 17;
constexpr int WIFI_REASON_GROUP_CIPHER_INVALID = 18;
constexpr int WIFI_REASON_PAIRWISE_CIPHER_INVALID = 19;
constexpr int WIFI_REASON_AKMP_INVALID = 20;
constexpr int WIFI_REASON_UNSUPP_RSN_IE_VERSION = 21;
constexpr int WIFI_REASON_INVALID_RSN_IE_CAP = 22;
constexpr int WIFI_REASON_802_1X_AUTH_FAILED = 23;
constexpr int WIFI_REASON_CIPHER_SUITE_REJECTED = 24;
constexpr int WIFI_REASON_BEACON_TIMEOUT = 200;
constexpr int WIFI_REASON_NO_AP_FOUND = 201;
constexpr int WIFI_REASON_AUTH_FAIL = 202;
constexpr int WIFI_REASON_ASSOC_FAIL = 203;
constexpr int WIFI_REASON_HANDSHAKE_TIMEOUT = 204;

} // namespace esp32_firmware
