#pragma once

// ArduinoTcpServer.h - Arduino WiFiServer adapter for ITcpServer.
// Bridges the listening TCP server (Arduino WiFiServer) to the vanilla
// ITcpServer interface used by TcpServerManager.
//
// accept() does the .ino-global sync: WiFiServer::accept() returns a WiFiClient
// by value; if it's a real connection, the adapter assigns it into the .ino's
// global `client` (the single connection truth source that ArduinoTcpClient
// reads) and returns an ArduinoTcpServerClient wrapping that global by
// reference. This keeps the manager's lifecycle and the .ino's connection-state
// readers in sync (one underlying socket, one truth). FirmwareApp queries the
// client-adoption state via IClientConnectionSource (TcpManagerConnectionSource
// over TcpServerManager::hasClient()), not via the global WiFiClient.
//
// Production implementation used in the .ino. Host tests use the mock in
// TcpServerManager_test.cpp. Only #included by can-bridge.ino (device-only), so
// no #ifdef ARDUINO guard is needed.

#include <WiFiServer.h>
#include <WiFiClient.h>
#include "ITcpServer.h"
#include "ArduinoTcpServerClient.h"

#include <memory>

namespace esp32_firmware {

class ArduinoTcpServer : public ITcpServer {
public:
    // server: the listening socket (owned by the .ino as a static global).
    // client: the .ino's global connected-client slot; accept() assigns into it.
    ArduinoTcpServer(WiFiServer& server, WiFiClient& client)
        : server_(server), client_(client) {}

    void begin() override { server_.begin(); }
    void end() override { server_.end(); }

    std::unique_ptr<ITcpServerClient> accept() override {
        WiFiClient next = server_.accept();
        if (!next) {
            return nullptr;  // no pending connection
        }
        // Adopt into the global slot so ArduinoTcpClient sees the new connection.
        // If an old socket is still here, assigning replaces it (the prior
        // WiFiClient destructor closes it). FirmwareApp queries the adoption
        // state via IClientConnectionSource (TcpServerManager::hasClient()).
        client_ = next;
        return std::make_unique<ArduinoTcpServerClient>(client_);
    }

private:
    WiFiServer& server_;
    WiFiClient& client_;
};

} // namespace esp32_firmware
