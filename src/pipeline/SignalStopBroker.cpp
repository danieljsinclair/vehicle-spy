#include "vehicle-sim/pipeline/SignalStopBroker.h"
#include "vehicle-sim/pipeline/StopToken.h"

#include <atomic>

namespace vehicle_sim::pipeline::signal_stop_broker {

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

} // namespace vehicle_sim::pipeline::signal_stop_broker

extern "C" void vehicle_sim_onStopSignal(int /*sig*/) noexcept {
    // Async-signal-safe body: one atomic load, at most one atomic store. No I/O,
    // no locks, no allocation. The atomic stop IS the signal to the hot loops.
    // g_activeStop lives in this TU's signal_stop_broker anonymous namespace;
    // the global C-linkage handler reaches it by full qualification.
    namespace bkr = vehicle_sim::pipeline::signal_stop_broker;
    if (vehicle_sim::pipeline::StopToken* token = bkr::g_activeStop.load(std::memory_order_seq_cst)) {
        token->requestStop();
    }
}
