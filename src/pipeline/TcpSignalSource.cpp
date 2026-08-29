#include "vehicle-sim/pipeline/TcpSignalSource.h"
#include "vehicle-sim/pipeline/LiveTwaiSource.h"

#include <vector>

namespace vehicle_sim::pipeline {

TCPSignalSource::TCPSignalSource(std::unique_ptr<ITransport> transport,
                                 domain::DBCTranslationService& translationService,
                                 std::shared_ptr<StopToken> stop)
    : transport_(std::move(transport))
    , translationService_(translationService)
    , stop_(std::move(stop))
{}

TCPSignalSource::~TCPSignalSource() {
    stop();
}

domain::VehicleSignal TCPSignalSource::latestSignal() const noexcept {
    std::scoped_lock lock(mutex_);
    return latestSignal_.value_or(
        domain::VehicleSignal(domain::VehicleSignal::Params{.timestampUtcMs = 0}));
}

bool TCPSignalSource::isRunning() const noexcept {
    return running_.load();
}

void TCPSignalSource::start() {
    if (running_.exchange(true)) {
        return; // already running
    }
    stop_->reset();
    if (!transport_->open()) {
        running_ = false;
        return;
    }
    worker_ = std::thread([this]() {
        runPipeline();
    });
}

void TCPSignalSource::stop() {
    // Always join the worker when one exists. Guarding the whole body on
    // running_ (as the original wrapper-local version did) skips the join
    // after the transport exhausts on its own — the thread has finished but
    // was never joined, and destroying a joinable std::thread calls
    // std::terminate. Idempotent: a second stop() finds nothing joinable.
    running_ = false;
    // Request the transport to return nullopt at its next select() timeout.
    stop_->requestStop();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void TCPSignalSource::runPipeline() {
    LiveTwaiSource source(*transport_);
    while (running_ && source.isOpen()) {
        auto frame = source.nextFrame();
        if (!frame) {
            break;
        }
        std::vector<std::uint8_t> bytes(frame->bytes.begin(), frame->bytes.end());
        auto signal = translationService_.processFrame(bytes, frame->timestampMs);
        if (signal) {
            std::scoped_lock lock(mutex_);
            latestSignal_ = signal;
        }
    }
    running_ = false;
}

} // namespace vehicle_sim::pipeline
