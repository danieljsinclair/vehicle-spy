#include "vehicle-sim/domain/ITimeProvider.h"

#include <chrono>

namespace vehicle_sim::domain {

std::uint64_t SystemTimeProvider::nowMs() const noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

MockTimeProvider::MockTimeProvider(std::uint64_t fixedTimeMs) noexcept
    : fixedTimeMs_(fixedTimeMs) {}

void MockTimeProvider::setCurrentTimeMs(std::uint64_t timeMs) noexcept {
    fixedTimeMs_ = timeMs;
}

std::uint64_t MockTimeProvider::nowMs() const noexcept {
    return fixedTimeMs_;
}

} // namespace vehicle_sim::domain
