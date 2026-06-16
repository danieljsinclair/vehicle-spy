#pragma once

#include "vehicle-sim/pipeline/IAdapterNormaliser.h"

namespace vehicle_sim::pipeline {

/**
 * Adapter normaliser for CAPTURE-FILE replay: turns capture transport lines
 * into RawFrames using the EXISTING domain::parseCaptureLine (legacy CSV
 * "ts,id,dlc,hex" and verbatim "ts,ID D0 D1..." forms). Parsing is reused,
 * not duplicated.
 *
 * This is the normaliser the file-replay path uses (FileTransport →
 * CaptureNormaliser). It deliberately owns no protocol state. The live-raw
 * adapter form (no timestamp, "ID D0 D1 ... D7") is handled by
 * RawFrameNormaliser; the ELM327 dialect is a later task (#18).
 */
class CaptureNormaliser final : public IAdapterNormaliser {
public:
    [[nodiscard]] NormaliserResult normalise(const std::string& line) noexcept override;
};

} // namespace vehicle_sim::pipeline
