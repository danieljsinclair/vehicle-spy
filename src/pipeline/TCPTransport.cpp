#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/boundary/ELM327Transport.h"

#if !defined(BUILD_IOS) && !defined(TARGET_OS_IPHONE)
// Hunt-on-disconnect: host resilience (not needed for iOS — it has its own scanning)
#include "vehicle-sim/discovery/UDPDiscovery.h"
#endif

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

namespace vehicle_sim::pipeline {

namespace {

// select() read timeout floor — the production default (0.5s) keeps nextLine()
// responsive to EOF/disconnect and a vanished peer, matching the capture tool's
// robustness target. The actual per-instance timeout is injectable via the
// constructor (readTimeoutUs_) so tests can pass a sub-millisecond value.
// Kept as a constant for documentation / comparison only.
constexpr int READ_TIMEOUT_US_FLOOR = 1000;  // 1ms poll floor so sub-ms injects still wake promptly

// Connection/reconnect constants
constexpr int CONNECT_TIMEOUT_S = 5;          // How long connect() may block before failing
constexpr std::size_t MAX_PENDING_LEN = 4096;  // Guard against runaway line buffering
constexpr int DEFAULT_PER_COMMAND_DELAY_MS = 50;  // Fallback per-command delay when none is supplied


// Resolve host:port into a connected TCP socket, or -1 on failure. Uses a
// bounded connect() so an unreachable board can't hang the CLI.
int connectToHost(const std::string& host, int port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;      // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;

    std::string portStr = std::to_string(port);

    addrinfo* result = nullptr;
    if (int rc = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result); rc != 0 || result == nullptr) {
        if (result != nullptr) freeaddrinfo(result);
        return -1;
    }

    int fd = -1;
    for (addrinfo* ai = result; ai != nullptr; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            continue;
        }

        // Suppress SIGPIPE: writing to a closed socket must return an error,
        // not kill the CLI. macOS lacks MSG_NOSIGNAL, so set SO_NOSIGPIPE.
#ifdef SO_NOSIGPIPE
        int nosig = 1;
        (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &nosig, sizeof(nosig));
#endif

        // Bound connect timeout so a dead host fails fast instead of hanging.
        struct timeval tv{};
        tv.tv_sec = CONNECT_TIMEOUT_S;
        tv.tv_usec = 0;
        (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            break;  // success
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(result);
    return fd;
}

} // namespace

TCPTransport::TCPTransport(std::string_view host, int port, std::string_view adapterProtocol,
                           std::shared_ptr<ITransportOutput> output,
                           TcpReadTiming timing,
                           std::shared_ptr<StopToken> stop)
    : host_(host)
    , port_(port)
    , adapterProtocol_(adapterProtocol)
    , output_(std::move(output))
    , stop_(std::move(stop))
    , readTimeoutUs_(timing.readTimeoutUs > 0 ? timing.readTimeoutUs : 500000)
    , atInitDelayMs_(timing.atInitDelayMs)
    , socketRecvTimeoutMs_(timing.socketRecvTimeoutMs > 0 ? timing.socketRecvTimeoutMs : 1000) {
}

TCPTransport::~TCPTransport() {
    if (fd_ >= 0) {
        close(fd_);
    }
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

bool TCPTransport::sendAll(int fd, std::string_view data) const noexcept {
    std::size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool TCPTransport::sendElm327Init(int fd) noexcept {
    // Reuse the shared ELM327 CAN-monitor init sequence (ATZ/ATE0/ATSP6/ATH1/
    // ATMA). The ELM327 *normaliser* (prompt/status parsing) is a later task
    // (#18); today elm327 only changes the connect handshake so a real adapter
    // enters monitor mode before we read its raw frame lines.
    const auto initSeq = boundary::ELM327Transport::buildCANMonitorInitSequence();
    for (const auto& cmd : initSeq) {
        if (!sendAll(fd, cmd.command)) {
            output_->err("[tcp] Failed to send AT command: " + cmd.command);
            return false;
        }
        // Read and discard the response to keep the buffer clean for HELO
        // Use a short timeout - responses should arrive promptly
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(fd, &readSet);
        struct timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 100000;  // 100ms timeout for response
        if (int ready = select(fd + 1, &readSet, nullptr, nullptr, &tv); ready > 0) {
            std::array<char, 256> resp{};
            auto n = static_cast<int>(recv(fd, resp.data(), resp.size() - 1, 0));
            if (n <= 0) {
                output_->err("[tcp] ELM327 init: no response to AT command (peer closed or error)");
                return false;
            }
        }
        // If no response within timeout, continue anyway - device may be slow

        // Pace each AT command so the adapter can process it before the next one.
        std::this_thread::sleep_for(std::chrono::milliseconds(perCommandDelayMs(cmd.delayMs)));
    }
    return true;
}

bool TCPTransport::connectAndAuth() {
    closeConnection();
    fd_ = connectToHost(host_, port_);
    if (fd_ < 0) return false;

    struct timeval rtv{};
    rtv.tv_sec = socketRecvTimeoutMs_ / 1000;
    rtv.tv_usec = (socketRecvTimeoutMs_ % 1000) * 1000;
    (void)setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));

    // Authenticate: send token, expect "OK" back
    if (std::string authCmd = "AUTH " TCP_AUTH_TOKEN "\r"; !sendAll(fd_, authCmd)) { closeConnection(); return false; }
    std::array<char, 64> authResp{};
    if (auto n = static_cast<int>(recv(fd_, authResp.data(), authResp.size() - 1, 0));
        n <= 0 || std::string(authResp.data(), static_cast<std::size_t>(n)).find("OK") == std::string::npos) {
        closeConnection(); return false;
    }

    if (adapterProtocol_ == "elm327" && !sendElm327Init(fd_)) { closeConnection(); return false; }

    // Perform HELO handshake to validate device and capture deviceId
    if (!performHeloHandshake()) {
        closeConnection();
        return false;
    }

    return true;
}

namespace {

// Read from fd into buf (usable capacity = cap - 1) until the trailing bytes
// match `prompt`, the buffer is full, or recv() returns <= 0 (timeout/error).
// Returns the number of bytes accumulated; the caller decides whether a
// non-positive total is fatal (ATI is optional-but-expected, ATHELO required).
int recvUntilPrompt(int fd, char* buf, std::size_t cap, std::string_view prompt) {
    int total = 0;
    while (total < static_cast<int>(cap - 1)) {
        auto n = static_cast<int>(recv(fd, buf + total, cap - 1 - static_cast<std::size_t>(total), 0));
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

bool TCPTransport::sendHeloAndParseAck(std::array<uint8_t, 16>& deviceId) {
    // First ensure we have an authenticated connection.
    if (fd_ < 0 && !connectAndAuth()) {
        output_->err("[tcp] HELO pre-flight: connection failed");
        return false;
    }

    // Send ATI (device info query)
    if (const std::string atiCmd = "ATI\r"; !sendAll(fd_, atiCmd)) {
        output_->err("[tcp] HELO pre-flight: failed to send ATI");
        closeConnection();
        return false;
    }

    // Read and discard ATI response (we don't parse it, just clear the buffer).
    // ATI is expected but best-effort: a zero/negative total is tolerated below
    // only as a warning path — we still require the device to answer ATHELO.
    std::array<char, 256> atiResp{};
    if (int totalAti = recvUntilPrompt(fd_, atiResp.data(), atiResp.size(), "\r>"); totalAti <= 0) {
        output_->err("[tcp] HELO pre-flight: no response to ATI");
        closeConnection();
        return false;
    }

    // Small delay to let the device process
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Send ATHELO command
    if (const std::string heloCmd = "ATHELO\r"; !sendAll(fd_, heloCmd)) {
        output_->err("[tcp] HELO pre-flight: failed to send ATHELO");
        closeConnection();
        return false;
    }

    // Read HELO ACK response with proper recv loop for fragmented TCP.
    // Expected format: ACK DEVICE=<name> FIRMWARE=<version> DEVICEID=<16-byte hex>\r\r>
    std::array<char, 256> heloResp{};
    const int totalHelo = recvUntilPrompt(fd_, heloResp.data(), heloResp.size(), "\r\r>");
    if (totalHelo <= 0) {
        output_->err("[tcp] HELO pre-flight: no response to ATHELO");
        closeConnection();
        return false;
    }

    // Parse/validate the ACK string (pure, socket-free). On failure we must
    // close the authenticated connection and treat the handshake as failed.
    // C++17 init-statement keeps the response buffer scoped to the parse.
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
    deviceIdHex_.clear();
    deviceIdHex_.reserve(32);
    for (uint8_t byte : deviceIdBytes) {
        std::array<char, 3> hex{};
        snprintf(hex.data(), hex.size(), "%02X", byte);
        deviceIdHex_.append(hex.data());
    }
    return true;
}

#if !defined(BUILD_IOS) && !defined(TARGET_OS_IPHONE)
// Hunt-on-disconnect: host resilience (not needed for iOS — it has its own scanning)

// Exponential backoff calculation (used by hunting logic)
constexpr int calculateRetryDelayMs(int retryCount) {
    // Exponential backoff: 1s, 2s, 4s, 8s, capped at MAX_RETRY_DELAY_MS
    int delay = TCPTransport::BASE_RETRY_DELAY_MS * (1 << std::min(retryCount, 12));
    return std::min(delay, TCPTransport::MAX_RETRY_DELAY_MS);
}

bool TCPTransport::enterHuntingState() {
    namespace discovery = vehicle_sim::discovery;

    // Start UDP discovery listener immediately (device broadcasts every 0.5-2s)
    discovery::UDPDiscovery hunter;
    std::atomic<bool> discoveryFound(false);
    std::atomic<bool> shouldStopDiscovery(false);
    std::string discoveredIp;

    // Background thread: listen for UDP discovery broadcasts
    std::thread discoveryListener([&]() {
        if (!hunter.start()) {
            output_->err("[tcp] Failed to start UDP discovery listener");
            return;
        }

        // Keep polling until connection succeeds or we're told to stop
        while (!shouldStopDiscovery.load() && !stop_->stopRequested()) {
            auto devices = hunter.poll(std::chrono::milliseconds(500));

            // Check if we found our device (or first available if no deviceId)
            for (const auto& device : devices) {
                // Convert discovered device ID to hex for comparison
                std::string discoveredHex;
                discoveredHex.reserve(32);
                for (uint8_t byte : device.deviceId) {
                    char hex[3];
                    snprintf(hex, sizeof(hex), "%02X", byte);
                    discoveredHex.append(hex);
                }

                // Check if this is our device (by deviceId) or first available
                bool isOurDevice = deviceIdHex_.empty() ||
                                   discoveredHex == deviceIdHex_ ||
                                   discoveredHex.substr(0, 8) == deviceIdHex_.substr(0, 8);

                if (isOurDevice && device.address != host_) {
                    // Found device at different IP!
                    discoveredIp = device.address;
                    discoveryFound.store(true);
                    output_->out("[tcp] Discovery: found device at new IP " + device.address +
                               " (was " + host_ + ")" + " [" + kEsp32TagPrefix + ":" + deviceIdHex_.substr(0, 8) + "]");
                    break;
                }
            }

            if (discoveryFound.load()) {
                break;  // Found new IP, exit discovery thread
            }
        }

        hunter.stop();
    });

    // Main thread: retry old IP with exponential backoff
    bool reconnected = false;
    retryCount_ = 0;

    while (retryCount_ < MAX_RETRIES && !discoveryFound.load() && !stop_->stopRequested()) {
        retryCount_++;
        int delayMs = calculateRetryDelayMs(retryCount_ - 1);

        output_->err("[tcp] hunting: retrying old IP " + host_ + ":" + std::to_string(port_) +
                   " (attempt " + std::to_string(retryCount_) + "/" + std::to_string(MAX_RETRIES) +
                   " in " + std::to_string(delayMs) + "ms)" + kClientTag + "...");

        // Sleep for backoff delay, but check periodically if discovery found new IP
        int checkInterval = 100;  // Check every 100ms
        for (int elapsed = 0; elapsed < delayMs && !discoveryFound.load(); elapsed += checkInterval) {
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min(checkInterval, delayMs - elapsed)));
        }

        if (discoveryFound.load()) {
            output_->out("[tcp] hunting: discovery found new IP first, switching...");
            break;  // Discovery won, exit retry loop
        }

        // Try connecting to old IP
        if (connectAndAuth()) {
            reconnected = true;
            output_->out("[tcp] hunting: reconnected to old IP " + host_ + ":" + std::to_string(port_) +
                       " [" + kEsp32TagPrefix + ":" + deviceIdHex_.substr(0, 8) + "]" + kClientTag);
            break;  // Old IP won
        }
    }

    // Stop discovery thread
    shouldStopDiscovery.store(true);
    if (discoveryListener.joinable()) {
        discoveryListener.join();
    }

    // Now decide what to do based on who won
    if (reconnected) {
        // Old IP reconnected - we're done
        retryCount_ = 0;
        return true;
    }

    if (discoveryFound.load() && !discoveredIp.empty()) {
        // Discovery found new IP first - switch and reconnect
        std::string oldHost = host_;
        host_ = discoveredIp;
        output_->out("[tcp] hunting: switching to discovered IP " + host_ + " (was " + oldHost + ")" + kClientTag);

        // Attempt connection to new IP immediately
        if (connectAndAuth()) {
            output_->out("[tcp] hunting: connected to new IP " + host_ + ":" + std::to_string(port_) +
                       " [" + kEsp32TagPrefix + ":" + deviceIdHex_.substr(0, 8) + "]" + kClientTag);
            retryCount_ = 0;
            return true;
        } else {
            output_->err("[tcp] hunting: failed to connect to new IP " + host_ + " — giving up");
            return false;
        }
    }

    // Neither old IP reconnected nor discovery found new IP
    output_->err("[tcp] hunting: neither old IP nor discovery succeeded — giving up");
    return false;
}
#endif // !BUILD_IOS && !TARGET_OS_IPHONE

void TCPTransport::closeConnection() noexcept {
    if (fd_ >= 0) { close(fd_); fd_ = -1; }
}

bool TCPTransport::open() {
    if (opened_) return fd_ >= 0 && !exhausted_;
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
    return opened_ && fd_ >= 0 && !exhausted_;
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
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(fd_, &readSet);

    // Honour the injected read timeout, but cap each select() poll at a
    // 1ms floor so the loop still wakes promptly on stop/disconnect even
    // when a test injects a sub-millisecond value. Production default
    // (500000us) is unaffected — min(500000, 1000 floor) == 1000 per poll,
    // and the stop flag is re-checked every poll, same as before.
    const int pollUs = std::min(readTimeoutUs_, READ_TIMEOUT_US_FLOOR);
    struct timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = pollUs;

    return select(fd_ + 1, &readSet, nullptr, nullptr, &tv);
}

ssize_t TCPTransport::readSocketIntoPending() {
    std::array<char, 256> buffer;
    ssize_t n = recv(fd_, buffer.data(), buffer.size(), 0);
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
        int ready = selectReady();
        if (ready < 0) {
            if (errno == EINTR) continue;  // signal — retry
            exhausted_ = true;             // genuine error → treat as EOF
            return std::nullopt;
        }
        if (ready == 0) {
            // Timeout with no data. A live transport keeps waiting (a quiet
            // bus is normal) — UNLESS a stop was requested (Ctrl+C from the
            // live run context's signal handler), in which case we return
            // nullopt so runReplay() terminates cleanly.
            if (shouldStop()) {
                exhausted_ = true;
                return std::nullopt;
            }
            continue;
        }

        if (ssize_t n = readSocketIntoPending(); n <= 0) {
            // Peer closed (0) or error (<0): attempt reconnection, unless stop
            // was requested (e.g. test cleanup or Ctrl+C). The stop flag is
            // checked before entering expensive hunting logic so tests that call
            // requestStop() before nextLine() terminate promptly instead of
            // waiting for exponential backoff.
            if (shouldStop()) {
                exhausted_ = true;
                return std::nullopt;
            }
            if (!deviceIdHex_.empty()) {
            output_->err("[tcp] disconnected from " + host_ + ":" + std::to_string(port_) + " [" + kEsp32TagPrefix + ":" + deviceIdHex_ + "] — reconnecting" + kClientTag + "...");
        } else {
            output_->err("[tcp] disconnected from " + host_ + ":" + std::to_string(port_) + " — reconnecting" + kClientTag + "...");
        }
            closeConnection();

#if !defined(BUILD_IOS) && !defined(TARGET_OS_IPHONE)
            // Hunt-on-disconnect: retry old IP + listen for UDP discovery simultaneously
            if (!enterHuntingState()) {
                output_->err("[tcp] hunting state failed — giving up");
                exhausted_ = true;
                return std::nullopt;
            }
            continue;  // reconnected — resume reading
#else
            // iOS: simple retry without hunt (iOS has its own scanning logic)
            output_->err("[tcp] iOS build: hunt disabled — using simple retry");
            exhausted_ = true;
            return std::nullopt;
#endif
        }

        // Try to extract a complete line from the newly buffered bytes.
        std::size_t end = pending_.find_first_of("\r\n");
        if (end == std::string::npos) {
            continue;  // still no complete line — read more
        }
        std::string line(pending_, 0, end);
        pending_.erase(0, end + 1);
        return line;
    }
}

} // namespace vehicle_sim::pipeline
