#include "vehicle-sim/pipeline/FileTransport.h"

namespace vehicle_sim::pipeline {

bool FileTransport::open() {
    if (stream_.is_open()) {
        return true;
    }
    stream_.open(filePath_);
    return stream_.is_open();
}

bool FileTransport::isOpen() const noexcept {
    return stream_.is_open() && !stream_.eof() && !stream_.bad();
}

std::optional<std::string> FileTransport::nextLine() {
    if (!stream_.is_open() || stream_.eof() || stream_.bad()) {
        return std::nullopt;
    }
    std::string line;
    if (!std::getline(stream_, line)) {
        return std::nullopt;
    }
    return line;
}

} // namespace vehicle_sim::pipeline
