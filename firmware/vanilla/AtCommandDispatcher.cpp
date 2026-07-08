#include "AtCommandDispatcher.h"
#include <algorithm>
#include <cctype>

namespace esp32_firmware {

AtCommandDispatcher::AtCommandDispatcher(ITcpClientAt& tcpClient, ISerialAt& serial, IEspAt& esp)
    : tcpClient_(tcpClient), serial_(serial), esp_(esp) {}

void AtCommandDispatcher::registerHandler(std::unique_ptr<IAtCommandHandler> handler) {
    handlers_.push_back(std::move(handler));
}

void AtCommandDispatcher::handleTcpCommand(const std::string& cmd) {
    handleCommand(cmd, [this](const char* response) { sendTcpPrompt(response); });
}

void AtCommandDispatcher::handleSerialCommand(const std::string& cmd) {
    handleCommand(cmd, [this](const char* response) { sendSerialPrompt(response); });
}

void AtCommandDispatcher::drainSerialCommands() {
    // In the real implementation, this reads from Serial
    // For testing, we'll use the buffer
    while (!serialCmdBuffer_.empty()) {
        handleSerialCommand(serialCmdBuffer_);
        serialCmdBuffer_.clear();
    }
}

void AtCommandDispatcher::handleCommand(const std::string& cmd, PromptCallback sendPrompt) {
    std::string normalizedCmd = normalizeAtCommand(cmd);

    // Find matching handler
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
            executeReboot();
        }
    } else {
        sendPrompt("?");
    }
}

void AtCommandDispatcher::sendTcpPrompt(const char* response) {
    // In real implementation, this would send to TCP client
    (void)response;
}

void AtCommandDispatcher::sendSerialPrompt(const char* response) {
    serial_.println(response);
}

void AtCommandDispatcher::executeReboot() {
    // Small delay before reboot
    esp_.restart();
}

// Testable pure functions

std::string AtCommandDispatcher::normalizeAtCommand(const std::string& cmd) {
    std::string normalized = cmd;
    // Trim whitespace
    size_t start = normalized.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = normalized.find_last_not_of(" \t\r\n");
    normalized = normalized.substr(start, end - start + 1);
    // Convert to uppercase
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return normalized;
}

std::string AtCommandDispatcher::buildHeloResponse(const std::array<uint8_t, 16>& deviceId) {
    std::array<char, 128> response{};
    int len = std::snprintf(response.data(), response.size(),
        "ACK DEVICE=ESP32-CAN-Bridge FIRMWARE=0.2.0 DEVICEID=");
    const int tailRoom = 3;
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

bool AtCommandDispatcher::isValidAuthToken(const std::string& received, const std::string& expectedToken) {
    std::string expected = "AUTH " + expectedToken;
    return received == expected;
}

} // namespace esp32_firmware