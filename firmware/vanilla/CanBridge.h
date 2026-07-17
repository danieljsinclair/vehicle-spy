#pragma once

// CanBridge.h - Vanilla C++ CAN frame streaming
// Extracted from can-bridge.ino for host testability

#include <cstdint>
#include <array>
#include <functional>

namespace esp32_firmware {

// CAN frame structure (matches twai_message_t)
struct CanFrame {
    uint32_t identifier;
    uint8_t data_length_code;
    uint8_t data[8];
};

// CAN configuration
struct CanConfig {
    static constexpr size_t CAN_FRAME_BUFFER_SIZE = 32;
    static constexpr uint8_t MAX_DATA_LENGTH = 8;
};

// Opaque handles for the ESP-IDF TWAI driver configs. Forward-declared here and
// defined ONLY in the adapter (ArduinoCanDriver wraps twai_general_config_t /
// twai_timing_config_t / twai_filter_config_t*). The vanilla never dereferences
// them — it threads the pointers between the injected ICanDriver::driverInstall
// call sites. This replaces raw `void*` (cpp:S5008) with named type-safe
// handles while keeping the vanilla free of ESP-IDF headers (mirrors the
// OtaPartitionRef idiom in OtaUpdateServer.h).
struct CanGeneralConfig;
struct CanTimingConfig;
struct CanFilterConfig;

// TWAI/CAN interface
struct ICanDriver {
    virtual int driverInstall(CanGeneralConfig* gcfg, CanTimingConfig* tcfg, CanFilterConfig* fcfg) = 0;
    virtual int start() = 0;
    virtual int receive(CanFrame* msg, uint32_t timeoutMs) = 0;
    virtual ~ICanDriver() = default;
};

// TCP client interface
struct ITcpClient {
    virtual bool connected() const = 0;
    virtual size_t print(const char* str) = 0;
    virtual void flush() = 0;
    virtual ~ITcpClient() = default;
};

// Serial interface
struct ISerialCan {
    virtual size_t print(const char* str) = 0;
    virtual void flush() = 0;
    virtual ~ISerialCan() = default;
};

// Aggregated dependency bundle for CanBridge construction.
// Named/value-semantic (holds 3 refs) so callers can build it once and pass it
// through an orchestrator ctor without leaking the individual adapters.
struct CanBridgeDeps {
    ICanDriver& canDriver;
    ITcpClient& tcpClient;
    ISerialCan& serial;
};

class CanBridge {
public:
    CanBridge(ICanDriver& canDriver, ITcpClient& tcpClient, ISerialCan& serial);
    explicit CanBridge(const CanBridgeDeps& deps);

    // Initialize CAN driver
    bool init();

    // Process received CAN frames - call from main loop()
    void processFrames(bool monitorActive, uint32_t serialQuietUntilMs);

    // Set monitor active state
    void setMonitorActive(bool active) { monitorActive_ = active; }

    // Check if monitor is active
    bool isMonitorActive() const { return monitorActive_; }

    // Testable pure function: build frame string
    static void buildFrameString(const CanFrame& msg, char* buf, size_t bufSize);

private:
    ICanDriver& canDriver_;
    ITcpClient& tcpClient_;
    ISerialCan& serial_;

    bool monitorActive_ = false;
    bool initialized_ = false;
};

} // namespace esp32_firmware