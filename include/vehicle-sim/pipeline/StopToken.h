#pragma once

#include <atomic>

namespace vehicle_sim::pipeline {

/**
 * Cooperative stop signal shared between a run-context, its transports, and the
 * process signal handler.
 *
 * Replaces the previous per-class file-scope `std::atomic<bool>` stop-flags
 * (TCPTransport::g_stopRequested, SecureTcpTransport::g_stopRequested,
 * USBTransport::g_stopRequested, the run-context g_running/g_liveRunning flags,
 * and UDPDiscovery::g_discoveryStopRequested) with a single injected value type.
 * A run-context owns one StopToken and shares it (via std::shared_ptr) with
 * every transport/discovery instance it creates, so a single Ctrl+C flips one
 * flag that all hot loops poll.
 *
 * The signal handler reaches the live StopToken through a single static pointer
 * published by the run-context (see SignalStopBroker) — the irreducible anchor a
 * C signal handler requires. requestStop()/reset() are atomic stores and are
 * async-signal-safe (N2547); the handler does no I/O.
 */
class StopToken {
public:
    void requestStop() noexcept { flag_.store(true, std::memory_order_seq_cst); }
    void reset() noexcept { flag_.store(false, std::memory_order_seq_cst); }
    [[nodiscard]] bool stopRequested() const noexcept {
        return flag_.load(std::memory_order_seq_cst);
    }

private:
    std::atomic<bool> flag_{false};
};

} // namespace vehicle_sim::pipeline
