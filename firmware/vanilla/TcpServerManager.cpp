#include "TcpServerManager.h"
#include "StatusLED.h"  // firmware::StatusLED::Pattern ordinals for LED transitions

#include <algorithm>
#include <string>

namespace esp32_firmware {
namespace {

// ── Per-phase read timeouts (ms). Mirror can-bridge.ino Constants so the
// extracted behaviour matches the inline loop exactly. Local to this TU so
// the manager owns no .ino dependency.
constexpr uint32_t TCP_AUTH_TIMEOUT_MS    = 5000;
constexpr uint32_t TCP_COMMAND_TIMEOUT_MS = 100;

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
                                   IStatusLED& statusLed,
                                   const std::string& authToken,
                                   ITcpHostCallbacks& host)
    : server_(server)
    , statusLed_(statusLed)
    , authToken_(authToken)
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
    // A new arrival replaces any current client (connected or not): the old
    // client is stop()'d, the new one is adopted and pushed through auth.
    std::unique_ptr<ITcpServerClient> next = server_.accept();
    if (next) {
        if (current_) {
            current_->stop();
        }
        current_ = std::move(next);
        host_.setMonitorActive(false);

        current_->setTimeout(TCP_AUTH_TIMEOUT_MS);
        std::string firstLine = trim(current_->readLine('\r'));

        if (isValidAuthToken(firstLine, authToken_)) {
            current_->println("OK");
            current_->flush();
            statusLed_.setPattern(
                static_cast<int>(firmware::StatusLED::Pattern::CLIENT_CONNECTED));
        } else {
            current_->println("ERROR unauthorized");
            current_->flush();
            current_->stop();
            current_.reset();
        }
        return;
    }

    // ── 2. No new client this tick; manage the adopted one ───────────────────
    const bool haveClient = current_ && current_->connected();

    if (haveClient) {
        // Command mode: read+dispatch one line when bytes are waiting.
        if (current_->available() > 0) {
            current_->setTimeout(TCP_COMMAND_TIMEOUT_MS);
            const std::string cmd = trim(current_->readLine('\r'));
            if (!cmd.empty()) {
                host_.handleTcpAtCommand(cmd);
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

        const int wifiState = host_.getWiFiState();
        if (wifiState == static_cast<int>(WiFiState::State::CONNECTED_STA) ||
            wifiState == static_cast<int>(WiFiState::State::CONNECTED_AP)) {
            statusLed_.setPattern(
                static_cast<int>(firmware::StatusLED::Pattern::WIFI_CONNECTED));
        }
        current_.reset();
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
