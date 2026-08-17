#pragma once

// ArduinoTcpServerClient.h - Arduino WiFiClient adapter for ITcpServerClient.
// Bridges a single connected TCP client (Arduino WiFiClient) to the vanilla
// ITcpServerClient interface used by TcpServerManager.
//
// The adapter holds a REFERENCE to the .ino's global WiFiClient (the single
// connection truth source) so that ArduinoTcpClient — which reads the same
// global — stays in sync with the manager's lifecycle. The manager owns the
// adapter (unique_ptr); it does NOT own the underlying WiFiClient (the global
// does). FirmwareApp queries the client-adoption state via
// IClientConnectionSource (TcpServerManager::hasClient()), not via this global.
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
    // Takes ownership of the accepted WiFiClient by VALUE (moved from
    // ArduinoTcpServer::accept()). Holding the actual accepted client (not a
    // reference to the .ino's global slot) guarantees this adapter reads the
    // bytes that arrived on the connection it was adopted for — the global slot
    // is only a write-facing mirror used by the CAN-TX path.
    explicit ArduinoTcpServerClient(WiFiClient client) : client_(std::move(client)) {}

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

    std::string readAvailableLine(char delimiter) override {
        // NON-BLOCKING: consume ONLY bytes already in the receive buffer and
        // return them verbatim (delimiter NOT stripped). Never blocks up to the
        // Stream timeout. The manager owns the line-splitting (it accumulates
        // partial bytes in its own buffer and dispatches on the delimiter),
        // which is what keeps cycle() from stalling the CAN-TX path. The
        // delimiter arg is accepted for interface parity with readLine() but
        // splitting happens in the manager.
        //
        // IMPORTANT (ESP32 WiFiClient quirk): WiFiClient::available() can return
        // 0 transiently even when bytes are buffered — notably right after a
        // readStringUntil() (the AUTH read) — which would make a caller that
        // gates on available()>0 silently skip ready input forever. So we do NOT
        // trust available(): we poll read() directly in a bounded loop. read()
        // returns -1 immediately when nothing is buffered (non-blocking), so this
        // stays O(1) on an idle socket and still drains every available byte.
        (void)delimiter;
        std::string buf;
        constexpr std::size_t kMaxDrain = 256;
        buf.reserve(kMaxDrain);
        int c = 0;
        while (buf.size() < kMaxDrain && (c = client_.read()) >= 0) {
            buf.push_back(static_cast<char>(c));
        }
        return buf;
    }

    void println(const std::string& line) override {
        client_.println(line.c_str());
    }

    void flush() override { client_.flush(); }

    void setNoDelay(bool enable) override {
        // WiFiClient has no setNoDelay(bool); the Arduino core exposes it as
        // setNoDelay(bool) on WiFiClient (ESP32). We forward directly.
        client_.setNoDelay(enable);
    }

    std::string remoteIP() const override {
        // WiFiClient::remoteIP() returns an IPAddress; toString() yields "ipv4"
        // or "ipv6". Returns empty string when the client is not connected.
        if (!static_cast<bool>(client_) || !client_.connected()) {
            return {};
        }
        return std::string(client_.remoteIP().toString().c_str());
    }

private:
    // mutable: the ITcpServerClient interface mandates connected()/available()/
    // remoteIP() as const, but the ESP32 WiFiClient SDK declares those *query*
    // methods non-const. These are logically read-only state probes, so the
    // value-owned client is marked mutable to satisfy the interface contract on
    // the real device. (The host mock marks them const, which would otherwise
    // mask this cross-build mismatch — the commit gate catches it via the real
    // .ino compile.)
    mutable WiFiClient client_;
};

} // namespace esp32_firmware
