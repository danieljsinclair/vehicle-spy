#pragma once

// ArduinoCanAdapters.h - CAN/TWAI Arduino adapter implementations.
//
// Header-only thin Arduino implementations of the vanilla ICanDriver/ITcpClient/
// ISerialCan/ILogger/ITwaiHardware interfaces declared in firmware/vanilla/
// CanDriver.h. Each adapter is constructed by the .ino with device-specific values
// it already owns (WiFiClient reference, color strings); the vanilla code is
// unaware of Arduino types.
//
// Header-only: no .cpp TU. Only #included by can-bridge.ino (device-only), so no
// #ifdef ARDUINO guard is needed.

#include <Arduino.h>
#include <WiFiClient.h>
#include <driver/twai.h>
#include "CanDriver.h"

namespace esp32_firmware {

// CAN driver adapter: bridges TWAI driver calls to the vanilla ICanDriver.
// The TWAI enabled/disabled branch is guarded at compile time; the enabled body
// calls ::twai_receive directly, the disabled body returns -1 stubs.
#if VEHICLE_SIM_ENABLE_TWAI
struct ArduinoCanDriver : public ICanDriver {
    int driverInstall(CanGeneralConfig*, CanTimingConfig*, CanFilterConfig*) override { return 0; }  // done in setup()
    int start() override { return 0; }                              // done in setup()
    int receive(CanFrame* msg, uint32_t timeoutMs) override {
        twai_message_t m{};
        if (twai_receive(&m, timeoutMs) != ESP_OK) return -1;
        msg->identifier = m.identifier;
        msg->data_length_code = m.data_length_code;
        std::copy(std::begin(m.data), std::end(m.data), std::begin(msg->data));
        return 0;  // ESP_OK
    }
};
#else
struct ArduinoCanDriver : public ICanDriver {
    int driverInstall(CanGeneralConfig*, CanTimingConfig*, CanFilterConfig*) override { return -1; }
    int start() override { return -1; }
    int receive(CanFrame*, uint32_t) override { return -1; }
};
#endif

// TCP client adapter: wraps the .ino's WiFiClient (the connected buddy).
// The WiFiClient reference is injected at construction so the adapter is
// independent of the global client() accessor.
struct ArduinoTcpClient : public ITcpClient {
    explicit ArduinoTcpClient(WiFiClient& client) : client_(client) {}
    bool connected() const override { return client_ && client_.connected(); }
    size_t print(const char* str) override { return client_.print(str); }
    void flush() override { client_.flush(); }
private:
    WiFiClient& client_;
};

// Serial CAN adapter: wraps Serial (USB diagnostic logging).
struct ArduinoSerialCan : public ISerialCan {
    size_t print(const char* str) override { return Serial.print(str); }
    void flush() override { Serial.flush(); }
};

// TWAI logger adapter: diagnostic output with ANSI color codes.
// isError selects severity: true -> RED (fatal error), false -> plain (info).
struct ArduinoTwaiLogger : public ILogger {
    ArduinoTwaiLogger(const char* red, const char* nc) : red_(red), nc_(nc) {}
    void log(const char* msg, bool isError) override {
        if (isError) {
            Serial.printf("%s%s%s\r\n", red_, msg, nc_);
        } else {
            Serial.printf("%s\r\n", msg);
        }
    }
private:
    const char* red_;
    const char* nc_;
};

// TWAI hardware adapter: thin wrappers around ESP-IDF TWAI driver calls.
// The opaque vanilla CanGeneralConfig/CanTimingConfig/CanFilterConfig pointers
// are cast back to their ESP-IDF twai_*_config_t types at the boundary.
struct ArduinoTwaiHardware : public ITwaiHardware {
    int driverInstall(CanGeneralConfig* gcfg,
                      CanTimingConfig* tcfg,
                      CanFilterConfig* fcfg) override {
        return ::twai_driver_install(
            static_cast<const twai_general_config_t*>(static_cast<const void*>(gcfg)),
            static_cast<const twai_timing_config_t*>(static_cast<const void*>(tcfg)),
            static_cast<const twai_filter_config_t*>(static_cast<const void*>(fcfg))
        );
    }
    int start() override {
        return ::twai_start();
    }
};

} // namespace esp32_firmware
