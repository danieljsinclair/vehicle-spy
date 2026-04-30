#pragma once

#include <cstdint>

namespace vehicle_sim::domain {

/**
 * Time provider abstraction for dependency injection.
 *
 * Replaces hardcoded std::chrono::system_clock::now() to enable
 * deterministic testing and flexible time sources (system time,
 * simulation time, mocked time).
 *
 * Implements DIP (Dependency Inversion Principle): translators depend
 * on this abstraction rather than concrete system time.
 */
class ITimeProvider {
public:
    virtual ~ITimeProvider() = default;

    /**
     * Get current time in milliseconds since Unix epoch.
     *
     * @return timestamp in milliseconds
     */
    [[nodiscard]] virtual std::uint64_t nowMs() const noexcept = 0;
};

/**
 * System time provider using std::chrono.
 *
 * Production implementation for real-time vehicle telemetry.
 */
class SystemTimeProvider final : public ITimeProvider {
public:
    ~SystemTimeProvider() override = default;

    [[nodiscard]] std::uint64_t nowMs() const noexcept override;
};

/**
 * Mock time provider for testing.
 *
 * Returns a fixed or controlled timestamp for deterministic tests.
 */
class MockTimeProvider final : public ITimeProvider {
public:
    explicit MockTimeProvider(std::uint64_t fixedTimeMs = 0) noexcept;
    ~MockTimeProvider() override = default;

    void setCurrentTimeMs(std::uint64_t timeMs) noexcept;

    [[nodiscard]] std::uint64_t nowMs() const noexcept override;

private:
    std::uint64_t fixedTimeMs_;
};

} // namespace vehicle_sim::domain
