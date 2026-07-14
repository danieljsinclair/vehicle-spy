#pragma once

#include "vehicle-sim/pipeline/ITransport.h"
#include "vehicle-sim/pipeline/ITransportOutput.h"
#include "vehicle-sim/pipeline/StopToken.h"
#include "vehicle-sim/discovery/IDiscoveryListener.h"

#include <atomic>
#include <chrono>
#include <functional>
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
 * Socket/read-timing configuration for TCPTransport. A cohesive domain grouping
 * of the three values that govern read behaviour and are always co-passed; held
 * as one named, defaultable config so the transport ctor stays below the S107
 * parameter threshold and a tuner varies timing as a unit.
 *
 *   - readTimeoutUs       Max select() wait (microseconds) before re-checking
 *                         the stop flag. Default 500000 (0.5s).
 *   - atInitDelayMs       Inter-command pacing (ms) between ELM327 AT-init
 *                         commands. -1 = use each command's own delayMs
 *                         (production). >= 0 overrides every command's delay.
 *   - socketRecvTimeoutMs SO_RCVTIMEO (ms) for recv(). Default 1000ms.
 */
struct TcpReadTiming {
    int readTimeoutUs = 500000;
    int atInitDelayMs = -1;
    int socketRecvTimeoutMs = 1000;
};

/**
 * Factory for the discovery listener used by the hunt-on-disconnect resilience
 * path (enterHuntingState). The hunt constructs a FRESH listener per call (to
 * mirror the original stack-local UDPDiscovery and its empty per-hunt dedup
 * state), so the injection is a factory rather than a shared instance.
 *
 * An empty/default-constructed factory (the ctor default) means "use the real
 * UDPDiscovery" — resolved lazily inside the host-only enterHuntingState(), so
 * the production TCPTransport never references UDPDiscovery outside the
 * host-only translation unit and the header stays iOS-portable. Tests pass a
 * factory returning a no-op IDiscoveryListener so the discovery-win path is
 * unreachable and the hunt is hermetic/deterministic regardless of any device
 * on the LAN.
 */
using DiscoveryListenerFactory =
    std::function<std::unique_ptr<discovery::IDiscoveryListener>()>;

/**
 * Sleep abstraction for the hunt backoff loop. The ONE place TCPTransport parks
 * between reconnect attempts: backoffThenAttemptReconnect() sleeps a
 * delayMs-long backoff, sliced into checkIntervalMs chunks so a discovery win
 * or a requestStop() is observed within one slice (not after the full delay).
 *
 * Production uses RealBackoffSleeper, which forwards to
 * std::this_thread::sleep_for per slice — byte-for-byte identical timing to the
 * pre-seam code (the load-bearing reconnect cadence is unchanged on the real
 * path). Tests inject an instant sleeper so the backoff advances in ~0 ms,
 * making the hunting suites deterministic and fast (was ~49 s wall-clock).
 *
 * The sleeper sees ONLY the sleep; discovery/connect/socket I/O stay real
 * (loopback TCP is already sub-ms). SRP: this seam exists because the
 * established util::IClock::waitFor() requires a std::condition_variable to
 * park on, while the backoff loop checks atomics (discoveryFound/stop_) — a
 * cv-free sliced sleep is the minimal, behaviour-preserving seam.
 */
class IBackoffSleeper {
public:
    virtual ~IBackoffSleeper() = default;

    /**
     * Sleep for up to totalMs, checking shouldStop() every sliceMs and returning
     * early when it becomes true. Mirrors the original loop's contract: the
     * caller observes stop/discovery within one slice, not after the full delay.
     *
     * @param totalMs      Total backoff duration in milliseconds.
     * @param sliceMs      Re-check interval in milliseconds.
     * @param shouldStop   Predicate; true aborts the remaining sleep promptly.
     */
    virtual void sleepSliced(int totalMs, int sliceMs,
                             const std::function<bool()>& shouldStop) = 0;
};

/**
 * Production sleeper: real std::this_thread::sleep_for per slice. Timing-
 * identical to the pre-seam backoff (the reconnect cadence is load-bearing).
 */
class RealBackoffSleeper final : public IBackoffSleeper {
public:
    void sleepSliced(int totalMs, int sliceMs,
                     const std::function<bool()>& shouldStop) override;
};

/**
 * Cohesive config for the host-only hunt-on-disconnect resilience path. Groups
 * the two DI seams that govern HOW a hunt proceeds (not WHETHER it runs):
 *   - discoveryFactory: the per-hunt discovery listener (empty = real
 *     UDPDiscovery; tests inject a no-op/fixed listener for hermeticity).
 *   - backoffSleeper: the backoff sleep strategy (RealBackoffSleeper by
 *     default; tests inject an instant sleeper for fast, deterministic hunts).
 *
 * Bundled into one struct so the TCPTransport ctor stays at 7 params (cpp:S107
 * threshold) — both fields are hunt-resilience concerns, always co-passed, and
 * default-constructible to the production-real path (zero behavior change).
 */
struct HuntResilienceConfig {
    /** Factory for the per-hunt discovery listener (empty = real UDPDiscovery). */
    DiscoveryListenerFactory discoveryFactory;
    /** Sleep strategy for the hunt backoff (default = RealBackoffSleeper). */
    std::shared_ptr<IBackoffSleeper> backoffSleeper = std::make_shared<RealBackoffSleeper>();
};

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
     * @param host            IPv4/hostname of the CAN-bridge.
     * @param port            TCP port (firmware default 3333).
     * @param adapterProtocol "raw" (no init) or "elm327" (send AT-init).
     * @param output          Where to emit human-readable status/error lines.
     * @param timing          Socket/read-timing config (see TcpReadTiming);
     *                        defaults to production values. Injectable so tests
     *                        can pass tiny values and see requestStop()/recv
     *                        timeouts in ~0 ms instead of the full poll.
     * @param stop            Shared cooperative stop signal (injected, owned by
     *                        the live run-context); must exist at construction
     *                        for the hot loop.
     * @param hunt           Hunt-on-disconnect resilience config (see
     *                       HuntResilienceConfig): the per-hunt discovery
     *                       listener factory + the backoff sleep strategy.
     *                       Defaults to the real UDPDiscovery +
     *                       RealBackoffSleeper (production timing). Tests inject
     *                       a no-op factory + instant sleeper for hermetic,
     *                       fast hunting (backoffs advance in ~0 ms).
     */
    TCPTransport(std::string_view host, int port, std::string_view adapterProtocol = "raw",
                 std::shared_ptr<ITransportOutput> output = std::make_shared<StdOut>(),
                 TcpReadTiming timing = TcpReadTiming{},
                 std::shared_ptr<StopToken> stop = std::make_shared<StopToken>(),
                 HuntResilienceConfig hunt = HuntResilienceConfig{});

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
     * The shared StopToken (injected at construction, owned by the live
     * run-context) is the cooperative stop signal; the signal handler flips it
     * via SignalStopBroker. requestStop()/reset() are async-signal-safe atomic
     * ops on the token.
     */
    void requestStop() noexcept { stop_->requestStop(); }
    /** Reset the stop token (for tests / repeated runs). */
    void resetStop() noexcept { stop_->reset(); }

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
    bool sendAll(int fd, std::string_view data) const noexcept;
    bool sendElm327Init(int fd) noexcept;
    // Falls back to DEFAULT_PER_COMMAND_DELAY_MS (50ms) when no positive value is supplied
    int perCommandDelayMs(int cmdDelayMs) const;
    bool connectAndAuth();
    void closeConnection() noexcept;
    bool performHeloHandshake();

    // Under VEHICLE_SIM_HUNTING_ENABLED the spec-first test harness drives the
    // production enterHuntingState() directly and asserts host_ switching. Expose
    // exactly these two members publicly; the rest stay private. (The test's
    // `#define private public` + re-include is a no-op under #pragma once, so the
    // public exposure must live in the header itself.)
    friend class TCPTransportHuntingTest;
#if defined(VEHICLE_SIM_HUNTING_ENABLED)
public:
    bool enterHuntingState();  // Retry old IP + listen for UDP discovery simultaneously (host-only)
    std::string host_;
private:
    // Resolve the per-hunt discovery listener from the injected factory, or a
    // real UDPDiscovery when no factory was injected. Host-only (references
    // UDPDiscovery in the .cpp); kept as a helper so enterHuntingState stays
    // focused on the hunt rather than listener construction.
    std::unique_ptr<discovery::IDiscoveryListener> resolveDiscoveryListener();
    // Background discovery loop extracted from enterHuntingState: start the
    // listener, poll for UDP broadcasts, and on the first target device at a
    // new IP record its address + flip discoveryFound. Host-only. Takes its
    // mutable state by reference so it owns NO captures (clears S1188/S3608/
    // S5019 that the old [&]-lambda tripped) and enterHuntingState stays a
    // small orchestrator.
    void runDiscovery(discovery::IDiscoveryListener& hunter,
                      const std::atomic<bool>& shouldStopDiscovery,
                      std::atomic<bool>& discoveryFound,
                      std::string& discoveredIp);
    // True iff a discovered device is the one we want: deviceIdHex_ empty
    // (match-all), OR the discovered hex exactly matches, OR the first 8 hex
    // chars match (4-byte prefix), AND the device is NOT at the current host_.
    // Pure predicate; preserves the G3-pinned 8-char-prefix branch verbatim.
    bool isTargetDevice(const discovery::DiscoveredDevice& device,
                        std::string_view discoveredHex) const;
    // One backoff sleep (100ms-sliced, G8) + one old-IP reconnect attempt.
    // Returns loopDone; sets reconnected=true on an old-IP win.
    bool backoffThenAttemptReconnect(int delayMs, const std::atomic<bool>& discoveryFound,
                                     bool& reconnected);
    // Post-loop outcome: old-IP win, discovery-win (switch host_ then connect,
    // G11-pinned), or give-up. Resets retryCount_ on success.
    bool finalizeHunt(const std::atomic<bool>& discoveryFound,
                      std::string_view discoveredIp, bool reconnected);
#else
#if !defined(BUILD_IOS) && (!defined(TARGET_OS_IPHONE) || TARGET_OS_IPHONE == 0)
    bool enterHuntingState();  // Retry old IP + listen for UDP discovery simultaneously (host-only)
    std::unique_ptr<discovery::IDiscoveryListener> resolveDiscoveryListener();
    void runDiscovery(discovery::IDiscoveryListener& hunter,
                      const std::atomic<bool>& shouldStopDiscovery,
                      std::atomic<bool>& discoveryFound,
                      std::string& discoveredIp);
    bool isTargetDevice(const discovery::DiscoveredDevice& device,
                        std::string_view discoveredHex) const;
    bool backoffThenAttemptReconnect(int delayMs, const std::atomic<bool>& discoveryFound,
                                     bool& reconnected);
    bool finalizeHunt(const std::atomic<bool>& discoveryFound,
                      std::string_view discoveredIp, bool reconnected);
#endif
    std::string host_;
#endif
    int port_;
    std::string adapterProtocol_;
    std::shared_ptr<ITransportOutput> output_;
    std::shared_ptr<StopToken> stop_;
    // Hunt-on-disconnect resilience config: the per-hunt discovery listener
    // factory (empty = real UDPDiscovery) + the backoff sleep strategy. Only
    // dereferenced inside the host-only enterHuntingState().
    HuntResilienceConfig hunt_;
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

    // stop_ is the injected cooperative stop signal (shared with the live
    // run-context); re-load it per call rather than caching across the read loop.
    bool shouldStop() const;

    // Outcome of recovering from a peer-close/error read inside nextLine().
    // Resume = reconnected, nextLine() continues its read loop; GiveUp = hunt
    // failed / iOS build / stop requested, nextLine() returns nullopt.
    enum class ReadRecovery { Resume, GiveUp };

    // Format the "disconnected ... reconnecting" message (deviceIdHex_-set
    // variant tags the ESP32 id; the empty variant omits it).
    std::string formatDisconnectMessage() const;

    // Handle a recv() <= 0: if stopped give up immediately; otherwise emit the
    // disconnect message, closeConnection(), and (host build) enterHuntingState
    // for reconnect-or-discovery, or (iOS build) give up. Sets exhausted_ on
    // every GiveUp path. nextLine() maps Resume -> continue, GiveUp -> nullopt.
    ReadRecovery handleReadFailure();
};

} // namespace vehicle_sim::pipeline
