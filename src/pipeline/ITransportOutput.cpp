#include "vehicle-sim/pipeline/ITransportOutput.h"
#include <iostream>

namespace vehicle_sim::pipeline {

void StdOut::out(const std::string& msg) {
    std::cout << msg << std::endl;
}

void StdOut::err(const std::string& msg) {
    std::cerr << msg << std::endl;
}

// TaggedOutput implementation
TaggedOutput::TaggedOutput(std::shared_ptr<ITransportOutput> base, const std::string& deviceId)
    : base_(std::move(base))
    , deviceId_(deviceId)
{
}

void TaggedOutput::setDeviceId(const std::string& deviceId) {
    deviceId_ = deviceId;
}

void TaggedOutput::out(const std::string& msg) {
    // Default to client (for backward compatibility)
    outClient(msg);
}

void TaggedOutput::outClient(const std::string& msg) {
    if (!deviceId_.empty()) {
        // Extract first 8 chars of device ID for display (32 hex -> 8 hex)
        std::string shortId = deviceId_.substr(0, 8);
        std::string prefix = "[CLIENT → " + shortId + "] ";
        base_->out(prefix + msg);
    } else {
        base_->out(msg);
    }
}

void TaggedOutput::outDevice(const std::string& msg) {
    if (!deviceId_.empty()) {
        // Extract first 8 chars of device ID for display (32 hex -> 8 hex)
        std::string shortId = deviceId_.substr(0, 8);
        // ESP32 messages in blue
        std::string prefix = "\033[0;34m[ESP32 " + shortId + "] \033[0m";
        base_->out(prefix + msg);
    } else {
        base_->out(msg);
    }
}

void TaggedOutput::err(const std::string& msg) {
    // Error messages don't get tagged (they're system-level)
    base_->err(msg);
}

}  // namespace vehicle_sim::pipeline
