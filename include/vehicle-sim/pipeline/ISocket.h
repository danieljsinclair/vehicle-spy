#pragma once

#include "vehicle-sim/pipeline/StopToken.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace vehicle_sim::pipeline {

/**
 * Socket abstraction for TCPTransport's network I/O.
 *
 * THE sole network seam for this transport. Today the production path is
 * PosixSocket (a verbatim port of the old connectToHost/waitForConnect +
 * send/recv/select logic); tests inject a FakeSocket that scripts the bytes —
 * no real socket, no real connect/recv/select, no real loopback server.
 *
 * Why this exists (and not just inline POSIX in TCPTransport):
 *   The hunting/nextLine unit suites were ~39 s wall-clock because they drove
 *   REAL loopback TCP + a REAL backoff sleep. Mocking the socket is the only
 *   way to get the suite under 100 ms deterministically. There is exactly one
 *   socket interface; production uses PosixSocket, tests use FakeSocket.
 *
 * The connect() contract mirrors the original nonblocking connect polled in
 * 100 ms slices honouring a stop predicate, with EINPROGRESS + SO_ERROR
 * handling — so the production reconnect cadence (the load-bearing property
 * the hunting loop depends on for interruptible connects) is preserved
 * byte-for-byte by PosixSocket.
 */
class ISocket {
public:
    virtual ~ISocket() = default;

    /**
     * Resolve + connect to host:port. Returns a connected OS fd >= 0 on
     * success, or -1 on failure/refusal/unreachable/stop.
     *
     * Production (PosixSocket) reproduces the original behavior: nonblocking
     * connect() polled in 100 ms slices honouring `stop`, EINPROGRESS handling,
     * SO_ERROR check, then a restore to blocking mode. `stop` is nullable.
     */
    virtual int connect(const std::string& host, int port, const StopToken* stop) = 0;

    /**
     * Read up to `len` bytes into `buf`. Returns >0 bytes read, 0 on peer
     * close (EOF), or <0 on error. Mirrors POSIX recv()'s contract exactly so
     * TCPTransport's framing/handshake logic is unchanged.
     */
    virtual ssize_t recv(char* buf, size_t len) = 0;

    /**
     * Block until the socket is readable or `timeoutUs` elapses. Returns
     * >0 when readable, 0 on timeout, <0 on error. Mirrors the original
     * `select(fd_ + 1, read, nullptr, nullptr, &tv)` call.
     */
    virtual int selectReadable(int timeoutUs) = 0;

    /** Close the connection (no-op if not connected). Idempotent. */
    virtual void close() noexcept = 0;

    /**
     * Set SO_RCVTIMEO on the connected fd (used by connectAndAuth to bound the
     * handshake-stage blocking recv()s). Returns true on success.
     */
    virtual bool setRecvTimeout(int ms) = 0;

    /**
     * Send all bytes (AUTH/ATI/ATHELO/ELM commands). Returns true on success
     * (every byte sent), false on a send error. Mirrors TCPTransport::sendAll.
     */
    virtual bool sendAll(std::string_view data) = 0;
};

} // namespace vehicle_sim::pipeline
