#pragma once

// TcpManagerConnectionSource.h - IClientConnectionSource implementation backed
// by TcpServerManager's own view of whether a client is adopted.
//
// The .ino previously fed FirmwareApp::setClientConnected() from the global
// WiFiClient's connected() state. That created a driver-cycle desync: when
// TcpServerManager stops the old client (on a new arrival or auth-fail), the
// global WiFiClient's connected() returns false even though TcpServerManager
// may still hold an adopted client — feeding false into selectLedPattern →
// LED out + [STATE] no client.
//
// This adapter queries TcpServerManager::hasClient() (the manager's own view
// of whether a client is adopted), eliminating the desync. It is constructed
// in the .ino over the single TcpServerManager instance and injected into
// FirmwareApp via the IClientConnectionSource seam.

#include "IClientConnectionSource.h"
#include "TcpServerManager.h"

namespace esp32_firmware {

class TcpManagerConnectionSource : public IClientConnectionSource {
public:
    explicit TcpManagerConnectionSource(TcpServerManager& manager)
        : manager_(manager) {}

    bool isClientConnected() const override {
        return manager_.hasClient();
    }

private:
    TcpServerManager& manager_;
};

} // namespace esp32_firmware
