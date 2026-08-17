#pragma once

// WifiTransitionObserver.h - [EVENT] emission policy over WiFi state transitions
//
// A listener over the state model: the WiFi state machine owns state; this
// observer presents transitions as serial [EVENT] lines (wifi_connected /
// wifi_drop / wifi_ap_fallback). FirmwareApp::update() feeds it the new state
// each tick; it owns the previous-state memory and the last drop-reason memory,
// and decides which events fire.
//
// Header-only (header-inline trivial logic): no .cpp, no CMake change.
// Pure logic — no Arduino/ESP32 dependencies, host-testable standalone.

#include <string>
#include "WiFiManager.h"

namespace esp32_firmware {

// Sink for the events the observer produces. FirmwareApp implements this by
// forwarding to its IEventLogger (keeping the observer free of any logger
// dependency — dependency inversion at the observer's own boundary).
struct ITransitionEventSink {
    virtual void onTransitionEvent(const char* eventType, const std::string& detail) = 0;
    virtual ~ITransitionEventSink() = default;
};

class WifiTransitionObserver {
public:
    explicit WifiTransitionObserver(ITransitionEventSink& sink)
        : sink_(sink) {}

    // Seed the previous-state memory (called after init() so the first update()
    // tick reports transitions relative to the post-init state).
    void setInitialState(int state) { previousState_ = state; }

    // Record the reason of the most recent disconnect event (called from
    // FirmwareApp::onWiFiDisconnected). Reported by the wifi_drop transition
    // event and cleared on the next successful connect.
    void recordDisconnectReason(int reason) { lastDisconnectReason_ = reason; }

    // Observe the (possibly unchanged) state for this tick and emit the
    // transition events the change warrants. Emits at most: wifi_connected /
    // wifi_drop (a CONNECTED exit) and wifi_ap_fallback (an AP entry).
    void observe(int currentState, const std::string& localIp, int escalatedToApReason) {
        if (currentState == previousState_) {
            return;  // no transition — nothing to present
        }

        if (currentState == static_cast<int>(WiFiState::State::WIFI_CONNECTED)) {
            // WiFi just connected: report the address we came up on. A live
            // connection invalidates any earlier drop reason.
            sink_.onTransitionEvent("wifi_connected", "ip=" + localIp);
            lastDisconnectReason_ = 0;
        } else if (previousState_ == static_cast<int>(WiFiState::State::WIFI_CONNECTED)) {
            // Dropped out of a live connection: report the last known reason.
            sink_.onTransitionEvent("wifi_drop",
                                    "reason=" + std::to_string(lastDisconnectReason_));
        }

        if (WiFiState::isApModeState(static_cast<WiFiState::State>(currentState))
            && !WiFiState::isApModeState(static_cast<WiFiState::State>(previousState_))) {
            // Entered an AP state (first-class reason recorded by the manager).
            sink_.onTransitionEvent("wifi_ap_fallback",
                                    "reason=" + std::to_string(escalatedToApReason));
        }

        previousState_ = currentState;
    }

private:
    ITransitionEventSink& sink_;
    int previousState_ = static_cast<int>(WiFiState::State::WIFI_DISCONNECTED);
    int lastDisconnectReason_ = 0;
};

} // namespace esp32_firmware
