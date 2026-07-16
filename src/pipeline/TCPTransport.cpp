#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/boundary/ELM327Transport.h"
#include "vehicle-sim/pipeline/PosixSocket.h"

#if !defined(BUILD_IOS) && (!defined(TARGET_OS_IPHONE) || TARGET_OS_IPHONE == 0)
// Hunt-on-disconnect: host resilience (not needed for iOS — it has its own scanning)
#include "vehicle-sim/discovery/UDPDiscovery.h"
#endif

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <thread>

namespace vehicle_sim::pipeline {

namespace {

// Connection/reconnect constants
constexpr std::size_t MAX_PENDING_LEN = 4096;  // Guard against runaway line buffering
constexpr int DEFAULT_PER_COMMAND_DELAY_MS = 50;  // Fallback per-command delay when none is supplied

// Hex-encode a 16-byte device id as 32 uppercase chars ("%02X" per byte).
// Shared by performHeloHandshake() (local id) and runDiscovery() (discovered id)
// so the two encodings can never drift — both sides of the device-match
// comparison (exact + 8-char prefix) are produced by this one function.
std::string deviceIdToHex(const std::array<uint8_t, 16>& deviceId) {
    std::string hex;
    hex.reserve(32);
    for (uint8_t byte : deviceId) {
        std::array<char, 3> buf{};
        snprintf(buf.data(), buf.size(), "%02X", byte);
        hex.append(buf.data());
    }
    return hex;
}

// Read from a socket into buf (usable capacity = cap - 1) until the trailing
// bytes match `prompt`, the buffer is full, or recv() returns <= 0
// (timeout/error). Returns the number of bytes accumulated; the caller decides
// whether a non-positive total is fatal (ATI is optional-but-expected, ATHELO
// required).
int recvUntilPrompt(ISocket& sock, char* buf, std::size_t cap, std::string_view prompt) {
    int total = 0;
    while (total < static_cast<int>(cap - 1)) {
        auto n = static_cast<int>(sock.recv(buf + total, cap - 1 - static_cast<std::size_t>(total)));
        if (n <= 0) {
            return total;  // Timeout/error/close — caller judges severity
        }
        total += n;
        // Stop as soon as the prompt appears at the tail of the accumulated bytes.
        if (static_cast<std::size_t>(total) >= prompt.size() &&
            std::string_view(buf + total - prompt.size(), prompt.size()) == prompt) {
            return total;
        }
    }
    return total;
}

// Parse a hex byte (two hex characters) into its uint8_t value.
// Returns true on success, false if the input is not valid hex.
bool parseHexByte(std::string_view s, std::size_t offset, uint8_t& out) {
    if (offset + 2 > s.size()) return false;
    auto hexCharToInt = [](char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    int hi = hexCharToInt(s[offset]);
    int lo = hexCharToInt(s[offset + 1]);
    if (hi < 0 || lo < 0) return false;
    out = static_cast<uint8_t>((hi << 4) | lo);
    return true;
}

// Parse/validate a HELO ACK string and fill deviceId (16 bytes) on success.
// Pure and socket-free: it only inspects the supplied string.
//
// Returns true iff the string has the "ACK DEVICE=" prefix, a "DEVICEID="
// token whose value is exactly 32 valid hex chars (after stripping any
// trailing \r \n space '>'), and every byte decodes. On failure returns false
// and leaves deviceId untouched. The FIRMWARE= token is intentionally not
// required (it is informational in the protocol) — a valid ACK needs only the
// two tokens above plus the well-formed device id.
bool parseHeloAck(std::string_view ack, std::array<uint8_t, 16>& deviceId) {
    constexpr std::string_view ackPrefix = "ACK DEVICE=";
    constexpr std::string_view deviceIdToken = "DEVICEID=";

    if (ack.find(ackPrefix) == std::string_view::npos) {
        return false;
    }

    const std::size_t deviceIdPos = ack.find(deviceIdToken);
    if (deviceIdPos == std::string_view::npos) {
        return false;
    }

    const std::size_t hexStart = deviceIdPos + deviceIdToken.length();
    std::string_view hexId = ack.substr(hexStart);

    // Clean up any trailing whitespace/CRLF/prompt.
    while (!hexId.empty() &&
           (hexId.back() == '\r' || hexId.back() == '\n' ||
            hexId.back() == ' ' || hexId.back() == '>')) {
        hexId.remove_suffix(1);
    }

    // Validate we have exactly 32 hex characters.
    if (hexId.length() != 32) {
        return false;
    }

    // Parse each byte.
    for (int i = 0; i < 16; ++i) {
        if (!parseHexByte(hexId, static_cast<std::size_t>(i * 2), deviceId[i])) {
            return false;
        }
    }

    return true;
}

} // namespace

TCPTransport::TCPTransport(TransportEndpoint endpoint,
                           std::shared_ptr<ITransportOutput> output,
                           TcpReadTiming timing,
                           std::shared_ptr<StopToken> stop,
                           HuntResilienceConfig hunt,
                           std::shared_ptr<util::IClock> clock,
                           std::shared_ptr<ISocket> socket)
    : host_(std::move(endpoint.host))
    , port_(endpoint.port)
    , adapterProtocol_(std::move(endpoint.protocol))
    , output_(std::move(output))
    , stop_(std::move(stop))
    , hunt_(std::move(hunt))
    , clock_(std::move(clock))
    , socket_(std::move(socket))
    , readTimeoutUs_(timing.readTimeoutUs > 0 ? timing.readTimeoutUs : 500000)
    , atInitDelayMs_(timing.atInitDelayMs)
    , socketRecvTimeoutMs_(timing.socketRecvTimeoutMs > 0 ? timing.socketRecvTimeoutMs : 1000) {
}

TCPTransport::~TCPTransport() {
    closeConnection();
}

// ELM327 init pacing: honours the overridable atInitDelayMs_ while never
// regressing to hard-coded defaults that tests can't control.
// Single call site (sendElm327Init) — kept as a named method to document the override point, not for reuse.
int TCPTransport::perCommandDelayMs(int cmdDelayMs) const {
    if (atInitDelayMs_ >= 0) {
        return atInitDelayMs_;  // tests / callers override every command
    }
    return cmdDelayMs > 0 ? cmdDelayMs : DEFAULT_PER_COMMAND_DELAY_MS;
}

bool TCPTransport::sendAll(std::string_view data) const noexcept {
    return socket_->sendAll(data);
}

bool TCPTransport::sendElm327Init() noexcept {
    // Reuse the shared ELM327 CAN-monitor init sequence (ATZ/ATE0/ATSP6/ATH1/
    // ATMA). The ELM327 *normaliser* (prompt/status parsing) is a later task
    // (#18); today elm327 only changes the connect handshake so a real adapter
    // enters monitor mode before we read its raw frame lines.
    const auto initSeq = boundary::ELM327Transport::buildCANMonitorInitSequence();
    for (const auto& cmd : initSeq) {
        if (!sendAll(cmd.command)) {
            output_->err("[tcp] Failed to send AT command: " + cmd.command);
            return false;
        }
        // Read and discard the response to keep the buffer clean for HELO
        // Use a short timeout - responses should arrive promptly
        if (int ready = socket_->selectReadable(100000); ready > 0) {
            std::array<char, 256> resp{};
            auto n = static_cast<int>(socket_->recv(resp.data(), resp.size() - 1));
            if (n <= 0) {
                output_->err("[tcp] ELM327 init: no response to AT command (peer closed or error)");
                return false;
            }
        }
        // If no response within timeout, continue anyway - device may be slow

        // Pace each AT command so the adapter can process it before the next one.
        // Routes through IClock so tests inject a FakeClock and the pacing is
        // instant (production uses the real SystemClock wall-clock sleep).
        clock_->sleepFor(std::chrono::milliseconds(perCommandDelayMs(cmd.delayMs)));
    }
    return true;
}

bool TCPTransport::connectAndAuth() {
    closeConnection();
    if (socket_->connect(host_, port_, stop_.get()) < 0) return false;
    connected_ = true;
    if (!socket_->setRecvTimeout(socketRecvTimeoutMs_)) return false;

    // Authenticate: send token, expect "OK" back
    if (std::string authCmd = "AUTH " TCP_AUTH_TOKEN "\r"; !sendAll(authCmd)) { closeConnection(); return false; }
    std::array<char, 64> authResp{};
    if (auto n = static_cast<int>(socket_->recv(authResp.data(), authResp.size() - 1));
        n <= 0 || std::string(authResp.data(), static_cast<std::size_t>(n)).find("OK") == std::string::npos) {
        closeConnection(); return false;
    }

    if (adapterProtocol_ == "elm327" && !sendElm327Init()) { closeConnection(); return false; }

    // Perform HELO handshake to validate device and capture deviceId
    if (!performHeloHandshake()) {
        closeConnection();
        return false;
    }

    return true;
}

bool TCPTransport::sendHeloAndParseAck(std::array<uint8_t, 16>& deviceId) {
    // First ensure we have an authenticated connection.
    if (!connected_ && !connectAndAuth()) {
        output_->err("[tcp] HELO pre-flight: connection failed");
        return false;
    }

    // Send ATI (device info query)
    if (const std::string atiCmd = "ATI\r"; !sendAll(atiCmd)) {
        output_->err("[tcp] HELO pre-flight: failed to send ATI");
        closeConnection();
        return false;
    }

    // Read and discard ATI response (we don't parse it, just clear the buffer).
    std::array<char, 256> atiResp{};
    if (int totalAti = recvUntilPrompt(*socket_, atiResp.data(), atiResp.size(), "\r>"); totalAti <= 0) {
        output_->err("[tcp] HELO pre-flight: no response to ATI");
        closeConnection();
        return false;
    }

    // Small delay to let the device process — routed through IClock so a fake
    // clock keeps the test instant (production parks on real wall-clock).
    clock_->sleepFor(std::chrono::milliseconds(50));

    // Send ATHELO command
    if (const std::string heloCmd = "ATHELO\r"; !sendAll(heloCmd)) {
        output_->err("[tcp] HELO pre-flight: failed to send ATHELO");
        closeConnection();
        return false;
    }

    // Read HELO ACK response with proper recv loop for fragmented TCP.
    std::array<char, 256> heloResp{};
    const int totalHelo = recvUntilPrompt(*socket_, heloResp.data(), heloResp.size(), "\r\r>");
    if (totalHelo <= 0) {
        output_->err("[tcp] HELO pre-flight: no response to ATHELO");
        closeConnection();
        return false;
    }

    // Parse/validate the ACK string (pure, socket-free). On failure we must
    // close the authenticated connection and treat the handshake as failed.
    if (std::string response(heloResp.data(), static_cast<std::size_t>(totalHelo));
        !parseHeloAck(response, deviceId)) {
        closeConnection();
        return false;
    }

    // HELO succeeded
    output_->out("[tcp] HELO pre-flight: device acknowledged");
    return true;
}

bool TCPTransport::performHeloHandshake() {
    std::array<uint8_t, 16> deviceIdBytes{};
    if (!sendHeloAndParseAck(deviceIdBytes)) {
        return false;
    }

    // Convert deviceId bytes to hex string for message tagging
    deviceIdHex_ = deviceIdToHex(deviceIdBytes);
    return true;
}

#if !defined(BUILD_IOS) && (!defined(TARGET_OS_IPHONE) || TARGET_OS_IPHONE == 0)
// Hunt-on-disconnect: host resilience (not needed for iOS — it has its own scanning)

// Exponential backoff calculation (used by hunting logic)
constexpr int calculateRetryDelayMs(int retryCount) {
    // Exponential backoff: 1s, 2s, 4s, 8s, capped at MAX_RETRY_DELAY_MS
    int delay = TCPTransport::BASE_RETRY_DELAY_MS * (1 << std::min(retryCount, 12));
    return std::min(delay, TCPTransport::MAX_RETRY_DELAY_MS);
}

// Resolve the per-hunt discovery listener. The injected factory wins when set
// (tests inject a no-op so the discovery-win path is unreachable and the hunt
// is hermetic regardless of any device broadcasting on the LAN); otherwise the
// real UDPDiscovery is constructed fresh per hunt (mirroring the original
// stack-local, including its empty per-hunt dedup state).
std::unique_ptr<discovery::IDiscoveryListener>
TCPTransport::resolveDiscoveryListener() {
    namespace discovery = vehicle_sim::discovery;
    if (hunt_.discoveryFactory) {
        return hunt_.discoveryFactory();
    }
    return std::unique_ptr<discovery::IDiscoveryListener>(
        std::make_unique<discovery::UDPDiscovery>());
}

bool TCPTransport::isTargetDevice(const discovery::DiscoveredDevice& device,
                                  std::string_view discoveredHex) const {
    // Match rule (G1/G2/G3): deviceIdHex_ empty = match-all; otherwise the
    // discovered hex must match exactly OR by the first 8 hex chars (4-byte
    // prefix). The device must also NOT be at the current host_. Compare the
    // 8-char prefix via string_view::substr on both sides (S6231: a
    // std::string::substr would create a needless temporary).
    const std::string_view knownId(deviceIdHex_);
    const bool idMatches = knownId.empty() ||
                           discoveredHex == knownId ||
                           discoveredHex.substr(0, 8) == knownId.substr(0, 8);
    return idMatches && device.address != host_;
}

void TCPTransport::runDiscovery(discovery::IDiscoveryListener& hunter,
                                const std::atomic<bool>& shouldStopDiscovery,
                                std::atomic<bool>& discoveryFound,
                                std::string& discoveredIp) {
    // Background discovery loop (extracted verbatim from the old [&]-lambda).
    if (!hunter.start()) {
        output_->err("[tcp] Failed to start UDP discovery listener");
        return;
    }

    // Keep polling until connection succeeds or we're told to stop.
    while (!shouldStopDiscovery.load() && !stop_->stopRequested()) {
        auto devices = hunter.poll(std::chrono::milliseconds(500));

        bool shouldStop = false;
        for (const auto& device : devices) {
            if (isTargetDevice(device, deviceIdToHex(device.deviceId))) {
                discoveredIp = device.address;
                discoveryFound.store(true);
                output_->out("[tcp] Discovery: found device at new IP " + device.address +
                             " (was " + host_ + ")" + " [" + kEsp32TagPrefix + ":" +
                             deviceIdHex_.substr(0, 8) + "]");
                shouldStop = true;
                break;
            }
        }

        if (discoveryFound.load() || shouldStop) {
            break;  // Found new IP, exit discovery loop.
        }
    }

    hunter.stop();
}

// Sleep for one backoff delay, sliced into 100ms chunks so a discovery win or
// a requestStop() interrupts within one slice (G8) rather than after the full
// delay; then, if discovery didn't win, attempt one old-IP reconnect. Returns
// true (loopDone) once discovery wins or old-IP reconnect succeeds; sets
// reconnected=true on an old-IP win.
//
// The 100ms-sliced loop is preserved exactly (this is the load-bearing
// interrupt property from G8) — but the inner sleep is now clock_->sleepFor
// (FakeClock = instant in tests, SystemClock = real sleep in production) rather
// than the removed IBackoffSleeper seam. The stop predicate mirrors the
// original loop guard exactly (discovery win OR requestStop).
bool TCPTransport::backoffThenAttemptReconnect(int delayMs,
                                               const std::atomic<bool>& discoveryFound,
                                               bool& reconnected) {
    constexpr int checkInterval = 100;  // re-check discovery/stop every 100ms
    for (int elapsed = 0; elapsed < delayMs; elapsed += checkInterval) {
        if (discoveryFound.load() || stop_->stopRequested()) {
            return true;  // discovery win or requestStop: abort remaining sleep.
        }
        clock_->sleepFor(std::chrono::milliseconds(std::min(checkInterval, delayMs - elapsed)));
    }

    if (discoveryFound.load()) {
        output_->out("[tcp] hunting: discovery found new IP first, switching...");
        return true;  // Discovery won, exit retry loop.
    }

    // Try connecting to old IP.
    if (connectAndAuth()) {
        reconnected = true;
        output_->out("[tcp] hunting: reconnected to old IP " + host_ + ":" + std::to_string(port_) +
                     " [" + kEsp32TagPrefix + ":" + deviceIdHex_.substr(0, 8) + "]" + kClientTag);
        return true;  // Old IP won.
    }
    return false;  // keep retrying
}

// Decide the hunt outcome after the retry loop + discovery have settled.
// Single exit: returns the hunt result and resets retryCount_ on success.
bool TCPTransport::finalizeHunt(const std::atomic<bool>& discoveryFound,
                                std::string_view discoveredIp,
                                bool reconnected) {
    if (reconnected) {
        retryCount_ = 0;  // Old IP reconnected - we're done.
        return true;
    }

    if (discoveryFound.load() && !discoveredIp.empty()) {
        // Discovery found a new IP first: switch host_ BEFORE the connect
        // attempt so a failed new-IP connect leaves host_ switched (G11).
        const std::string oldHost = host_;
        host_ = discoveredIp;
        output_->out("[tcp] hunting: switching to discovered IP " + host_ +
                     " (was " + oldHost + ")" + kClientTag);

        if (connectAndAuth()) {
            output_->out("[tcp] hunting: connected to new IP " + host_ + ":" + std::to_string(port_) +
                         " [" + kEsp32TagPrefix + ":" + deviceIdHex_.substr(0, 8) + "]" + kClientTag);
            retryCount_ = 0;
            return true;
        }
        output_->err("[tcp] hunting: failed to connect to new IP " + host_ + " — giving up");
        return false;
    }

    // Neither old IP reconnected nor discovery found a new IP.
    output_->err("[tcp] hunting: neither old IP nor discovery succeeded — giving up");
    return false;
}

bool TCPTransport::enterHuntingState() {
    // Start UDP discovery listener immediately (device broadcasts every 0.5-2s).
    auto hunter = resolveDiscoveryListener();
    std::atomic discoveryFound(false);
    std::atomic shouldStopDiscovery(false);
    std::string discoveredIp;

    // Background thread: listen for UDP discovery broadcasts.
    std::thread discoveryListener([this, &hunter, &shouldStopDiscovery,
                                   &discoveryFound, &discoveredIp]() {
        runDiscovery(*hunter, shouldStopDiscovery, discoveryFound, discoveredIp);
    });

    // Main thread: retry old IP with exponential backoff.
    bool reconnected = false;
    retryCount_ = 0;
    bool loopDone = false;

    // Signal "the hunt loop is now live" exactly once, the instant before the
    // first retry/backoff attempt — so an observer can await hunt-start on this
    // signal (latch/future/cv) instead of polling or sleeping. Default (empty)
    // is a no-op, so production behavior is unchanged.
    if (hunt_.onHuntStarted) {
        hunt_.onHuntStarted();
    }

    while (!loopDone && retryCount_ < MAX_RETRIES && !discoveryFound.load() && !stop_->stopRequested()) {
        retryCount_++;
        const int delayMs = calculateRetryDelayMs(retryCount_ - 1);

        output_->err("[tcp] hunting: retrying old IP " + host_ + ":" + std::to_string(port_) +
                     " (attempt " + std::to_string(retryCount_) + "/" + std::to_string(MAX_RETRIES) +
                     " in " + std::to_string(delayMs) + "ms)" + kClientTag + "...");

        loopDone = backoffThenAttemptReconnect(delayMs, discoveryFound, reconnected);
        retryCount_++;
    }

    // Stop + join the discovery thread (exactly once).
    shouldStopDiscovery.store(true);
    if (discoveryListener.joinable()) {
        discoveryListener.join();
    }

    return finalizeHunt(discoveryFound, discoveredIp, reconnected);
}
#endif // !BUILD_IOS && (!defined(TARGET_OS_IPHONE) || TARGET_OS_IPHONE == 0)

void TCPTransport::closeConnection() noexcept {
    socket_->close();
    connected_ = false;
}

bool TCPTransport::open() {
    if (opened_) return !exhausted_;
    opened_ = true;
    retryCount_ = 0;
    if (!connectAndAuth()) {
        output_->err("[tcp] Failed to connect to " + host_ + ":" + std::to_string(port_));
        return false;
    }
    if (!deviceIdHex_.empty()) {
        output_->out("[tcp] Monitoring " + host_ + ":" + std::to_string(port_) + kClientTag + " [" + kEsp32TagPrefix + ":" + deviceIdHex_ + "]");
    } else {
        output_->out("[tcp] Monitoring " + host_ + ":" + std::to_string(port_) + kClientTag);
    }
    pending_.reserve(256);
    return true;
}

bool TCPTransport::isOpen() const noexcept {
    return opened_ && connected_ && !exhausted_;
}

std::optional<std::string> TCPTransport::takeBufferedLine() {
    const std::size_t end = pending_.find_first_of("\r\n");
    if (end == std::string::npos) {
        return std::nullopt;  // no complete line buffered — need more bytes
    }
    std::string line(pending_, 0, end);
    pending_.erase(0, end + 1);
    // We return the line verbatim (the normaliser tolerates a trailing '\r'
    // already stripped here by the terminator split). An empty line from a
    // "\r\r" banner sequence is delivered as "" — the normaliser Skip's it.
    return line;
}

int TCPTransport::selectReady() const {
    // Honour the injected read timeout, but cap each select() poll at a
    // 1ms floor so the loop still wakes promptly on stop/disconnect even
    // when a test injects a sub-millisecond value. Production default
    // (500000us) is unaffected — min(500000, 1000 floor) == 1000 per poll,
    // and the stop flag is re-checked every poll, same as before.
    const int pollUs = std::min(readTimeoutUs_, 1000);
    return socket_->selectReadable(pollUs);
}

ssize_t TCPTransport::readSocketIntoPending() {
    std::array<char, 256> buffer;
    ssize_t n = socket_->recv(buffer.data(), buffer.size());
    if (n > 0) {
        pending_.append(buffer.data(), static_cast<std::size_t>(n));

        // Defensive cap so a peer that never sends a line ending can't grow
        // the buffer without bound.
        if (pending_.size() > MAX_PENDING_LEN) {
            pending_.clear();
        }
    }
    return n;
}

bool TCPTransport::shouldStop() const {
    return stop_->stopRequested();
}

// Format the "disconnected ... reconnecting" message. The deviceIdHex_-set
// variant tags the ESP32 id (when a HELO handshake completed); the empty
// variant omits the tag (pre-HELO disconnect).
std::string TCPTransport::formatDisconnectMessage() const {
    const std::string base = "[tcp] disconnected from " + host_ + ":" +
                             std::to_string(port_);
    if (deviceIdHex_.empty()) {
        return base + " — reconnecting" + kClientTag + "...";
    }
    return base + " [" + kEsp32TagPrefix + ":" + deviceIdHex_ +
           "] — reconnecting" + kClientTag + "...";
}

// Handle a peer-close/error read (recv <= 0): if stopped, give up immediately;
// otherwise emit the disconnect message, close the socket, and (host build)
// enter the hunting state for reconnect-or-discovery, or (iOS build) give up.
// Sets exhausted_ on every GiveUp path.
TCPTransport::ReadRecovery TCPTransport::handleReadFailure() {
    if (shouldStop()) {
        exhausted_ = true;
        return ReadRecovery::GiveUp;
    }

    output_->err(formatDisconnectMessage());
    closeConnection();

#if !defined(BUILD_IOS) && (!defined(TARGET_OS_IPHONE) || TARGET_OS_IPHONE == 0)
    // Hunt-on-disconnect: retry old IP + listen for UDP discovery simultaneously.
    if (!enterHuntingState()) {
        output_->err("[tcp] hunting state failed — giving up");
        exhausted_ = true;
        return ReadRecovery::GiveUp;
    }
    return ReadRecovery::Resume;  // reconnected — resume reading
#else
    // iOS: simple retry without hunt (iOS has its own scanning logic).
    output_->err("[tcp] iOS build: hunt disabled — using simple retry");
    exhausted_ = true;
    return ReadRecovery::GiveUp;
#endif
}

std::optional<std::string> TCPTransport::nextLine() {
    if (!canRead()) {
        return std::nullopt;
    }

    // First, satisfy the request from any already-buffered complete line.
    if (auto line = takeBufferedLine()) {
        return *line;
    }

    // Read more bytes from the socket with a bounded select() so we never hang.
    while (true) {
        if (const int ready = selectReady(); ready < 0) {
            if (errno == EINTR) continue;  // signal — retry
            exhausted_ = true;             // genuine error → treat as EOF
            return std::nullopt;
        } else if (ready == 0) {
            if (shouldStop()) {
                exhausted_ = true;
                return std::nullopt;
            }
            continue;
        }

        if (ssize_t n = readSocketIntoPending(); n <= 0) {
            if (handleReadFailure() == ReadRecovery::GiveUp) {
                return std::nullopt;
            }
            continue;  // reconnected — resume reading
        }

        if (const std::size_t end = pending_.find_first_of("\r\n");
            end == std::string::npos) {
            continue;  // still no complete line — read more
        } else {
            std::string line(pending_, 0, end);
            pending_.erase(0, end + 1);
            return line;
        }
    }
}

} // namespace vehicle_sim::pipeline
