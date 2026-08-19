#pragma once

// AtCommandDispatcher.h - Vanilla C++ AT command handling
// Extracted from can-bridge.ino for host testability.
//
// This is the canonical, host-tested AT core. The firmware .ino delegates its
// `handleAT` / `handleSerialAT` loops to a single AtCommandDispatcher instance,
// injecting the four runtime boundaries it cannot own itself:
//   - ITcpClientAt     : flush the connected TCP client before reboot
//   - ISerialAt        : echo prompts to the USB serial console
//   - IEspAt           : perform the actual ESP restart
//   - IWifiCredentialStore : persist/store WiFi SSID+password to NVS
//   - IMonitorState    : toggle the CAN monitor flag (ATZ/ATMA/ATPC)
//
// Pure helpers (normalizeAtCommand, buildHeloResponse, parseSetWifiParams,
// isValidAuthToken) are static and side-effect free so the .ino's existing
// behavior can be mirrored and locked down by tests without a device.

#include <array>
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

// WiFi credential persistence (NVS on-device; faked in tests)
struct IWifiCredentialStore {
    virtual bool store(const std::string& ssid, const std::string& password) = 0;
    virtual ~IWifiCredentialStore() = default;
};

// TCP auth token persistence (NVS on-device; faked in tests)
struct IWifiTokenStore {
    virtual bool storeToken(const std::string& token) = 0;
    virtual ~IWifiTokenStore() = default;
};

// WiFi credential clear (factory-reset / ATCLEARWIFI)
struct IWifiCredentialClear {
    virtual bool clear() = 0;
    virtual ~IWifiCredentialClear() = default;
};

// CAN monitor flag the firmware loop reads each tick.
struct IMonitorState {
    virtual void setMonitorActive(bool active) = 0;
    virtual ~IMonitorState() = default;
};

// WiFi SET command parameters
struct SetWifiParams {
    std::string ssid;
    std::string password;
    bool valid = false;
};

// TCP client interface
struct ITcpClientAt {
    virtual void print(const char* str) = 0;
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
    // deviceId: 16-byte discovery device id echoed by ATHELO.
    // rebootDelayMs: delay before ESP.restart() (matches Constants::TCP_REBOOT_DELAY_MS).
    AtCommandDispatcher(ITcpClientAt& tcpClient, ISerialAt& serial, IEspAt& esp,
                       IWifiCredentialStore& wifiStore, IWifiTokenStore& tokenStore,
                       IWifiCredentialClear& credClear, IMonitorState& monitor,
                       const std::array<uint8_t, 16>& deviceId);

    // Register a command handler
    void registerHandler(std::unique_ptr<IAtCommandHandler> handler);

    // Register the canonical firmware command set once (idempotent).
    void registerFirmwareHandlers();

    // Handle an AT command from TCP
    void handleTcpCommand(const std::string& cmd);

    // Handle an AT command from Serial
    void handleSerialCommand(const std::string& cmd);

    // Testable pure functions
    static std::string normalizeAtCommand(const std::string& cmd);
    static std::string buildHeloResponse(const std::array<uint8_t, 16>& deviceId,
                                         const char* deviceName, const char* firmwareVersion);
    static SetWifiParams parseSetWifiParams(const std::string& params);
    static bool isValidAuthToken(const std::string& received, const std::string& expectedToken);

private:
    ITcpClientAt& tcpClient_;
    ISerialAt& serial_;
    IEspAt& esp_;
    IWifiCredentialStore& wifiStore_;
    IWifiTokenStore& tokenStore_;
    IWifiCredentialClear& credClear_;
    IMonitorState& monitor_;
    std::array<uint8_t, 16> deviceId_;

    std::vector<std::unique_ptr<IAtCommandHandler>> handlers_;
    bool handlersRegistered_ = false;

    void handleCommand(const std::string& cmd, std::function<void(const char*)> sendPrompt);
    void sendTcpPrompt(const char* response);
    void sendSerialPrompt(const char* response);
    void executeReboot();
};

} // namespace esp32_firmware
