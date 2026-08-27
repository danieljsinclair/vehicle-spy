#pragma once

#include "vehicle-sim/pipeline/ISocket.h"
#include "vehicle-sim/pipeline/StopToken.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace vehicle_sim::pipeline {

// Why a read failure is being reported to the owner. TCPTransport uses the
// kind to decide whether to run reconnect-or-discovery logic (RecvFailure)
// or simply mark the transport exhausted and give up (SelectError,
// StopRequested) — matching the OLD nextLine() semantics where both
// select-error and stop-during-silent-poll set exhausted_=true.
enum class ReadFailureKind { RecvFailure, SelectError, StopRequested };

// Callback injected by TCPTransport into TcpReader::nextLine().
// Invoked on every read failure with the failure kind. Returns true to
// signal that reconnection succeeded (loop should resume), false to give up.
// For RecvFailure the callback decides via reconnect-or-discovery; for
// SelectError/StopRequested the owner should mark exhausted and return
// false (no reconnect hunt on those paths).
using ReadFailureCallback = std::function<bool(ReadFailureKind)>;

// Low-level TCP I/O reader: owns the pending_ buffer + socket I/O seam.
// The owning TCPTransport injects a ReadFailureCallback so that TcpReader
// can trigger reconnection logic without creating a direct cycle.
class TcpReader final {
public:
    TcpReader(std::shared_ptr<ISocket> socket,
              std::shared_ptr<StopToken> stop,
              int readTimeoutUs,
              ReadFailureCallback onFailure = {});  // callback takes ReadFailureKind

    ~TcpReader() = default;

    TcpReader(const TcpReader&) = delete;
    TcpReader& operator=(const TcpReader&) = delete;
    TcpReader(TcpReader&&) = delete;
    TcpReader& operator=(TcpReader&&) = delete;

    // True when the injected stop signal has been requested.
    bool shouldStop() const;

    // If a complete line (terminated by '\r' or '\n') is already buffered in
    // pending_, remove and return it; otherwise return nullopt.
    std::optional<std::string> takeBufferedLine();

    // Wait up to one bounded poll for the socket to become readable. Returns
    // the select() ready count (negative on error).
    int selectReady() const;

    // recv() one chunk (256 bytes) into pending_ (with MAX_PENDING_LEN cap).
    // Returns the recv byte count — <= 0 means peer-closed/error.
    ssize_t readSocketIntoPending();

    // Drive nextLine(): returns the next complete line, or nullopt on EOF,
    // error, or stop. On read failure invokes onFailure_ (if set) so the
    // owning TCPTransport can run reconnect logic.
    std::optional<std::string> nextLine();

    // Strip leading characters matching `chars` from pending_. Used by
    // TCPTransport::performHeloHandshake to remove trailing ESP32 prompt
    // bytes ("\r\r>") left after the ACK line is consumed.
    void stripLeading(std::string_view chars) {
        if (const std::size_t firstReal = pending_.find_first_not_of(chars);
            firstReal != std::string::npos) {
            pending_.erase(0, firstReal);
        } else {
            pending_.clear();
        }
    }

    // Clear any buffered bytes (called by TCPTransport before a fresh connect).
    void clearPending() noexcept { pending_.clear(); }

    // Reserve buffer space (called by TCPTransport after a successful open).
    void reservePending(std::size_t n) { pending_.reserve(n); }

private:
    // I/O seams (injected, shared with the live run-context).
    std::shared_ptr<ISocket> socket_;
    std::shared_ptr<StopToken> stop_;

    int readTimeoutUs_;

    // Callback invoked when nextLine() encounters a read failure (select
    // error, EOF, or peer-close). The callback returns true to signal
    // reconnected/resume, false to give up. Empty = no callback (failure
    // → GiveUp).
    ReadFailureCallback onFailure_;

    // Accumulated bytes not yet terminated by a line ending.
    std::string pending_;
};

} // namespace vehicle_sim::pipeline
