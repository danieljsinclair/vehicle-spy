#pragma once

// AtCommandDispatcher.h - Vanilla C++ AT command handling
// Extracted from can-bridge.ino for host testability

#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace esp32_firmware {

// AT command result
struct AtCommandResult {
    std::string response;
    bool shouldReboot = false;
    bool shouldFlushClient = false;

    AtCommandResult() = default;
    explicit AtCommandResult(const char* resp, bool reboot = false, bool flush = false)
        : response(resp), shouldReboot(reboot), shouldFlushClient(flush) {}
};

// AT command handler interface (Command Pattern)
struct IAtCommandHandler {
    virtual bool matches(const std::string& normalizedCmd) const = 0;
    virtual AtCommandResult execute(const std::string& originalCmd) const = 0;
    virtual ~IAtCommandHandler() = default;
};

// WiFi SET command parameters
struct SetWifiParams {
    std::string ssid;
    std::string password;
    bool valid = false;
};

// TCP client interface
struct ITcpClientAt {
    virtual void flush() = 0;
    virtual ~ITcpClientAt() = default;
};

// Serial interface
struct ISerialAt {
    virtual void println(const char* str) = 0;
    virtual void flush() = 0;
    virtual ~ISerialAt() = default;
};

// ESP interface for reboot
struct IEspAt {
    virtual void restart() = 0;
    virtual ~IEspAt() = default;
};

// AtCommandDispatcher - routes AT commands to handlers
class AtCommandDispatcher {
public:
    using PromptCallback = std::function<void(const char*)>;

    AtCommandDispatcher(ITcpClientAt& tcpClient, ISerialAt& serial, IEspAt& esp);

    // Register a command handler
    void registerHandler(std::unique_ptr<IAtCommandHandler> handler);

    // Handle an AT command from TCP
    void handleTcpCommand(const std::string& cmd);

    // Handle an AT command from Serial
    void handleSerialCommand(const std::string& cmd);

    // Drain serial AT commands - call from main loop()
    void drainSerialCommands();

    // Testable pure functions
    static std::string normalizeAtCommand(const std::string& cmd);
    static std::string buildHeloResponse(const std::array<uint8_t, 16>& deviceId);
    static SetWifiParams parseSetWifiParams(const std::string& params);
    static bool isValidAuthToken(const std::string& received, const std::string& expectedToken);

private:
    ITcpClientAt& tcpClient_;
    ISerialAt& serial_;
    IEspAt& esp_;

    std::vector<std::unique_ptr<IAtCommandHandler>> handlers_;
    std::string serialCmdBuffer_;
    uint32_t serialQuietUntilMs_ = 0;

    void handleCommand(const std::string& cmd, PromptCallback sendPrompt);
    void sendTcpPrompt(const char* response);
    void sendSerialPrompt(const char* response);
    void executeReboot();
};

} // namespace esp32_firmware