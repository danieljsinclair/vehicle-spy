#pragma once

// SerialDebugMock.h - Recording test double for the ISerial debug-trace seam
// declared in firmware/vanilla/WiFiManager.h.
//
// ISerial::printf is variadic, which gmock cannot mock directly (MOCK_METHOD does
// not support C varargs). This hand-rolled spy formats each call into a string and
// records it, so tests assert on the RENDERED trace line — the thing that actually
// reaches the serial console — rather than on unformatted format-string arguments.

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "vanilla/WiFiManager.h"

namespace esp32_firmware {

class SerialDebugMock : public ISerial {
public:
    void println(const char* msg) override {
        lines_.emplace_back(msg);
    }

    __attribute__((format(printf, 2, 3)))
    void printf(const char* fmt, ...) override {
        va_list args;
        va_start(args, fmt);
        char buffer[256];
        const int written = vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        if (written > 0) {
            lines_.emplace_back(buffer);
        }
    }

    // All recorded output lines, in emission order.
    const std::vector<std::string>& lines() const { return lines_; }

    // Number of recorded lines containing the given substring.
    size_t countContaining(const std::string& needle) const {
        size_t count = 0;
        for (const std::string& line : lines_) {
            if (line.find(needle) != std::string::npos) {
                ++count;
            }
        }
        return count;
    }

    bool contains(const std::string& needle) const {
        return countContaining(needle) > 0;
    }

    // Recorded line at index, or an explicit sentinel when absent. Tests use
    // this instead of lines().front() so that a missing line produces a clean
    // assertion failure with a readable diagnostic, rather than undefined
    // behaviour from dereferencing an empty vector.
    std::string lineAt(size_t index) const {
        return index < lines_.size() ? lines_[index] : std::string("<no line emitted>");
    }

    std::string firstLine() const { return lineAt(0); }

    void reset() { lines_.clear(); }

private:
    std::vector<std::string> lines_;
};

} // namespace esp32_firmware
