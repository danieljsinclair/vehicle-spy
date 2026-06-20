#include "vehicle-sim/pipeline/CaptureNormaliser.h"
#include "vehicle-sim/domain/CaptureLog.h"

namespace vehicle_sim::pipeline {

NormaliserResult CaptureNormaliser::normalise(const std::string& line) noexcept {
    using domain::CaptureParseResult;
    auto parsed = domain::parseCaptureLine(line);
    switch (parsed.result) {
        case CaptureParseResult::Frame:
            return NormaliserResult::ofFrame(std::move(parsed.frame), /*timestamped=*/true);
        case CaptureParseResult::NotAFrame:
            return NormaliserResult::skip();
        case CaptureParseResult::Malformed:
        default:
            return NormaliserResult::malformed();
    }
}

} // namespace vehicle_sim::pipeline
