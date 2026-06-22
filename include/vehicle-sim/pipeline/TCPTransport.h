#pragma once

#include "vehicle-sim/pipeline/ITransport.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace vehicle_sim::pipeline {

/**
 * Live raw TCP transport: opens a POSIX socket to a CAN-bridge (e.g. the ESP32
 * firmware's TCP server), reads the stream line by line, and delivers each
 * line to the normaliser. This is a PURE TRANSPORT — it knows nothing about
 * DBC, decode, or VehicleSignal (replacing the old TcpSignalSource which baked
 * a DBCTranslationService& into the transport, a lifetime hazard).
 *
 * Reads are ALWAYS gated by select() with a short timeout, so nextLine() can
 * never block indefinitely: it returns nullopt promptly on disconnect, EOF, or
 * a peer that goes quiet. This makes the transport safe to drive from the
 * single-threaded replay loop.
 *
 * RAW protocol (default): the bridge streams "<ID> <D0> ... <D7>\r" lines with
 * NO AT-init — bytes are forwarded verbatim. When adapterProtocol is "elm327",
 * the transport sends the shared ELM327 CAN-monitor init sequence on connect
 * (ATZ/ATE0/ATSP6/ATH1/ATMA) before reading; the ELM327 *normaliser* is a
 * later task (#18), so today elm327 only changes the connect handshake.
 *
 * Portability: POSIX sockets only (macOS, iOS, Linux).
 */
class TCPTransport final : public ITransport {
public:
    /**
     * @param host             IPv4/hostname of the CAN-bridge.
     * @param port             TCP port (firmware default 3333).
     * @param adapterProtocol  "raw" (no init) or "elm327" (send AT-init).
     */
    TCPTransport(std::string host, int port, std::string adapterProtocol = "raw");

    ~TCPTransport() override;

    TCPTransport(const TCPTransport&) = delete;
    TCPTransport& operator=(const TCPTransport&) = delete;
    TCPTransport(TCPTransport&&) = delete;
    TCPTransport& operator=(TCPTransport&&) = delete;

    bool open() override;
    [[nodiscard]] bool isOpen() const noexcept override;
    std::optional<std::string> nextLine() override;

    /**
     * Request that nextLine() return nullopt at the next select() timeout.
     * Used by the live run context's signal handler to stop a live stream
     * cleanly without hanging. Safe to call from a signal handler (atomic).
     */
    static void requestStop() noexcept;
    /** Reset the stop flag (for tests / repeated runs). */
    static void resetStop() noexcept;

private:
    bool sendAll(int fd, const std::string& data) noexcept;
    bool sendElm327Init(int fd) noexcept;
    bool connectAndAuth();
    void closeConnection() noexcept;

    std::string host_;
    int port_;
    std::string adapterProtocol_;
    int fd_ = -1;
    bool opened_ = false;
    bool exhausted_ = false;
    // Reconnect state
    int retryCount_ = 0;
    static constexpr int MAX_RETRIES = 0x7FFFFFFF;  // ~2 billion — effectively unlimited
    static constexpr int RETRY_DELAY_MS = 1000;
    // Accumulated bytes not yet terminated by a line ending.
    std::string pending_;
};

} // namespace vehicle_sim::pipeline
