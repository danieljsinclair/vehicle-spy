#pragma once

#include "vehicle-sim/pipeline/IAdapterNormaliser.h"

#include <cstdint>
#include <string>

namespace vehicle_sim::pipeline {

/**
 * Adapter normaliser for ELM327 CAN-monitor output (ATMA + ATH1): lines of
 * the form "<3-hex-ID> <D0> ... <D7>". The CAN ID is a 3-hex-digit
 * 11-bit identifier; 1..8 trailing tokens are the data bytes.
 *
 * Non-frame adapter chatter is Skip (silent): the '>' ready-prompt, blank
 * lines, banner/status strings (NO DATA, DATA ERROR, STOPPED, ?, OK,
 * ELM327, version strings, SEARCHING...). Frame-shaped lines that fail to
 * decode (bad hex, ID out of 11-bit range, more than 8 data bytes) are
 * Malformed.
 *
 * 29-bit (extended) CAN IDs are out of scope. KISS: this normaliser knows
 * ONLY monitor-line -> TwaiFrame. It must NOT know transport (AT-init,
 * socket) or DBC decode (Open/Closed).
 *
 * The capture-file replay path uses BinaryFileSource (which decodes both
 * ASCII and binary TWAI captures); the live raw adapter path uses
 * LiveTwaiSource's inline tokeniser; this normaliser is the ELM327 path.
 */
class Elm327Normaliser final : public IAdapterNormaliser {
public:
    /**
     * Parse one ELM327 monitor line into a TwaiFrame. Exposed for unit
     * testing the pure parser independently of the socket worker.
     */
    [[nodiscard]] static NormaliserResult parseMonitorLine(const std::string& line) noexcept;

    [[nodiscard]] NormaliserResult normalise(const std::string& line) noexcept override;
};

} // namespace vehicle_sim::pipeline
