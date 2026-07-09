#include "vehicle-sim/pipeline/SignalStopBroker.h"
#include "vehicle-sim/pipeline/StopToken.h"

#include <atomic>

namespace vehicle_sim::pipeline::signal_stop_broker {

// The single static anchor a signal handler requires. Lock-free atomic load/store
// (pointer-width atomics are lock-free on Apple Silicon and ESP32 Xtensa).
// Defaults to nullptr; the active run-context publishes its StopToken via brokerSet().
// Held as a function-local static (not a namespace global) so there is no non-const
// global variable; initialisation is thread-safe in C++11+.
std::atomic<StopToken*>& activeStop() noexcept {
    static std::atomic<StopToken*> g_activeStop{nullptr};
    return g_activeStop;
}

void brokerSet(StopToken* token) noexcept {
    activeStop().store(token, std::memory_order_seq_cst);
}

void brokerClear() noexcept {
    activeStop().store(nullptr, std::memory_order_seq_cst);
}

} // namespace vehicle_sim::pipeline::signal_stop_broker

extern "C" void vehicle_sim_onStopSignal(int /*sig*/) noexcept {
    // Async-signal-safe body: one atomic load, at most one atomic store. No I/O,
    // no locks, no allocation. The atomic stop IS the signal to the hot loops.
    // The active stop lives behind a function-local static reached via
    // signal_stop_broker::activeStop(); the global C-linkage handler qualifies it.
    namespace bkr = vehicle_sim::pipeline::signal_stop_broker;
    if (vehicle_sim::pipeline::StopToken* token = bkr::activeStop().load(std::memory_order_seq_cst)) {
        token->requestStop();
    }
}
