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

#include <WiFiUdp.h>
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
        // Defect 2 fix: the old code called udp_.beginPacket(ip.c_str(), port),
        // which routes to WiFiUDP::beginPacket(const char*). That does
        // gethostbyname() on the literal string — including the broadcast
        // address "255.255.255.255" used for discovery. gethostbyname on a
        // literal broadcast address is unreliable on ESP32 and frequently
        // returns NULL, so beginPacket() returned 0 and the packet never
        // left the radio (the tcpdump anomaly: broadcast() called ~264× but
        // only ~1 packet on the wire).
        //
        // Parse the address ourselves with IPAddress::fromString (no DNS) and
        // call the IPAddress overload, which skips gethostbyname entirely and
        // reliably opens the send. beginPacket() still returns 0 if the socket
        // can't be created, so DiscoveryManager's Defect-1 return-code check
        // will correctly NOT count a failed send.
        IPAddress resolved;
        if (!resolved.fromString(ip.c_str())) {
            return 0;  // Not a valid numeric address — fail fast, don't broadcast.
        }
        return udp_.beginPacket(resolved, port);
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
