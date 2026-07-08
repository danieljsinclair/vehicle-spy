#pragma once

#include "vehicle-sim/pipeline/IAdapterNormaliser.h"

#include <cstdint>

namespace vehicle_sim::pipeline {

/**
 * Adapter normaliser for ELM327 CAN-monitor output: lines of the form
 *   "<ID> <D0> <D1> ... <D7>"   e.g. "1D5 29 00 00 00 00 A0 9F"
 * as emitted by ATMA (monitor-all) with ATH1 (headers on). The CAN ID is a
 * 3-hex-digit 11-bit identifier; the 1..8 trailing tokens are the data bytes.
 * There is NO timestamp prefix — monitor lines are timestamped on receipt
 * (the transport/decoder stamps them with wall-clock time at decode), exactly
 * like the live raw normaliser.
 *
 * Non-frame adapter chatter is Skip (silent): the '>' ready-prompt, blank
 * lines, and status/banner strings (NO DATA, DATA ERROR, STOPPED, ?, OK,
 * ELM327, version strings, SEARCHING...). Frame-shaped lines that fail to
 * decode (bad hex, ID out of 11-bit range, more than 8 data bytes) are
 * Malformed.
 *
 * 29-bit (extended) CAN IDs are out of scope. They cannot be reliably
 * distinguished from 11-bit frames on the monitor line alone without protocol
 * context, so they are not specially detected here; a MARKER notes the future
 * extension point (tracked separately). KISS: the normaliser knows ONLY
 * monitor-line -> RawFrame.
 * It must NOT know transport (AT-init, socket) or DBC decode (Open/Closed).
 *
 * This is the normaliser the live ELM327 path uses (TCPTransport with
 * --adapter-protocol elm327, and later BLE/USB). The raw "<ID> <D0>..." form
 * without an ELM327 adapter is handled by RawFrameNormaliser; capture-file
 * replay uses CaptureNormaliser.
 */
class Elm327Normaliser final : public IAdapterNormaliser {
public:
    /**
     * Parse one ELM327 monitor line into a RawFrame with timestampMs = 0
     * (the caller/decoder stamps wall-clock time for live sources). Exposed
     * for unit testing the pure parser independently of the socket worker.
     */
    [[nodiscard]] static NormaliserResult parseMonitorLine(const std::string& line) noexcept;

    [[nodiscard]] NormaliserResult normalise(const std::string& line) noexcept override;
};

} // namespace vehicle_sim::pipeline
