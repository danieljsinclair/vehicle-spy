#include "AtCommandDispatcher.h"
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace esp32_firmware {

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

// Diagnostic: report the stored WiFi credential state as the boot reader sees
// it (same loadCredentialsImpl path). Does not print the password.
struct AtdumpwifiCommandHandler : public IAtCommandHandler {
    explicit AtdumpwifiCommandHandler(IWifiCredentialStore& wifiStore) : wifiStore_(wifiStore) {}
    bool matches(const std::string& normalizedCmd) const override {
        return normalizedCmd == "ATDUMPWIFI";
    }
    AtCommandResult execute(const std::string& /*originalCmd*/) const override {
        std::string ssid, pass;
        if (wifiStore_.load(ssid, pass)) {
            const std::string msg = "OK stored ssid=" + ssid +
                                    " pass_len=" + std::to_string(pass.size());
            return AtCommandResult(msg.c_str());
        }
        return AtCommandResult("OK no stored credentials");
    }
    IWifiCredentialStore& wifiStore_;
};

struct AtsettokenCommandHandler : public IAtCommandHandler {
    explicit AtsettokenCommandHandler(IWifiTokenStore& tokenStore) : tokenStore_(tokenStore) {}
    bool matches(const std::string& normalizedCmd) const override {
        return normalizedCmd.rfind("ATSETTOKEN", 0) == 0;
    }
    AtCommandResult execute(const std::string& originalCmd) const override {
        std::string params = originalCmd.substr(10);  // Skip "ATSETTOKEN"
        // trim leading/trailing whitespace
        size_t s = params.find_first_not_of(" \t\r\n");
        if (s == std::string::npos) {
            params.clear();  // all whitespace -> empty
        } else {
            size_t e = params.find_last_not_of(" \t\r\n");
            params = params.substr(s, e - s + 1);
        }

        if (params.empty()) {
            return AtCommandResult("ERROR Token cannot be empty");
        }
        if (params.length() > 64) {
            return AtCommandResult("ERROR Token too long (max 64 chars)");
        }
        if (tokenStore_.storeToken(params)) {
            return AtCommandResult("OK Token stored. Rebooting...", true, true);
        }
        return AtCommandResult("ERROR Failed to store token");
    }
    IWifiTokenStore& tokenStore_;
};

struct AtclearwifiCommandHandler : public IAtCommandHandler {
    explicit AtclearwifiCommandHandler(IWifiCredentialClear& clearFn) : clearFn_(clearFn) {}
    bool matches(const std::string& normalizedCmd) const override {
        return normalizedCmd == "ATCLEARWIFI";
    }
    AtCommandResult execute(const std::string&) const override {
        if (clearFn_.clear()) {
            return AtCommandResult("OK WiFi credentials cleared. Rebooting...", true, true);
        }
        return AtCommandResult("ERROR Failed to clear credentials");
    }
    IWifiCredentialClear& clearFn_;
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

// ── AtCommandDispatcher method implementations ────────────────────────────────

AtCommandDispatcher::AtCommandDispatcher(ITcpClientAt& tcpClient, ISerialAt& serial, IEspAt& esp,
                                         IWifiCredentialStore& wifiStore, IWifiTokenStore& tokenStore,
                                         IWifiCredentialClear& credClear, IMonitorState& monitor,
                                         const std::array<uint8_t, 16>& deviceId)
    : tcpClient_(tcpClient), serial_(serial), esp_(esp), wifiStore_(wifiStore),
      tokenStore_(tokenStore), credClear_(credClear), monitor_(monitor),
      deviceId_(deviceId) {}

void AtCommandDispatcher::registerHandler(std::unique_ptr<IAtCommandHandler> handler) {
    handlers_.push_back(std::move(handler));
}

void AtCommandDispatcher::registerFirmwareHandlers() {
    if (handlersRegistered_) return;
    handlersRegistered_ = true;

    registerHandler(std::make_unique<AtzCommandHandler>(monitor_));
    registerHandler(std::make_unique<AteCommandHandler>());
    registerHandler(std::make_unique<AtspCommandHandler>());
    registerHandler(std::make_unique<AthCommandHandler>());
    registerHandler(std::make_unique<AtcsmCommandHandler>());
    registerHandler(std::make_unique<AtmaCommandHandler>(monitor_));
    registerHandler(std::make_unique<AtpcCommandHandler>(monitor_));
    registerHandler(std::make_unique<AtheloCommandHandler>(deviceId_));
    registerHandler(std::make_unique<AtsetwifiCommandHandler>(wifiStore_));
    registerHandler(std::make_unique<AtdumpwifiCommandHandler>(wifiStore_));
    registerHandler(std::make_unique<AtsettokenCommandHandler>(tokenStore_));
    registerHandler(std::make_unique<AtclearwifiCommandHandler>(credClear_));
    registerHandler(std::make_unique<AtiCommandHandler>());
    registerHandler(std::make_unique<AtrebootCommandHandler>());
}

void AtCommandDispatcher::handleTcpCommand(const std::string& cmd) {
    handleCommand(cmd, [this](const char* response) { sendTcpPrompt(response); });
}

void AtCommandDispatcher::handleSerialCommand(const std::string& cmd) {
    handleCommand(cmd, [this](const char* response) { sendSerialPrompt(response); });
}

void AtCommandDispatcher::handleCommand(const std::string& cmd,
                                       std::function<void(const char*)> sendPrompt) {
    registerFirmwareHandlers();

    std::string normalizedCmd = normalizeAtCommand(cmd);

    const IAtCommandHandler* matchingHandler = nullptr;
    for (const auto& handler : handlers_) {
        if (handler->matches(normalizedCmd)) {
            matchingHandler = handler.get();
            break;
        }
    }

    if (matchingHandler) {
        AtCommandResult result = matchingHandler->execute(cmd);
        sendPrompt(result.response.c_str());

        if (result.shouldFlushClient) {
            tcpClient_.flush();
            serial_.println("REBOOT");
            serial_.flush();
        }

        if (result.shouldReboot) {
            // Small delay before reboot (Constants::TCP_REBOOT_DELAY_MS).
            executeReboot();
        }
    } else {
        sendPrompt("?");
    }
}

void AtCommandDispatcher::sendTcpPrompt(const char* response) {
    // Faithful to the device's historical sendPrompt(): emit the ELM327-style
    // initiator prompt "<response>\r\r>" to the *TCP client only*, then flush so
    // the reply reaches the socket before any reboot side-effect runs.
    //
    // The trailing "\r\r>" is NOT cosmetic: TCPTransport::sendHeloAndParseAck on
    // the host waits for the "\r\r>" terminator after ATHELO (and "\r>" after
    // ATI) to know the line is complete. Without it the host handshake stalls.
    // We do NOT echo to Serial here — the serial command path (sendSerialPrompt)
    // owns the serial console; mixing the two would double-print to USB.
    std::string framed = std::string(response) + "\r\r>";
    tcpClient_.print(framed.c_str());
    tcpClient_.flush();
}

void AtCommandDispatcher::sendSerialPrompt(const char* response) {
    serial_.println(response);
}

void AtCommandDispatcher::executeReboot() {
    // The pre-reboot delay is platform-specific; the firmware's IEspAt::restart()
    // owns delay(Constants::TCP_REBOOT_DELAY_MS) before the actual ESP.restart().
    esp_.restart();
}

// ── Testable pure functions ───────────────────────────────────────────────────

std::string AtCommandDispatcher::normalizeAtCommand(const std::string& cmd) {
    std::string normalized = cmd;
    size_t start = normalized.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = normalized.find_last_not_of(" \t\r\n");
    normalized = normalized.substr(start, end - start + 1);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return normalized;
}

std::string AtCommandDispatcher::buildHeloResponse(const std::array<uint8_t, 16>& deviceId,
                                                  const char* deviceName,
                                                  const char* firmwareVersion) {
    std::array<char, 128> response{};
    int len = std::snprintf(response.data(), response.size(),
        "ACK DEVICE=%s FIRMWARE=%s DEVICEID=", deviceName, firmwareVersion);
    const int tailRoom = 3;  // one more "%02X" + trailing "\r"
    size_t i = 0;
    while (i < deviceId.size() && len < static_cast<int>(response.size()) - tailRoom) {
        len += std::snprintf(response.data() + len, response.size() - len, "%02X", deviceId[i]);
        ++i;
    }
    std::snprintf(response.data() + len, response.size() - len, "\r");
    return std::string(response.data());
}

SetWifiParams AtCommandDispatcher::parseSetWifiParams(const std::string& params) {
    SetWifiParams result;

    size_t commaIndex = params.find(',');
    if (commaIndex == std::string::npos || commaIndex == 0) {
        return result;  // Invalid format
    }

    result.ssid = params.substr(0, commaIndex);
    result.password = params.substr(commaIndex + 1);
    result.valid = true;

    return result;
}

bool AtCommandDispatcher::isValidAuthToken(const std::string& received,
                                          const std::string& expectedToken) {
    std::string expected = "AUTH " + expectedToken;
    return received == expected;
}

} // namespace esp32_firmware
