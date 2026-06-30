#pragma once

namespace vehicle_sim::pipeline {

class StopToken;

/**
 * Async-signal-safe bridge between a C signal handler and an injected StopToken.
 *
 * A signal handler has no `this`, no capture, and no heap — it can only reach
 * static storage. This broker holds a single atomic pointer to the live
 * run-context's StopToken. A run-context publishes its token at run() entry
 * (brokerSet) and clears it on exit (brokerClear). The handler calls
 * onStopSignal(), whose entire body is a lock-free atomic load + an atomic
 * store (via StopToken::requestStop) — async-signal-safe per signal-safety(7)
 * and N2547, with NO std::cout / std::endl / locks / malloc.
 *
 * This single static pointer is the irreducible anchor any signal handler
 * requires; every other file-scope stop-flag moves onto injected StopTokens.
 * Single-active-run-context-per-process (already documented at
 * LiveRunContext.cpp:36-39) keeps the single-pointer broker safe.
 */
namespace signal_stop_broker {

/// Publish the live StopToken the handler should signal. Pass nullptr on exit.
void brokerSet(StopToken* token) noexcept;
/// Clear the published token (run-context teardown).
void brokerClear() noexcept;

/**
 * Signal-handler entry point (SIGINT/SIGTERM). Async-signal-safe: one atomic
 * load, at most one atomic store. Install via std::signal(onStopSignal).
 */
extern "C" void onStopSignal(int sig) noexcept;

} // namespace signal_stop_broker

} // namespace vehicle_sim::pipeline
