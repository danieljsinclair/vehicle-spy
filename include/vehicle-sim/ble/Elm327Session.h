#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "vehicle-sim/boundary/OBD2Protocol.h"
#include "vehicle-sim/domain/VehicleDetector.h"

namespace vehicle_sim {

/**
 * @brief Cohesive host interface supplying the transport facilities an
 *        Elm327Session needs from its owner.
 *
 * This is a small role interface (ISP), not a param-bag: it exposes exactly
 * the three concerns the session depends on — sending bytes over the BLE
 * transport, reading connection state, and delivering parsed data to the
 * consumer. The concrete host (BLEManagerBase) implements it; the session
 * never reaches back into the host's broader surface.
 */
class Elm327SessionHost {
public:
    virtual ~Elm327SessionHost() = default;

    /// Send an ASCII command to the ELM327 adapter as raw BLE bytes.
    virtual void sessionSendAscii(std::string_view command) = 0;

    /// Whether a device is currently connected (the polling loop gates on it).
    virtual bool sessionIsConnected() const = 0;

    /// Deliver parsed OBD2/CAN binary data to the consumer callback.
    virtual void sessionDeliverParsed(const std::vector<uint8_t>& data) = 0;
};

/**
 * @brief The ELM327 OBD2 session role — protocol, polling, CAN monitor, VIN
 *        query and vehicle auto-detection.
 *
 * Extracted from BLEManagerBase (god-class decomposition, S3656/S1448). This
 * component fuses the two responsibilities that had zero subclass surface:
 *   - role 4: the OBD2/ELM327 protocol layer (AT command sequencing, prompt
 *     handling, response parsing, the OBD2Protocol handler);
 *   - role 5: OBD2 polling, CAN monitor mode, VIN query and auto-detection.
 *
 * It owns its own state (the protocol handler, vehicle detector, prompt
 * sequencing, polling thread and CAN-mode flag) and is driven by its host
 * through a Elm327SessionHost reference. Behaviour is identical to the
 * former BLEManagerBase implementation; only the layout changed.
 */
class Elm327Session {
public:
    /// Polling timing constants (preserved from BLEManagerBase verbatim).
    static constexpr int DEFAULT_POLLING_INTERVAL_MS = 200;
    static constexpr int PROMPT_TIMEOUT_MS = 2000;           // max wait for '>' prompt before skipping PID
    static constexpr int POST_CONNECT_SETUP_DELAY_MS = 500;  // wait for characteristic notifications

    explicit Elm327Session(Elm327SessionHost& host);

    // The session owns a polling thread and synchronization primitives, so it
    // is non-copyable and non-movable (Rule of Five via =delete).
    Elm327Session(const Elm327Session&) = delete;
    Elm327Session& operator=(const Elm327Session&) = delete;
    Elm327Session(Elm327Session&&) = delete;
    Elm327Session& operator=(Elm327Session&&) = delete;
    ~Elm327Session();

    // === ELM327 / OBD2 init & detection ====================================
    /// Send the full ELM327 init sequence (prompt-driven). Always emits every
    /// command once; returns true unconditionally (matching legacy behaviour).
    bool initializeELM327();

    /// Initialise the adapter then run vehicle auto-detection.
    std::optional<domain::VehicleDetectionResult> initializeOBD2WithDetection();

    /// Route incoming ASCII data into the OBD2 protocol handler.
    void processOBD2Data(std::string_view asciiData);

    // === OBD2 live-data polling ============================================
    /// Begin prompt-driven polling of the standard PIDs. Idempotent: a second
    /// call while polling is active is a no-op.
    void startOBD2Polling(int interval_ms = DEFAULT_POLLING_INTERVAL_MS);

    /// Stop polling and join the polling thread.
    void stopOBD2Polling();

    // === CAN monitor mode ==================================================
    /// Send the CAN monitor init sequence and enter CAN mode.
    bool initializeCANMonitor();

    /// Enter CAN monitor mode (flag-set only; frames stream via notifications).
    void startCANMonitor(int interval_ms = 200);

    /// Leave CAN monitor mode and re-assert ATMA to end the monitor stream.
    void stopCANMonitor();

    // === VIN query =========================================================
    /// Clear CAN mode, reset the detector, and send the VIN-query init sequence
    /// (ATSP6 — specific CAN protocol, not auto-probe).
    bool initializeForVINQuery();

    /// Query the VIN (Mode 09 PID 02). Returns nullopt on prompt timeout or if
    /// the detector has no VIN when the prompt arrives.
    std::optional<std::string> queryVIN(int timeout_ms = PROMPT_TIMEOUT_MS * 2 + 1000);

    // === Data path (driven by the host's notification callback) =============
    /// Handle raw bytes received from the BLE transport. Performs prompt
    /// detection (OBD2 mode), CAN/OBD2 parsing, vehicle-detector observation,
    /// and delivery of any parsed binary to the host. The raw-notification
    /// bookkeeping (count/hex) is the host's concern and stays there.
    void handleIncomingData(const std::vector<uint8_t>& data);

    // === Prompt sequencing =================================================
    /// Block until the ELM327 '>' prompt is received or the timeout expires.
    bool waitForPrompt(int timeout_ms = PROMPT_TIMEOUT_MS);

    /// Signal that a '>' prompt has been observed in the incoming byte stream.
    void notifyPrompt();

    // === Accessors =========================================================
    /// Whether the adapter is in CAN monitor mode.
    [[nodiscard]] bool canMode() const noexcept { return can_mode_.load(); }

    /// Whether a '>' prompt has been signalled and not yet consumed by a wait.
    /// Exposed so the host (and legacy test seams) can observe prompt state.
    bool promptReady() const { return prompt_ready_; }

    /// The vehicle detector (for reading detection results / seeding VIN).
    domain::VehicleDetector* vehicleDetector() { return vehicle_detector_.get(); }

    /// Parse an ASCII OBD2 response to binary (delegates to ELM327Transport).
    /// Exposed so the host can offer the legacy parse helper surface.
    std::vector<uint8_t> parseASCIIResponseToBinary(const std::vector<uint8_t>& asciiData) const;

private:
    /// Body of the OBD2 polling thread.
    void obd2PollingLoop();

    Elm327SessionHost& host_;

    // OBD2 protocol handler for vehicle detection and command management.
    boundary::OBD2Protocol obd2_protocol_;

    // Vehicle auto-detection (passive CAN ID observation).
    std::unique_ptr<domain::VehicleDetector> vehicle_detector_{std::make_unique<domain::VehicleDetector>()};

    // CAN monitor mode flag.
    std::atomic<bool> can_mode_{false};

    // OBD2 polling state.
    std::atomic<bool> polling_active_{false};
    std::thread polling_thread_;
    int polling_interval_ms_ = DEFAULT_POLLING_INTERVAL_MS;

    // ELM327 prompt-driven sequencing state. The ELM327 sends '>' when ready
    // for the next command; BLE notifications may fragment responses, so we
    // buffer and scan for '>' across notification boundaries.
    std::mutex prompt_mutex_;
    std::condition_variable prompt_cv_;
    bool prompt_ready_ = false;
    std::string prompt_buffer_;
    static constexpr size_t PROMPT_BUFFER_MAX = 256;
};

} // namespace vehicle_sim
