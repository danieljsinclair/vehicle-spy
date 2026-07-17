#pragma once

// TWAIMock.h - Mock TWAI/CAN driver for host testing
// Implements ICanDriver interface

#include <queue>
#include "CanBridge.h"

namespace esp32_firmware {

class TWAIMock : public ICanDriver {
public:
    TWAIMock() = default;

    int driverInstall(CanGeneralConfig*, CanTimingConfig*, CanFilterConfig*) override {
        (void)gcfg; (void)tcfg; (void)fcfg;
        installed_ = true;
        return 0;  // ESP_OK
    }

    int start() override {
        started_ = true;
        return 0;  // ESP_OK
    }

    int receive(CanFrame* msg, uint32_t timeoutMs) override {
        (void)timeoutMs;
        if (rxQueue_.empty()) {
            return -1;  // ESP_ERR_TIMEOUT
        }
        *msg = rxQueue_.front();
        rxQueue_.pop();
        return 0;  // ESP_OK
    }

    // Test helpers
    void pushFrame(const CanFrame& frame) {
        rxQueue_.push(frame);
    }

    void pushFrame(uint32_t id, const uint8_t* data, uint8_t len) {
        CanFrame frame;
        frame.identifier = id;
        frame.data_length_code = std::min(len, static_cast<uint8_t>(8));
        std::memcpy(frame.data, data, frame.data_length_code);
        rxQueue_.push(frame);
    }

    bool isInstalled() const { return installed_; }
    bool isStarted() const { return started_; }

    void reset() {
        installed_ = false;
        started_ = false;
        while (!rxQueue_.empty()) rxQueue_.pop();
    }

private:
    std::queue<CanFrame> rxQueue_;
    bool installed_ = false;
    bool started_ = false;
};

} // namespace esp32_firmware