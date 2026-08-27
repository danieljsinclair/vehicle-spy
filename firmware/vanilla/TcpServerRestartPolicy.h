// TcpServerRestartPolicy.h - TCP server restart flag policy
// Extracted from FirmwareApp for S1448 method-count compliance.
//
// Owns the shouldRestartTcpServer / clearTcpServerRestartFlag concern.
// FirmwareApp delegates TCP-restart flag management to this policy.

#pragma once

#include <functional>

namespace esp32_firmware {

class WiFiManager;

// TcpServerRestartPolicy: owns TCP server restart flag management.
// Separated from FirmwareApp for S1448 compliance.
class TcpServerRestartPolicy {
public:
    TcpServerRestartPolicy(WiFiManager& wifiManager,
                           std::function<void()> restartTcpServer);

    // Check if TCP server needs restart (WiFi reconnected with new IP).
    bool shouldRestart() const;

    // Clear the restart flag (called after restart is performed).
    void clear();

private:
    WiFiManager& wifiManager_;
    std::function<void()> restartTcpServer_;
};

} // namespace esp32_firmware
