#include "vehicle-sim/pipeline/RawLogSink.h"

#include <chrono>

namespace vehicle_sim::pipeline {

RawLogSink::RawLogSink(const std::string& base)
    : file_(base + ".raw.txt") {}

RawLogSink::RawLogSink(RawLogSink&& other) noexcept
    : file_(std::move(other.file_)) {}

RawLogSink& RawLogSink::operator=(RawLogSink&& other) noexcept {
    if (this != &other) {
        if (file_.is_open()) {
            file_.close();
        }
        file_ = std::move(other.file_);
    }
    return *this;
}

RawLogSink::~RawLogSink() {
    if (file_.is_open()) {
        file_.close();
    }
}

void RawLogSink::writeLine(const std::string& line) noexcept {
    if (!file_.is_open()) {
        return;
    }
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    file_ << now << ',' << line << '\n';
    file_.flush();
}

bool RawLogSink::isValid() const noexcept {
    return file_.is_open();
}

} // namespace vehicle_sim::pipeline
