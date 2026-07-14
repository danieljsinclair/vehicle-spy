#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/boundary/ELM327Transport.h"

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
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
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

// Outcome of waiting for a nonblocking connect() to complete.
enum class ConnectWaitResult {
    Connected,   // connect() succeeded (socket is writable, SO_ERROR == 0)
    Failed,      // timeout, poll error, or SO_ERROR set (try next address)
    Cancelled,   // the stop token fired mid-connect (abort the whole resolution)
};

// Wait for a nonblocking connect() on `fd` to either complete within
// CONNECT_TIMEOUT_S, fail, or be cancelled via `stop`. Polls writability in
// 100ms slices (mirroring UDPDiscovery::poll's idiom) so the stop token is
// re-checked each iteration — this is the property that lets requestStop()
// interrupt an in-flight connect during hunting. `stop` is nullable.
ConnectWaitResult waitForConnect(int fd, const StopToken* stop) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(CONNECT_TIMEOUT_S);

    while (true) {
        if (stop != nullptr && stop->stopRequested()) {
            return ConnectWaitResult::Cancelled;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return ConnectWaitResult::Failed;
        }
        const auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     deadline - now).count();
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        const int sliceMs = std::min(static_cast<int>(remainingMs), 100);
        const int ret = ::poll(&pfd, 1, sliceMs);
        if (ret < 0) {
            // EINTR: a signal interrupted poll — re-loop and re-check stop/deadline.
            // Any other error is a genuine failure for this address.
            if (errno != EINTR) {
                return ConnectWaitResult::Failed;
            }
            continue;
        }
        if (ret == 0) {
            continue;  // slice elapsed: re-check stop + deadline
        }
        // Socket is writable (or errored): resolve SO_ERROR to decide.
        int sockErr = 0;
        if (socklen_t optLen = sizeof(sockErr);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &sockErr, &optLen) == 0 && sockErr == 0) {
            return ConnectWaitResult::Connected;
        }
        return ConnectWaitResult::Failed;
    }
}

// Resolve host:port into a connected TCP socket, or -1 on failure. Uses a
// nonblocking connect() polled in bounded chunks so an unreachable board fails
// within CONNECT_TIMEOUT_S AND so a cooperating caller's StopToken can cancel
// an in-flight connect (SO_SNDTIMEO alone does not reliably bound a blocking
// connect() on macOS — the kernel's SYN retransmit can run for a minute+).
// `stop` is nullable: callers without a stop token (e.g. the initial open())
// pass nullptr and get the bounded-timeout behaviour without cancellation.
int connectToHost(const std::string& host, int port, const StopToken* stop) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;      // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;

    const std::string portStr = std::to_string(port);

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

        // Make the socket nonblocking so connect() returns immediately and we
        // can poll for writability (and the stop flag) in bounded chunks. This
        // mirrors UDPDiscovery::poll's chunked-poll idiom (poll in 100ms slices
        // so the stop token is re-checked each iteration).
        const int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            close(fd);
            fd = -1;
            continue;
        }

        const int rc = connect(fd, ai->ai_addr, ai->ai_addrlen);
        const bool immediateSuccess = (rc == 0);
        const bool pending = (rc < 0 && errno == EINPROGRESS);
        if (!immediateSuccess && !pending) {
            // Immediate failure (ECONNREFUSED, ENETUNREACH, ...). Try next addr.
            close(fd);
            fd = -1;
            continue;
        }

        ConnectWaitResult outcome = ConnectWaitResult::Connected;
        if (pending) {
            // EINPROGRESS: connect is pending. Poll for writability, checking
            // the stop token each chunk so requestStop() can interrupt an
            // in-flight connect (the production correctness property the
            // hunting loop needs).
            outcome = waitForConnect(fd, stop);
        }

        if (outcome == ConnectWaitResult::Cancelled) {
            close(fd);
            freeaddrinfo(result);
            return -1;  // stop requested: bail out of the whole resolution
        }
        if (outcome == ConnectWaitResult::Connected) {
            // Restore blocking mode for the subsequent send/recv hot path.
            fcntl(fd, F_SETFL, flags);
            break;  // success
        }

        // Failed: try the next resolved address.
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
                           std::shared_ptr<StopToken> stop,
                           DiscoveryListenerFactory discoveryFactory)
    : host_(host)
    , port_(port)
    , adapterProtocol_(adapterProtocol)
    , output_(std::move(output))
    , stop_(std::move(stop))
    , discoveryFactory_(std::move(discoveryFactory))
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
    fd_ = connectToHost(host_, port_, stop_.get());
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
    if (discoveryFactory_) {
        return discoveryFactory_();
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
    // All mutable state is passed by reference; members (output_/stop_/
    // deviceIdHex_/host_) are accessed directly, so there are NO captures.
    // shouldStopDiscovery is read-only here (load()) -> const ref (S995).
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
                // Found device at a different IP.
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
bool TCPTransport::backoffThenAttemptReconnect(int delayMs,
                                               const std::atomic<bool>& discoveryFound,
                                               bool& reconnected) {
    constexpr int checkInterval = 100;  // re-check discovery/stop every 100ms
    for (int elapsed = 0; elapsed < delayMs && !discoveryFound.load() && !stop_->stopRequested();
         elapsed += checkInterval) {
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min(checkInterval, delayMs - elapsed)));
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

    // Background thread: listen for UDP discovery broadcasts. runDiscovery owns
    // the whole poll loop + device-match decision; captures are explicit (no
    // default [&] — clears S3608/S5019): this for the member call, plus the
    // per-hunt mutable state by reference.
    std::thread discoveryListener([this, &hunter, &shouldStopDiscovery,
                                   &discoveryFound, &discoveredIp]() {
        runDiscovery(*hunter, shouldStopDiscovery, discoveryFound, discoveredIp);
    });

    // Main thread: retry old IP with exponential backoff.
    bool reconnected = false;
    retryCount_ = 0;
    bool loopDone = false;

    while (!loopDone && retryCount_ < MAX_RETRIES && !discoveryFound.load() && !stop_->stopRequested()) {
        retryCount_++;
        const int delayMs = calculateRetryDelayMs(retryCount_ - 1);

        output_->err("[tcp] hunting: retrying old IP " + host_ + ":" + std::to_string(port_) +
                     " (attempt " + std::to_string(retryCount_) + "/" + std::to_string(MAX_RETRIES) +
                     " in " + std::to_string(delayMs) + "ms)" + kClientTag + "...");

        loopDone = backoffThenAttemptReconnect(delayMs, discoveryFound, reconnected);
        retryCount_++;
    }

    // Stop + join the discovery thread (exactly once — the old code had a
    // duplicate joinable()/join() block, dead after the first join).
    shouldStopDiscovery.store(true);
    if (discoveryListener.joinable()) {
        discoveryListener.join();
    }

    return finalizeHunt(discoveryFound, discoveredIp, reconnected);
}
#endif // !BUILD_IOS && (!defined(TARGET_OS_IPHONE) || TARGET_OS_IPHONE == 0)

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
// Sets exhausted_ on every GiveUp path. nextLine() maps Resume -> continue and
// GiveUp -> return nullopt, matching the original inline control flow.
TCPTransport::ReadRecovery TCPTransport::handleReadFailure() {
    // Stop requested (test cleanup / Ctrl+C): bail out before the expensive
    // hunting logic so a stop terminates promptly instead of waiting for the
    // exponential backoff.
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
            // Peer closed (0) or error (<0): delegate reconnect/give-up to the
            // focused recovery helper (keeps this loop a simple dispatcher).
            if (handleReadFailure() == ReadRecovery::GiveUp) {
                return std::nullopt;
            }
            continue;  // reconnected — resume reading
        }

        // Try to extract a complete line from the newly buffered bytes.
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
