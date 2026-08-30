#pragma once

#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/ISignalSource.h"
#include "vehicle-sim/domain/VehicleSignal.h"
#include "vehicle-sim/pipeline/ITransport.h"
#include "vehicle-sim/pipeline/StopToken.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace vehicle_sim::pipeline {

/**
 * ISignalSource implementation that drives a live ITransport (the TCP path to
 * an ESP32 CAN bridge) through the canonical IFrameSource seam (LiveTwaiSource)
 * on a background thread. Each decoded VehicleSignal is stored as the latest
 * signal, which the caller polls from its own thread (the iOS wrapper polls
 * from the main thread; see VehicleSimWrapper.mm).
 *
 * Thread-safety: latestSignal() is called from the polling thread; the
 * pipeline writes from the background thread. A mutex protects the signal.
 *
 * Lifecycle: start() launches the pipeline thread; stop() requests the transport
 * to cease and joins the thread. The transport's stop flag is set so nextLine()
 * returns nullopt at its next select() timeout, cleanly ending the loop.
 */
class TCPSignalSource final : public domain::ISignalSource {
public:
    TCPSignalSource(std::unique_ptr<ITransport> transport,
                    domain::DBCTranslationService& translationService,
                    std::shared_ptr<StopToken> stop);
    ~TCPSignalSource() override;

    TCPSignalSource(const TCPSignalSource&) = delete;
    TCPSignalSource& operator=(const TCPSignalSource&) = delete;

    [[nodiscard]] domain::VehicleSignal latestSignal() const noexcept override;

    /// Returns true if the pipeline thread is still alive and reading from
    /// the transport. When the transport exhausts (peer close, network drop),
    /// the worker sets running_ = false and this returns false — allowing
    /// the caller to detect a silent TCP drop.
    [[nodiscard]] bool isRunning() const noexcept;

    void start() override;
    void stop() override;

private:
    std::unique_ptr<ITransport> transport_;
    domain::DBCTranslationService& translationService_;
    std::shared_ptr<StopToken> stop_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::optional<domain::VehicleSignal> latestSignal_;
    mutable std::mutex mutex_;

    // The decode loop driven on the worker thread: LiveTwaiSource wraps the
    // transport (inline raw-CAN tokeniser + wall-clock stamping), and each
    // TwaiFrame goes through the translation service — the same decode as the
    // CLI live path.
    void runPipeline();
};

} // namespace vehicle_sim::pipeline
