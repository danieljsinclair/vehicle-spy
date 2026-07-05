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

// TWAI/CAN interface
struct ICanDriver {
    virtual int driverInstall(void* gcfg, void* tcfg, void* fcfg) = 0;
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

class CanBridge {
public:
    CanBridge(ICanDriver& canDriver, ITcpClient& tcpClient, ISerialCan& serial);

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