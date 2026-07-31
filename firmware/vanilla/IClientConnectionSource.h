#pragma once

// IClientConnectionSource.h - Narrow seam for querying whether a TCP client
// is currently adopted by the server.
//
// FirmwareApp previously relied on the .ino calling setClientConnected() with
// the raw global WiFiClient's connected() state. That created a driver-cycle
// desync: when TcpServerManager stops the old client (on a new arrival or
// auth-fail), the global WiFiClient's connected() returns false even though
// TcpServerManager may still hold an adopted client — feeding false into
// selectLedPattern → LED out + [STATE] no client.
//
// This interface lets FirmwareApp query the connection source through DI
// instead of a free-function callback, making the wiring host-testable. The
// .ino implements it over TcpServerManager::hasClient() (the manager's own
// view of whether a client is adopted), eliminating the desync.

namespace esp32_firmware {

class IClientConnectionSource {
public:
    virtual ~IClientConnectionSource() = default;

    // True if a TCP client is currently adopted by the server.
    virtual bool isClientConnected() const = 0;
};

} // namespace esp32_firmware
