#include "TcpServerManager.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <string>

namespace esp32_firmware {
namespace {

// ── Per-phase read timeouts (ms). Mirror can-bridge.ino Constants so the
// extracted behaviour matches the inline loop exactly. Local to this TU so
// the manager owns no .ino dependency.
constexpr uint32_t TCP_AUTH_TIMEOUT_MS    = 5000;
constexpr uint32_t TCP_COMMAND_TIMEOUT_MS = 100;   // command-phase read timeout (ms)

// Trim leading/trailing ASCII whitespace from a line read off the wire.
// Mirrors Arduino String::trim() (space, \t, \r, \n, \f, \v). Applied before
// isValidAuthToken (AUTH) and before handleTcpAtCommand (commands) so trailing
// CR/whitespace from the wire never corrupts matching or dispatch.
std::string trim(const std::string& s) {
    const auto isWs = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
    };
    const size_t begin = (s.empty() || !isWs(static_cast<unsigned char>(s.front())))
                             ? 0
                             : s.find_first_not_of(" \t\r\n\f\v");
    if (begin == std::string::npos) {
        return {};
    }
    const size_t end = (s.empty() || !isWs(static_cast<unsigned char>(s.back())))
                           ? s.size()
                           : s.find_last_not_of(" \t\r\n\f\v") + 1;
    return s.substr(begin, end - begin);
}

} // namespace

TcpServerManager::TcpServerManager(ITcpServer& server,
                                   TokenProvider tokenProvider,
                                   ITcpHostCallbacks& host)
    : server_(server)
    , tokenProvider_(std::move(tokenProvider))
    , host_(host) {
}

// Pure helper — REAL implementation (no state, no I/O, fully decided).
// Mirrors the original inline isValidAuthToken: expected = "AUTH " + token,
// exact full-string match.
bool TcpServerManager::isValidAuthToken(const std::string& received,
                                        const std::string& authToken) {
    const std::string expected = "AUTH " + authToken;
    return received == expected;
}

void TcpServerManager::cycle(uint32_t /*nowMs*/) {
    // ── 1. Probe for a new connection every tick ─────────────────────────────
    // ALWAYS-ALLOW-CONNECT (resilient-reconnect req-4): the firmware never refuses
    // or holds the single client slot. A new arrival replaces any current client
    // (connected or not) — LAST-WINS — so the ESP32 and app stay magnetic/sticky
    // regardless of connect order. The old client is stop()'d, the new one is
    // adopted and pushed through auth. There is no "slot taken" refusal path.
    std::unique_ptr<ITcpServerClient> next = server_.accept();
    if (next) {
        if (current_) {
            current_->stop();
        }
        // Capture the remote IP at accept time (before auth outcome is known)
        // so it is available for observability events regardless of result.
        clientIp_ = next->remoteIP();
        current_ = std::move(next);
        // Latency fix (Phase 1): disable Nagle on the stream client immediately
        // at accept so each CAN frame is sent without waiting for the delayed-ACK
        // window. Mirrors the client-side socket_->setNoDelay(true).
        current_->setNoDelay(true);
        host_.setMonitorActive(false);
        partialLine_.clear();  // fresh client: no leftover partial command

        current_->setTimeout(TCP_AUTH_TIMEOUT_MS);
        std::string firstLine = trim(current_->readLine('\r'));

        // Drop the auth-phase read timeout to a small value for the command
        // phase. The ESP32 WiFiClient read()/peek() can block up to the Stream
        // timeout when no data is buffered, so a large timeout here would stall
        // cycle() (and the CAN-TX path) for seconds on an idle socket. A small
        // (effectively non-blocking) timeout keeps readAvailableLine O(1) per
        // tick while still letting a slow command byte arrive.
        current_->setTimeout(TCP_COMMAND_TIMEOUT_MS);

        if (isValidAuthToken(firstLine, tokenProvider_())) {
            current_->println("OK");
            current_->flush();
            // ── Stream-on-connect ────────────────────────────────────────────
            // An authenticated client is, by contract, asking for the CAN
            // stream. The raw (non-ELM327) host protocol never sends ATMA, so
            // waiting for it would leave the client connected and silent. The
            // ATMA/ATPC handlers still toggle this same flag, so ELM327-mode
            // clients are unaffected — ATMA on an already-active monitor is
            // idempotent, and ATPC can still pause the stream explicitly.
            host_.setMonitorActive(true);
            // LED pattern is now owned by FirmwareApp via selectLedPattern.
            // FirmwareApp::update() queries IClientConnectionSource (backed by
            // TcpServerManager::hasClient()) and will show CLIENT_CONNECTED on
            // the next loop tick (clientConnected=true).
            host_.onClientConnected(clientIp_);
        } else {
            current_->println("ERROR unauthorized");
            current_->flush();
            current_->stop();
            host_.onAuthFailed(clientIp_);
            current_.reset();
            clientIp_.clear();
        }
        return;
    }

    // ── 2. No new client this tick; manage the adopted one ───────────────────
    const bool haveClient = current_ && current_->connected();

    if (haveClient) {
        // Command mode: NON-BLOCKING line assembly. readAvailableLine() drains
        // whatever bytes are already in the receive buffer (delimiter NOT
        // stripped) and never blocks up to the Stream timeout — so a
        // partially-arrived command cannot stall this cycle() and delay
        // CanBridge::processFrames (the CAN-TX path). We accumulate partial
        // bytes in partialLine_ and dispatch only on a complete
        // (delimiter-terminated) line. This removes the per-tick TX starvation
        // that the blocking readStringUntil (TCP_COMMAND_TIMEOUT_MS = 100ms
        // ceiling) introduced.
        //
        // NOTE: we do NOT gate on available()>0 before calling readAvailableLine.
        // On the ESP32 WiFiClient, available() can transiently return 0 even when
        // bytes are buffered (e.g. right after the AUTH readStringUntil), which
        // would make a gate skip ready input permanently and leave TCP AT
        // commands (ATI/ATHELO/PING) unanswered. readAvailableLine() polls
        // read() directly (returns -1 instantly when empty) so it is safe to
        // call unconditionally every tick.
        std::string incoming = current_->readAvailableLine('\r');
        if (!incoming.empty()) {
            partialLine_ += incoming;
            const auto delim = partialLine_.find('\r');
            if (delim != std::string::npos) {
                // One or more complete commands may be coalesced. Dispatch each.
                std::string::size_type pos = 0;
                while ((pos = partialLine_.find('\r')) != std::string::npos) {
                    const std::string cmd = trim(partialLine_.substr(0, pos));
                    partialLine_.erase(0, pos + 1);  // consume through delimiter
                    if (!cmd.empty()) {
                        // Keepalive contract (Phase 1): PING <seq> -> PONG <seq>.
                        // Handled here (not the generic AT dispatcher) because it
                        // is a transport-health frame, not a CAN/AT command — and
                        // it must round-trip with minimal latency, mirroring the
                        // macOS client's performPing() RTT probe.
                        if (cmd.rfind("PING ", 0) == 0) {
                            const std::string seq = cmd.substr(5);
                            current_->println("PONG " + seq);
                            current_->flush();
                        } else {
                            host_.handleTcpAtCommand(cmd);
                        }
                    }
                }
            }
        }
        return;
    }

    // ── 3. Adopted client has dropped — disconnect cleanup ───────────────────
    // Only meaningful if we held a client handle (current_ non-null). Fires on
    // transition from connected → not, once, then the handle is released.
    if (current_) {
        host_.setMonitorActive(false);
        host_.resetDiscoveryBackoff();
        host_.onClientDisconnected(clientIp_, 0);

        partialLine_.clear();  // dropped client: discard any partial command

        // LED pattern is now owned by FirmwareApp via selectLedPattern.
        // IClientConnectionSource (backed by TcpServerManager::hasClient())
        // will report clientConnected=false on the next loop tick, and
        // FirmwareApp::update() will select the correct wifi-state pattern
        // (e.g. WIFI_CONNECTED for WIFI_CONNECTED).
        current_.reset();
        clientIp_.clear();
    }
}

void TcpServerManager::writeLineToClient(const std::string& line) {
    // Guard on the SAME connected() predicate cycle() uses (line 119): a client
    // handle may be non-null (hasClient() true) yet already dropped, and
    // writing to a dead socket stalls the loop. Only send when live.
    if (current_ && current_->connected()) {
        current_->println(line);
    }
}

void TcpServerManager::start() {
    // No-op: listening-socket begin/end are hardware side effects owned by the
    // .ino (restartTcpServerIfNeeded). Reserved for future setup-time hooks.
}

void TcpServerManager::stop() {
    // No-op: listening-socket begin/end are hardware side effects owned by the
    // .ino (restartTcpServerIfNeeded). Drop any adopted client on shutdown.
    if (current_) {
        current_->stop();
        current_.reset();
    }
}

} // namespace esp32_firmware
