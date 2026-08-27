#include "WiFiManager.h"
// TcpServerRestartPolicy.cpp - TCP server restart policy implementation

#include "TcpServerRestartPolicy.h"

namespace esp32_firmware {

TcpServerRestartPolicy::TcpServerRestartPolicy(WiFiManager& wifiManager,
                                               std::function<void()> restartTcpServer)
    : wifiManager_(wifiManager)
    , restartTcpServer_(std::move(restartTcpServer)) {
}

bool TcpServerRestartPolicy::shouldRestart() const {
    return wifiManager_.shouldRestartTcpServer();
}

void TcpServerRestartPolicy::clear() {
    wifiManager_.clearTcpServerRestartFlag();
}

} // namespace esp32_firmware
