#pragma once

#include "vehicle-sim/pipeline/ISocket.h"

#include <string>
#include <string_view>

namespace vehicle_sim::pipeline {

/**
 * Production ISocket: verbatim behavior-port of TCPTransport's former direct
 * POSIX calls (connectToHost / waitForConnect / send / recv / select).
 *
 * ZERO behavior change on the production path — the backoff/reconnect timing
 * is load-bearing, so the nonblocking-connect-polled-in-100ms-slices contract
 * (EINPROGRESS handling, SO_ERROR check, restore-to-blocking) is reproduced
 * exactly. The characterization tests (which assert real behavior) prove this
 * by staying green.
 */
class PosixSocket final : public ISocket {
public:
    PosixSocket() = default;

    int connect(const std::string& host, int port, const StopToken* stop) override;
    ssize_t recv(char* buf, size_t len) override;
    int selectReadable(int timeoutUs) override;
    void close() noexcept override;
    bool setRecvTimeout(int ms) override;
    bool sendAll(std::string_view data) override;

private:
    int fd_ = -1;
};

} // namespace vehicle_sim::pipeline
