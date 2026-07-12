#ifndef FIRMWARE_TCP_SERVER_MANAGER_H
#define FIRMWARE_TCP_SERVER_MANAGER_H

#include "ITcpServer.h"
#include "WiFiManager.h"  // For esp32_firmware::IStatusLED + WiFiState::State

#include <cstdint>
#include <memory>
#include <string>

namespace esp32_firmware {

// ── TcpServerManager — vanilla TCP accept/auth/dispatch state machine ────────
// Owns the per-tick TCP server lifecycle that previously lived inline in
// can-bridge.ino loop() (the accept → AUTH → command-dispatch → disconnect
// sequence). Hardware side effects (WiFiServer begin/end on IP change) stay
// in the .ino via restartTcpServerIfNeeded(); this component drives only the
// client-facing half through the ITcpServer/ITcpServerClient abstractions.
//
// SRP: this component manages ONE client connection's lifecycle. It does not
// own the listening socket's bring-up/tear-down (that's an IP-change hardware
// effect, owned by the .ino), nor command semantics (delegated to
// ITcpHostCallbacks.handleTcpAtCommand → FirmwareApp's dispatcher).
//
// Dependency-injected for host-native testability:
//   - server_   : ITcpServer (WiFiServer adapter / mock)
//   - statusLed_: IStatusLED  (pattern transitions on connect/disconnect)
//   - authToken_: build-time AUTH token ("vehicle-sim-2026")
//   - host_     : ITcpHostCallbacks (narrow FirmwareApp delegation)
class TcpServerManager {
public:
    TcpServerManager(ITcpServer& server,
                     IStatusLED& statusLed,
                     const std::string& authToken,
                     ITcpHostCallbacks& host);

    // ── Pure helper: validate a received AUTH line ────────────────────────────
    // Expected format: "AUTH <token>". Constant-time semantics are NOT promised
    // (the token is a shared build-time secret, not a per-user credential);
    // equality is a plain prefix+token compare. Static so it can be tested in
    // isolation without constructing the manager.
    static bool isValidAuthToken(const std::string& received,
                                 const std::string& authToken);

    // ── Per-tick state-machine step ──────────────────────────────────────────
    // Drives one iteration of the client-facing TCP lifecycle:
    //   1. Probe accept(); if a new client arrives, stop() any existing one,
    //      adopt the new client, clear the monitor flag, set the AUTH read
    //      timeout, read+trim the first line and validate it:
    //        - valid   → println "OK" + flush + setPattern(CLIENT_CONNECTED);
    //        - invalid → println "ERROR unauthorized" + flush + stop + drop.
    //   2. Otherwise, if the adopted client is connected and a command is
    //      available, set the COMMAND read timeout, read+trim the line and
    //      forward non-empty commands to host_.handleTcpAtCommand.
    //   3. Otherwise, if the adopted client has dropped, run disconnect
    //      cleanup: setMonitorActive(false) + resetDiscoveryBackoff(), and
    //      revert the LED to WIFI_CONNECTED when WiFi is CONNECTED_STA/AP
    //      (no revert when DISCONNECTED).
    //
    // nowMs is the current tick (millis()); accepted for clock-injection but
    // not currently read by the body (kept in the signature so the contract
    // admits time-based extensions without an ABI churn).
    void cycle(uint32_t nowMs);

    // ── Lifecycle hooks ──────────────────────────────────────────────────────
    // start()/stop() are reserved for setup/shutdown + IP-change bring-down.
    // Currently no-op: the listening socket's begin/end are hardware side
    // effects that stay in the .ino (restartTcpServerIfNeeded).
    void start();
    void stop();

private:
    ITcpServer& server_;
    IStatusLED& statusLed_;
    const std::string authToken_;
    ITcpHostCallbacks& host_;

    // The single adopted client (nullptr when none / after reject or drop).
    std::unique_ptr<ITcpServerClient> current_;
};

} // namespace esp32_firmware

#endif // FIRMWARE_TCP_SERVER_MANAGER_H
