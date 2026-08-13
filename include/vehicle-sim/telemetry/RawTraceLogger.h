#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <fstream>

namespace vehicle_sim::telemetry {

class RawTraceLogger {
public:
    explicit RawTraceLogger(const std::string& filePath);

    // rule of zero: std::ofstream owns the file; the compiler-generated special
    // members (move/copy + dtor) are correct and noexcept where needed.

    void write(std::uint64_t timestampMs, const std::vector<std::uint8_t>& data) noexcept;

    [[nodiscard]] bool isValid() const noexcept;

private:
    std::ofstream file_;
};

}
