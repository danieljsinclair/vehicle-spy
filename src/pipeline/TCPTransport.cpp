#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/boundary/ELM327Transport.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>
#include <thread>

namespace vehicle_sim::pipeline {

// Process-wide stop flag, set by requestStop() (from a signal handler) and
// polled by nextLine() on each select() timeout so a live stream stops cleanly
// without hanging. A single flag serves all TCPTransport instances — only one
// live transport runs per process.
namespace {
std::atomic<bool> g_stopRequested{false};
}  // namespace

void TCPTransport::requestStop() noexcept {
    g_stopRequested.store(true);
}

void TCPTransport::resetStop() noexcept {
    g_stopRequested.store(false);
}

namespace {

// select() read timeout — keeps nextLine() responsive to EOF/disconnect and a
// vanished peer. Matches the capture tool's robustness target (~0.5s).
constexpr int READ_TIMEOUT_US = 500000;

// How long connect() may block. A missing/unreachable board fails fast.
constexpr int CONNECT_TIMEOUT_S = 5;

// SO_RCVTIMEO backstop in case select() returns readable with no data.
constexpr int SOCKET_RCVTIMEO_MS = 1000;

// Guard against runaway line buffering on a noisy/garbage stream.
constexpr std::size_t MAX_PENDING_LEN = 4096;

// Resolve host:port into a connected TCP socket, or -1 on failure. Uses a
// bounded connect() so an unreachable board can't hang the CLI.
int connectToHost(const std::string& host, int port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;      // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;

    std::string portStr = std::to_string(port);

    addrinfo* result = nullptr;
    int rc = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result);
    if (rc != 0 || result == nullptr) {
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

TCPTransport::TCPTransport(std::string host, int port, std::string adapterProtocol,
                           std::shared_ptr<ITransportOutput> output)
    : host_(std::move(host))
    , port_(port)
    , adapterProtocol_(std::move(adapterProtocol))
    , output_(std::move(output)) {
}

TCPTransport::~TCPTransport() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

bool TCPTransport::sendAll(int fd, const std::string& data) noexcept {
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
        // Brief settle so the adapter can process each command.
        std::this_thread::sleep_for(
            std::chrono::milliseconds(cmd.delayMs > 0 ? cmd.delayMs : 50));
    }
    return true;
}

bool TCPTransport::connectAndAuth() {
    closeConnection();
    fd_ = connectToHost(host_, port_);
    if (fd_ < 0) return false;

    struct timeval rtv{};
    rtv.tv_sec = SOCKET_RCVTIMEO_MS / 1000;
    rtv.tv_usec = (SOCKET_RCVTIMEO_MS % 1000) * 1000;
    (void)setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));

    // Authenticate: send token, expect "OK" back
    std::string authCmd = "AUTH " TCP_AUTH_TOKEN "\r";
    if (!sendAll(fd_, authCmd)) { closeConnection(); return false; }
    char authResp[64] = {};
    int n = recv(fd_, authResp, sizeof(authResp) - 1, 0);
    if (n <= 0 || std::string(authResp, n).find("OK") == std::string::npos) {
        closeConnection(); return false;
    }

    if (adapterProtocol_ == "elm327") {
        if (!sendElm327Init(fd_)) { closeConnection(); return false; }
    }
    return true;
}

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
    output_->out("[tcp] Monitoring " + host_ + ":" + std::to_string(port_));
    pending_.reserve(256);
    return true;
}

bool TCPTransport::isOpen() const noexcept {
    return opened_ && fd_ >= 0 && !exhausted_;
}

std::optional<std::string> TCPTransport::nextLine() {
    if (!opened_ || fd_ < 0 || exhausted_) {
        return std::nullopt;
    }

    // First, satisfy the request from any already-buffered complete line.
    while (true) {
        std::size_t end = pending_.find_first_of("\r\n");
        if (end == std::string::npos) {
            break;  // no complete line buffered — need more bytes
        }
        std::string line(pending_, 0, end);
        pending_.erase(0, end + 1);
        // We return the line verbatim (the normaliser tolerates a trailing '\r'
        // already stripped here by the terminator split). An empty line from a
        // "\r\r" banner sequence is delivered as "" — the normaliser Skip's it.
        return line;
    }

    // Read more bytes from the socket with a bounded select() so we never hang.
    while (true) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(fd_, &readSet);

        struct timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = READ_TIMEOUT_US;

        int ready = select(fd_ + 1, &readSet, nullptr, nullptr, &tv);
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
            if (g_stopRequested.load()) {
                exhausted_ = true;
                return std::nullopt;
            }
            continue;
        }

        char buffer[256];
        ssize_t n = recv(fd_, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            // Peer closed (0) or error (<0): attempt reconnect with backoff.
            output_->err("[tcp] disconnected from " + host_ + ":" + std::to_string(port_) + " — reconnecting...");
            closeConnection();
            while (retryCount_ < MAX_RETRIES) {
                if (g_stopRequested.load()) {
                    output_->err("[tcp] reconnect cancelled (stop requested)");
                    exhausted_ = true;
                    return std::nullopt;
                }
                retryCount_++;
                output_->err("[tcp] reconnect attempt " + std::to_string(retryCount_) + " in " + std::to_string(RETRY_DELAY_MS) + "ms");
                std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));
                if (connectAndAuth()) {
                    output_->out("[tcp] reconnected to " + host_ + ":" + std::to_string(port_));
                    retryCount_ = 0;
                    break;
                }
            }
            if (fd_ < 0) {
                output_->err("[tcp] reconnect failed after " + std::to_string(MAX_RETRIES) + " attempts — giving up");
                exhausted_ = true;
                return std::nullopt;
            }
            continue;  // reconnected — resume reading
        }

        pending_.append(buffer, static_cast<std::size_t>(n));

        // Defensive cap so a peer that never sends a line ending can't grow
        // the buffer without bound.
        if (pending_.size() > MAX_PENDING_LEN) {
            pending_.clear();
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
