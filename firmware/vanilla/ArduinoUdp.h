#pragma once

// ArduinoUdp.h - Arduino UDP implementation for IUdp interface
// Bridges Arduino WiFiUDP library to vanilla IUdp interface
//
// This is the production implementation used in the .ino.
// For host testing, use MockUdp from tests.
//
// IMPORTANT: This file is only available when building for Arduino (ARDUINO defined).
// Host tests use mocks instead.

#ifdef ARDUINO

#include <WiFiUDP.h>
#include <string>
#include "DiscoveryManager.h"

namespace esp32_firmware {

// ArduinoUdp - production IUdp implementation using Arduino WiFiUDP library
class ArduinoUdp : public IUdp {
public:
    ArduinoUdp() = default;

    // IUdp interface - delegates to WiFiUDP class
    void begin(uint16_t port) override {
        udp_.begin(port);
    }

    int beginPacket(const std::string& ip, uint16_t port) override {
        return udp_.beginPacket(ip.c_str(), port);
    }

    size_t write(const uint8_t* data, size_t len) override {
        return udp_.write(data, len);
    }

    int endPacket() override {
        return udp_.endPacket();
    }

private:
    WiFiUDP udp_;
};

} // namespace esp32_firmware

#endif // ARDUINO
