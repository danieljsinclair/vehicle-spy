#pragma once

#include "vehicle-sim/pipeline/ITransport.h"

#include <fstream>
#include <string>

namespace vehicle_sim::pipeline {

/**
 * Phase 1 transport: reads a capture file line by line. The file IS the
 * source of truth for replay, so this transport has no separate "raw log"
 * output. Demo/BLE/TCP/USB transports added in later phases implement the
 * same ITransport interface.
 */
class FileTransport final : public ITransport {
public:
    explicit FileTransport(std::string filePath) : filePath_(std::move(filePath)) {}

    bool open() override;
    [[nodiscard]] bool isOpen() const noexcept override;
    std::optional<std::string> nextLine() override;

private:
    std::string filePath_;
    std::ifstream stream_;
};

} // namespace vehicle_sim::pipeline
