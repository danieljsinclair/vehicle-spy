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
// 39 = WNM-SLEEP-MODE-REJECT in the generic 802.11 table, but on TP-Link Deco
// mesh APs it is observed as the code delivered when the mesh STEERS/disassociates
// a 2.4GHz-only client (e.g. an ESP32) between nodes it cannot follow, or rejects
// a reassociation while a stale association entry still exists for this MAC. It is
// intentionally NOT in the low (1-24) or high (200-204) families above, which is
// exactly why a bare "reason=39" in the serial trace was undecodable. Named here
// so the firmware can print a meaningful label instead of a mystery number.
constexpr int WIFI_REASON_MESH_STEER_REJECT = 39;

// Decode a wifi_err_reason_t value to its ESP-IDF name. Returns "UNKNOWN(N)" for
// any code not in the table (e.g. vendor/Deco-specific) so the trace is ALWAYS
// human-decodable rather than printing a bare number.
inline const char* wifiReasonName(int reason) {
    switch (reason) {
        case WIFI_REASON_UNSPECIFIED:           return "UNSPECIFIED";
        case WIFI_REASON_AUTH_EXPIRE:           return "AUTH_EXPIRE";
        case WIFI_REASON_AUTH_LEAVE:            return "AUTH_LEAVE";
        case WIFI_REASON_ASSOC_EXPIRE:          return "ASSOC_EXPIRE";
        case WIFI_REASON_ASSOC_TOOMANY:         return "ASSOC_TOOMANY";
        case WIFI_REASON_NOT_AUTHED:            return "NOT_AUTHED";
        case WIFI_REASON_NOT_ASSOCED:           return "NOT_ASSOCED";
        case WIFI_REASON_ASSOC_LEAVE:           return "ASSOC_LEAVE";
        case WIFI_REASON_ASSOC_NOT_AUTHED:      return "ASSOC_NOT_AUTHED";
        case WIFI_REASON_DISASSOC_PWRCAP_BAD:   return "DISASSOC_PWRCAP_BAD";
        case WIFI_REASON_DISASSOC_SUPCHAN_BAD:  return "DISASSOC_SUPCHAN_BAD";
        case WIFI_REASON_BSS_TRANSITION_DISASSOC: return "BSS_TRANSITION_DISASSOC";
        case WIFI_REASON_IE_INVALID:            return "IE_INVALID";
        case WIFI_REASON_MIC_FAILURE:           return "MIC_FAILURE";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4WAY_HANDSHAKE_TIMEOUT";
        case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT: return "GROUP_KEY_UPDATE_TIMEOUT";
        case WIFI_REASON_IE_IN_4WAY_DIFFERS:    return "IE_IN_4WAY_DIFFERS";
        case WIFI_REASON_GROUP_CIPHER_INVALID:  return "GROUP_CIPHER_INVALID";
        case WIFI_REASON_PAIRWISE_CIPHER_INVALID: return "PAIRWISE_CIPHER_INVALID";
        case WIFI_REASON_AKMP_INVALID:          return "AKMP_INVALID";
        case WIFI_REASON_UNSUPP_RSN_IE_VERSION: return "UNSUPP_RSN_IE_VERSION";
        case WIFI_REASON_INVALID_RSN_IE_CAP:    return "INVALID_RSN_IE_CAP";
        case WIFI_REASON_802_1X_AUTH_FAILED:    return "802_1X_AUTH_FAILED";
        case WIFI_REASON_CIPHER_SUITE_REJECTED: return "CIPHER_SUITE_REJECTED";
        case WIFI_REASON_BEACON_TIMEOUT:        return "BEACON_TIMEOUT";
        case WIFI_REASON_NO_AP_FOUND:           return "NO_AP_FOUND";
        case WIFI_REASON_AUTH_FAIL:             return "AUTH_FAIL";
        case WIFI_REASON_ASSOC_FAIL:            return "ASSOC_FAIL";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:     return "HANDSHAKE_TIMEOUT";
        case WIFI_REASON_MESH_STEER_REJECT:     return "MESH_STEER_REJECT";
        default:                                return "UNKNOWN";
    }
}

// Phase classification for a disconnect reason. Returns the connect-phase bucket
// (auth/assoc/handshake/link) so the serial trace labels at a glance WHICH LEG
// of the connect failed. OCP: adding a new reason code is just adding a row to
// this table; the call site never changes.
inline const char* wifiReasonPhase(int reason) {
    switch (reason) {
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_AUTH_LEAVE:
        case WIFI_REASON_NOT_AUTHED:
        case WIFI_REASON_NOT_ASSOCED:
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_802_1X_AUTH_FAILED:
            return "auth";
        case WIFI_REASON_ASSOC_EXPIRE:
        case WIFI_REASON_ASSOC_LEAVE:
        case WIFI_REASON_ASSOC_TOOMANY:
        case WIFI_REASON_ASSOC_NOT_AUTHED:
        case WIFI_REASON_ASSOC_FAIL:
        case WIFI_REASON_MESH_STEER_REJECT:
            return "assoc";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return "handshake";
        case WIFI_REASON_BEACON_TIMEOUT:
        case WIFI_REASON_NO_AP_FOUND:
            return "link";
        default:
            return "unknown";
    }
}

} // namespace esp32_firmware
