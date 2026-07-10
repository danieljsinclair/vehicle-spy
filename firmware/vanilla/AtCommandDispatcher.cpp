#include "AtCommandDispatcher.h"
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace esp32_firmware {

AtCommandDispatcher::AtCommandDispatcher(ITcpClientAt& tcpClient, ISerialAt& serial, IEspAt& esp,
                                         IWifiCredentialStore& wifiStore, IMonitorState& monitor,
                                         const std::array<uint8_t, 16>& deviceId)
    : tcpClient_(tcpClient), serial_(serial), esp_(esp), wifiStore_(wifiStore), monitor_(monitor),
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
    // Mirror the .ino sendPrompt(): print to the client then flush so the reply
    // reaches the socket before any reboot side-effect runs.
    tcpClient_.print(response);
    tcpClient_.flush();
    // Also echo to serial for parity with the firmware prompt path.
    serial_.println(response);
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
