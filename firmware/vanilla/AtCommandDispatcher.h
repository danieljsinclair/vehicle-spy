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
                       IWifiCredentialStore& wifiStore, IMonitorState& monitor,
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
    IMonitorState& monitor_;
    std::array<uint8_t, 16> deviceId_;

    std::vector<std::unique_ptr<IAtCommandHandler>> handlers_;
    bool handlersRegistered_ = false;

    void handleCommand(const std::string& cmd, std::function<void(const char*)> sendPrompt);
    void sendTcpPrompt(const char* response);
    void sendSerialPrompt(const char* response);
    void executeReboot();
};

// ── Concrete firmware command handlers (Command Pattern) ──────────────────────
// One handler per AT command; adding a command is push_back-only (OpenClosed).

struct AtzCommandHandler : public IAtCommandHandler {
    explicit AtzCommandHandler(IMonitorState& monitor) : monitor_(monitor) {}
    bool matches(const std::string& normalizedCmd) const override {
        return normalizedCmd == "ATZ";
    }
    AtCommandResult execute(const std::string& /*originalCmd*/) const override {
        monitor_.setMonitorActive(false);
        return AtCommandResult("ELM327 v2.3");
    }
    IMonitorState& monitor_;
};

struct AteCommandHandler : public IAtCommandHandler {
    bool matches(const std::string& normalizedCmd) const override {
        return normalizedCmd == "ATE0" || normalizedCmd == "ATE1";
    }
    AtCommandResult execute(const std::string& /*originalCmd*/) const override {
        return AtCommandResult("OK");
    }
};

struct AtspCommandHandler : public IAtCommandHandler {
    bool matches(const std::string& normalizedCmd) const override {
        return normalizedCmd.rfind("ATSP", 0) == 0;
    }
    AtCommandResult execute(const std::string& /*originalCmd*/) const override {
        return AtCommandResult("OK");
    }
};

struct AthCommandHandler : public IAtCommandHandler {
    bool matches(const std::string& normalizedCmd) const override {
        return normalizedCmd == "ATH0" || normalizedCmd == "ATH1";
    }
    AtCommandResult execute(const std::string& /*originalCmd*/) const override {
        return AtCommandResult("OK");
    }
};

struct AtcsmCommandHandler : public IAtCommandHandler {
    bool matches(const std::string& normalizedCmd) const override {
        return normalizedCmd == "ATCSM0" || normalizedCmd == "ATCSM1";
    }
    AtCommandResult execute(const std::string& /*originalCmd*/) const override {
        return AtCommandResult("OK");
    }
};

struct AtmaCommandHandler : public IAtCommandHandler {
    explicit AtmaCommandHandler(IMonitorState& monitor) : monitor_(monitor) {}
    bool matches(const std::string& normalizedCmd) const override {
        return normalizedCmd == "ATMA";
    }
    AtCommandResult execute(const std::string& /*originalCmd*/) const override {
        monitor_.setMonitorActive(true);
        return AtCommandResult("OK");
    }
    IMonitorState& monitor_;
};

struct AtpcCommandHandler : public IAtCommandHandler {
    explicit AtpcCommandHandler(IMonitorState& monitor) : monitor_(monitor) {}
    bool matches(const std::string& normalizedCmd) const override {
        return normalizedCmd == "ATPC";
    }
    AtCommandResult execute(const std::string& /*originalCmd*/) const override {
        monitor_.setMonitorActive(false);
        return AtCommandResult("OK");
    }
    IMonitorState& monitor_;
};

struct AtheloCommandHandler : public IAtCommandHandler {
    explicit AtheloCommandHandler(const std::array<uint8_t, 16>& deviceId) : deviceId_(deviceId) {}
    bool matches(const std::string& normalizedCmd) const override {
        return normalizedCmd == "ATHELO" || normalizedCmd == "HELLO";
    }
    AtCommandResult execute(const std::string& /*originalCmd*/) const override {
        return AtCommandResult(AtCommandDispatcher::buildHeloResponse(deviceId_, "ESP32-CAN-Bridge", "0.2.0").c_str());
    }
    const std::array<uint8_t, 16>& deviceId_;
};

struct AtsetwifiCommandHandler : public IAtCommandHandler {
    explicit AtsetwifiCommandHandler(IWifiCredentialStore& wifiStore) : wifiStore_(wifiStore) {}
    bool matches(const std::string& normalizedCmd) const override {
        return normalizedCmd.rfind("ATSETWIFI", 0) == 0;
    }
    AtCommandResult execute(const std::string& originalCmd) const override {
        std::string params = originalCmd.substr(9);  // Skip "ATSETWIFI"
        // trim leading/trailing whitespace
        size_t s = params.find_first_not_of(" \t\r\n");
        size_t e = params.find_last_not_of(" \t\r\n");
        if (s != std::string::npos) params = params.substr(s, e - s + 1);

        SetWifiParams wifiParams = AtCommandDispatcher::parseSetWifiParams(params);

        if (!wifiParams.valid) {
            return AtCommandResult("ERROR Invalid format. Use: ATSETWIFI<ssid>,<pass>");
        }
        if (wifiParams.ssid.empty() || wifiParams.ssid.length() > 32) {
            return AtCommandResult("ERROR Invalid SSID length (1-32 chars)");
        }
        if (wifiParams.password.empty() || wifiParams.password.length() > 64) {
            return AtCommandResult("ERROR Invalid password length (1-64 chars)");
        }
        if (wifiStore_.store(wifiParams.ssid, wifiParams.password)) {
            return AtCommandResult("OK WiFi credentials stored. Rebooting to connect...", true, true);
        }
        return AtCommandResult("ERROR Failed to store credentials");
    }
    IWifiCredentialStore& wifiStore_;
};

struct AtiCommandHandler : public IAtCommandHandler {
    bool matches(const std::string& normalizedCmd) const override {
        return normalizedCmd == "ATI";
    }
    AtCommandResult execute(const std::string& /*originalCmd*/) const override {
        return AtCommandResult("ESP32 CAN Bridge v0.1");
    }
};

struct AtrebootCommandHandler : public IAtCommandHandler {
    bool matches(const std::string& normalizedCmd) const override {
        return normalizedCmd == "ATREBOOT";
    }
    AtCommandResult execute(const std::string& /*originalCmd*/) const override {
        // shouldFlushClient is false on purpose: sendPrompt already flushed the
        // "REBOOT" response. An extra client.flush() here hangs indefinitely on a
        // dead/half-closed socket (ESP32 WiFiClient::flush() has no timeout).
        return AtCommandResult("REBOOT", true, false);
    }
};

} // namespace esp32_firmware
