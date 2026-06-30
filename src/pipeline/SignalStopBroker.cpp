#include "vehicle-sim/pipeline/SignalStopBroker.h"
#include "vehicle-sim/pipeline/StopToken.h"

#include <atomic>

namespace vehicle_sim::pipeline {
namespace signal_stop_broker {

namespace {
// The single static anchor a signal handler requires. Lock-free atomic load/store
// (pointer-width atomics are lock-free on Apple Silicon and ESP32 Xtensa).
// Defaults to nullptr; the active run-context publishes its StopToken via brokerSet().
std::atomic<StopToken*> g_activeStop{nullptr};
} // namespace

void brokerSet(StopToken* token) noexcept {
    g_activeStop.store(token, std::memory_order_seq_cst);
}

void brokerClear() noexcept {
    g_activeStop.store(nullptr, std::memory_order_seq_cst);
}

extern "C" void onStopSignal(int /*sig*/) noexcept {
    // Async-signal-safe body: one atomic load, at most one atomic store. No I/O,
    // no locks, no allocation. The atomic stop IS the signal to the hot loops.
    if (StopToken* token = g_activeStop.load(std::memory_order_seq_cst)) {
        token->requestStop();
    }
}

} // namespace signal_stop_broker
} // namespace vehicle_sim::pipeline
