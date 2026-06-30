#pragma once

#include "vehicle-sim/pipeline/ITransport.h"
#include "vehicle-sim/pipeline/ITransportOutput.h"

#include <memory>
#include <string>
#include <string_view>

// TCP auth token — injected at build time via compiler define
// Makefile passes -DTCP_AUTH_TOKEN=\"...\" for firmware; CMakeLists.txt sets it for native
#ifndef TCP_AUTH_TOKEN
#define TCP_AUTH_TOKEN "vehicle-sim-2026"
#endif

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
     * @param host               IPv4/hostname of the CAN-bridge.
     * @param port               TCP port (firmware default 3333).
     * @param adapterProtocol    "raw" (no init) or "elm327" (send AT-init).
     * @param output             Where to emit human-readable status/error lines.
     * @param readTimeoutUs      Max wait (microseconds) for a select() read before
     *                           re-checking the stop flag. Defaults to 500000 (0.5s)
     *                           — matches the capture tool's robustness target.
     *                           Injectable so tests can pass a tiny value and see a
     *                           requestStop() in ~0 ms instead of waiting out the
     *                           full production poll. Production default unchanged.
     * @param atInitDelayMs      Inter-command pacing (milliseconds) between ELM327
     *                           AT-init commands. -1 (default) means use each
     *                           command's own cmd.delayMs (production behaviour —
     *                           a real adapter needs the settle time). Any value
     *                           >= 0 overrides every command's delay to that value,
     *                           so tests can pass 0 and skip the ~700ms of pacing.
     * @param socketRecvTimeoutMs Socket SO_RCVTIMEO (milliseconds) for recv().
     *                           Defaults to 1000ms (production). Injectable so
     *                           tests can pass 1ms and avoid waiting the full
     *                           second on auth/handshake recv timeouts. Production
     *                           default unchanged.
     */
    TCPTransport(std::string_view host, int port, std::string_view adapterProtocol = "raw",
                 std::shared_ptr<ITransportOutput> output = std::make_shared<StdOut>(),
                 int readTimeoutUs = 500000,
                 int atInitDelayMs = -1,
                 int socketRecvTimeoutMs = 1000);

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

    /**
     * HELO/ACK pre-flight: send ATHELO and parse ACK response.
     * Returns true if HELO succeeded, false otherwise.
     * Populates deviceId (16 bytes) with device's unique identifier.
     */
    bool sendHeloAndParseAck(std::array<uint8_t, 16>& deviceId);

    /**
     * Get the device ID from HELO handshake (32 hex chars).
     * Returns empty string if HELO hasn't completed yet.
     */
    const std::string& getDeviceId() const noexcept { return deviceIdHex_; }

    // Connection/reconnect constants (public for utility function access)
    static constexpr int MAX_RETRIES = 60;              // Bounded retry: ~60s total wait
    static constexpr int BASE_RETRY_DELAY_MS = 1000;   // Initial reconnect delay
    static constexpr int MAX_RETRY_DELAY_MS = 10000;   // Max exponential backoff
    // Legacy alias for compatibility
    static constexpr int RETRY_DELAY_MS = BASE_RETRY_DELAY_MS;

    // L3: tag literals shared across the C++ layer. iOS has its own equivalents.
    static constexpr const char* kClientTag = " [CLIENT]";
    static constexpr const char* kEsp32TagPrefix = "ESP32";

private:
    bool sendAll(int fd, std::string_view data) noexcept;
    bool sendElm327Init(int fd) noexcept;
    // Falls back to DEFAULT_PER_COMMAND_DELAY_MS (50ms) when no positive value is supplied
    int perCommandDelayMs(int cmdDelayMs) const;
    bool connectAndAuth();
    void closeConnection() noexcept;
    bool performHeloHandshake();
#if !defined(BUILD_IOS) && !defined(TARGET_OS_IPHONE)
    bool enterHuntingState();  // Retry old IP + listen for UDP discovery simultaneously (host-only)
#endif

    std::string host_;
    int port_;
    std::string adapterProtocol_;
    std::shared_ptr<ITransportOutput> output_;
    int readTimeoutUs_ = 500000;
    int atInitDelayMs_ = -1;
    int socketRecvTimeoutMs_ = 1000;
    int fd_ = -1;
    bool opened_ = false;
    bool exhausted_ = false;
    // Reconnect state
    int retryCount_ = 0;
    // Device ID from HELO handshake (32 hex chars, empty until HELO succeeds)
    std::string deviceIdHex_;
    // Accumulated bytes not yet terminated by a line ending.
    std::string pending_;

    // True when nextLine() may legitimately read more: the transport was
    // opened, holds a live descriptor, and has not been marked EOF/exhausted.
    bool canRead() const noexcept { return opened_ && fd_ >= 0 && !exhausted_; }

    // If a complete line (terminated by '\r' or '\n') is already buffered in
    // pending_, remove and return it; otherwise return nullopt. find_first_of
    // splits on each '\r' and each '\n' individually, so a "\r\n" sequence
    // yields one line plus a following empty banner line — do NOT collapse.
    std::optional<std::string> takeBufferedLine();

    // Wait up to one bounded poll for fd_ to become readable. Returns the
    // select() ready count (negative on error). EINTR retry and the
    // exhausted_/stop mutations stay in nextLine(), the caller.
    int selectReady() const;

    // recv() one chunk (256 bytes) into a local buffer and append it to
    // pending_ (with the MAX_PENDING_LEN runaway cap). Returns the recv byte
    // count — <= 0 means peer-closed/error, handled by the caller. Line
    // extraction from pending_ stays in nextLine().
    ssize_t readSocketIntoPending();

    // g_stopRequested is process-global (set by a signal handler); re-load it
    // per call rather than caching across the read loop.
    bool shouldStop() const;
};

} // namespace vehicle_sim::pipeline
