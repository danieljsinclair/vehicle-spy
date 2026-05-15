#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <fstream>

namespace vehicle_sim::telemetry {

class RawTraceLogger {
public:
    explicit RawTraceLogger(std::string filePath);
    ~RawTraceLogger();

    void write(std::uint64_t timestampMs, const std::vector<std::uint8_t>& data) noexcept;

    RawTraceLogger(const RawTraceLogger&) = delete;
    RawTraceLogger& operator=(const RawTraceLogger&) = delete;
    RawTraceLogger(RawTraceLogger&&) noexcept;
    RawTraceLogger& operator=(RawTraceLogger&&) noexcept;

    [[nodiscard]] bool isValid() const noexcept;

private:
    std::ofstream file_;
};

}
