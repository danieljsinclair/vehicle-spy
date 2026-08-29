#pragma once

#include "vehicle-sim/pipeline/ITransport.h"
#include "vehicle-sim/pipeline/StopToken.h"

#include <memory>
#include <string>
#include <string_view>

namespace vehicle_sim::pipeline {

/**
 * A built pipeline source (just the transport — IAdapterNormaliser is an
 * internal detail of LiveTwaiSource). Built by PipelineFactory from CLI
 * semantics; consumed by LiveRunContext to wire the transport into the
 * canonical IFrameSource seam.
 *
 * The factory is the ONLY place that knows which concrete transport a
 * connect target maps to. A new transport is a new factory branch — the
 * driver is transport-agnostic.
 */
struct PipelineSource {
    std::unique_ptr<ITransport> transport;
};

/**
 * Resolve the effective adapter protocol for a connect target + override.
 *   "raw"     — direct CAN frames
 *   "elm327"  — AT-init + 11-bit monitor dialect
 * Default table: file/demo/tcp/usb → raw; BLE → elm327. An explicit raw/elm
 * override always wins. Case-insensitive.
 */
[[nodiscard]] std::string resolveAdapterProtocol(
    std::string_view connectTarget,
    std::string_view adapterProtocol) noexcept;

/**
 * Parse a "tcp:<host>:<port>" or "tcp:<host>" target. Port defaults to 3333.
 * Port token must be all-digits in [1, 65535]; non-numeric → whole body is
 * the host with default port.
 */
[[nodiscard]] bool parseTcpTarget(
    std::string_view target,
    std::string& hostOut,
    int& portOut) noexcept;

/**
 * Build the live transport for a connect target. No normaliser is built —
 * LiveTwaiSource owns the text→TwaiFrame parsing (inline for raw, via the
 * supplied ELM327 normaliser for ELM327 mode).
 *   "demo"                 → DemoTransport
 *   "tcp:<host>[:<port>]"  → TCPTransport
 *   "usb:<path>"           → USBTransport
 * Returns {nullptr} if the target is unsupported / unparseable.
 */
[[nodiscard]] PipelineSource buildPipelineSource(
    std::string_view connectTarget,
    std::string_view adapterProtocol,
    std::shared_ptr<StopToken> stop = nullptr);

} // namespace vehicle_sim::pipeline
