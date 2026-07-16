#include "vehicle-sim/pipeline/PosixSocket.h"

#include <cerrno>
#include <chrono>
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

// select() read timeout floor — mirrors the original READ_TIMEOUT_US_FLOOR.
constexpr int READ_TIMEOUT_US_FLOOR = 1;  // 1us poll floor; unused here but kept
                                          // for parity documentation.

constexpr int CONNECT_TIMEOUT_S = 5;      // How long connect() may block before failing

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
//
// Verbatim port of the original waitForConnect().
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
// an in-flight connect. `stop` is nullable: callers without a stop token pass
// nullptr and get the bounded-timeout behavior without cancellation.
//
// Verbatim port of the original connectToHost().
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
        // can poll for writability (and the stop flag) in bounded chunks.
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

int PosixSocket::connect(const std::string& host, int port, const StopToken* stop) {
    fd_ = connectToHost(host, port, stop);
    return fd_;
}

ssize_t PosixSocket::recv(char* buf, size_t len) {
    return ::recv(fd_, buf, len, 0);
}

int PosixSocket::selectReadable(int timeoutUs) {
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(fd_, &readSet);

    const int pollUs = std::max(timeoutUs, READ_TIMEOUT_US_FLOOR);
    struct timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = pollUs;

    return select(fd_ + 1, &readSet, nullptr, nullptr, &tv);
}

void PosixSocket::close() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool PosixSocket::setRecvTimeout(int ms) {
    if (fd_ < 0) return false;
    struct timeval rtv{};
    rtv.tv_sec = ms / 1000;
    rtv.tv_usec = (ms % 1000) * 1000;
    (void)setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));
    return true;
}

bool PosixSocket::sendAll(std::string_view data) {
    if (fd_ < 0) return false;
    std::size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd_, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

} // namespace vehicle_sim::pipeline
