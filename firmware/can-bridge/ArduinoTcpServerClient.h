#pragma once

// ArduinoTcpServerClient.h - Arduino WiFiClient adapter for ITcpServerClient.
// Bridges a single connected TCP client (Arduino WiFiClient) to the vanilla
// ITcpServerClient interface used by TcpServerManager.
//
// The adapter holds a REFERENCE to the .ino's global WiFiClient (the single
// connection truth source) so that ArduinoTcpClient + firmwareApp.setClientConnected
// — which read the same global — stay in sync with the manager's lifecycle.
// The manager owns the adapter (unique_ptr); it does NOT own the underlying
// WiFiClient (the global does).
//
// Production implementation used in the .ino. Host tests use the mock in
// TcpServerManager_test.cpp. Only available when building for Arduino.
//
// NOTE: no #ifdef ARDUINO guard here — this header is only #included by
// can-bridge.ino (compiled solely for the device), so the Arduino WiFi types
// are always available.

#include <Arduino.h>
#include <WiFiClient.h>
#include "ITcpServer.h"

#include <string>

namespace esp32_firmware {

class ArduinoTcpServerClient : public ITcpServerClient {
public:
    explicit ArduinoTcpServerClient(WiFiClient& client) : client_(client) {}

    bool connected() const override {
        return static_cast<bool>(client_) && client_.connected();
    }

    void stop() override { client_.stop(); }

    void setTimeout(uint32_t ms) override {
        // Resolve to Stream::setTimeout(unsigned long) [milliseconds], NOT the
        // WiFiClient::setTimeout(uint32_t seconds) overload — the inline passed
        // millisecond constants (TCP_AUTH_TIMEOUT_MS etc.), so we must keep ms
        // semantics. Binding to a named unsigned long selects the Stream overload
        // without a Sonar-flagged cast (the two uint32_t/unsigned long overloads
        // differ in units, so overload selection is load-bearing here).
        const unsigned long timeoutMs = ms;
        client_.setTimeout(timeoutMs);
    }

    int available() const override {
        return client_.available();
    }

    std::string readLine(char delimiter) override {
        // WiFiClient::readStringUntil reads up to (and consumes) the delimiter.
        // Returns whatever was buffered even on timeout — the manager trims +
        // validates the result (it tolerates an empty/partial line).
        String line = client_.readStringUntil(delimiter);
        return std::string(line.c_str());
    }

    void println(const std::string& line) override {
        client_.println(line.c_str());
    }

    void flush() override { client_.flush(); }

private:
    WiFiClient& client_;
};

} // namespace esp32_firmware
