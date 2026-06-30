#pragma once

#include "vehicle-sim/pipeline/IAdapterNormaliser.h"
#include "vehicle-sim/pipeline/ITransport.h"
#include "vehicle-sim/pipeline/StopToken.h"

#include <memory>
#include <string>
#include <string_view>

namespace vehicle_sim::pipeline {

/**
 * The transport + normaliser pair that make up one pipeline source. Built by
 * PipelineFactory from CLI semantics; consumed by runReplay(). Both are
 * heap-owned so the factory can return the concrete type erased behind the
 * interface (Open/Closed: the driver never switches on transport kind).
 */
struct PipelineSource {
    std::unique_ptr<ITransport> transport;
    std::unique_ptr<IAdapterNormaliser> normaliser;
};

/**
 * Resolve the adapter protocol for a connect target + explicit override.
 *
 * Default table (applied when `adapterProtocol` is empty or "default"):
 *   demo → raw, file → raw, ble → elm327, tcp → raw, usb → raw.
 * An explicit "raw" or "elm327" override always wins. Returns "raw" or
 * "elm327"; unknown values are normalised to "raw".
 */
[[nodiscard]] std::string resolveAdapterProtocol(
    std::string_view connectTarget,
    std::string_view adapterProtocol) noexcept;

/**
 * Parse a "tcp:<host>:<port>" or "tcp:<host>" connect target.
 *
 * This is the SINGLE canonical TCP-target parser for the engine (the pipeline
 * owns TCP semantics now). Port defaults to 3333 (the firmware default) when
 * omitted. The port must be all-digits in [1, 65535]; an out-of-range or
 * non-numeric port after the last ':' causes the whole body to be treated as a
 * hostname with the default port.
 *
 * @param target   The raw connect string (must start with "tcp:").
 * @param hostOut  [out] Resolved host.
 * @param portOut  [out] Resolved port.
 * @return true if the target is a well-formed tcp: target with a non-empty host.
 */
[[nodiscard]] bool parseTcpTarget(
    std::string_view target,
    std::string& hostOut,
    int& portOut) noexcept;

/**
 * Build the pipeline source (transport + normaliser) for a connect target.
 *
 *   "file:<path>"            → FileTransport  + CaptureNormaliser
 *   "demo"                   → DemoTransport  + RawFrameNormaliser
 *   "tcp:<host>[:<port>]"    → TCPTransport   + RawFrameNormaliser
 *   "usb:<path>"              → USBTransport   + RawFrameNormaliser
 *
 * The factory is the ONLY place that knows which concrete transport pairs with
 * which normaliser — runReplay() and the run contexts are transport-agnostic
 * (Open/Closed: a new transport is a new factory branch, not a driver change).
 *
 * `adapterProtocol` is the resolved protocol ("raw" or "elm327"); only the TCP
 * transport consults it today (elm327 sends AT-init on connect).
 *
 * Returns {nullptr, nullptr} if the target is unsupported / unparseable.
 */
[[nodiscard]] PipelineSource buildPipelineSource(
    std::string_view connectTarget,
    std::string_view adapterProtocol,
    std::shared_ptr<StopToken> stop = nullptr);

} // namespace vehicle_sim::pipeline
