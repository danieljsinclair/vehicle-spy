#ifndef FIRMWARE_ITCP_SERVER_H
#define FIRMWARE_ITCP_SERVER_H

#include <cstdint>
#include <memory>
#include <string>

namespace esp32_firmware {

// ── TCP Server Client Interface (hardware abstraction for testability) ───────
// Wraps a single connected TCP client (Arduino WiFiClient) so the TCP server
// state machine can be unit-tested with mocks. The real implementation adapts
// WiFiClient; mock implementations record calls for test assertions.
class ITcpServerClient {
public:
    virtual ~ITcpServerClient() = default;

    // True if the underlying connection is currently open.
    virtual bool connected() const = 0;

    // Close the connection and release the underlying socket.
    virtual void stop() = 0;

    // Set the read timeout (ms) for subsequent read calls.
    virtual void setTimeout(uint32_t ms) = 0;

    // Number of bytes available to read without blocking.
    virtual int available() const = 0;

    // Read up to (and excluding) the delimiter. Returns the bytes read; the
    // delimiter itself is consumed but not returned.
    virtual std::string readLine(char delimiter) = 0;

    // Write a line (newline appended) to the client.
    virtual void println(const std::string& line) = 0;

    // Flush any buffered output to the client.
    virtual void flush() = 0;
};

// ── TCP Server Interface (hardware abstraction for testability) ───────────────
// Wraps the listening TCP server (Arduino WiFiServer) so accept/begin/end can
// be exercised under test. accept() returns nullptr when there is no pending
// connection (or the server is not listening).
class ITcpServer {
public:
    virtual ~ITcpServer() = default;

    // Start listening. Idempotent.
    virtual void begin() = 0;

    // Stop listening and close the listening socket.
    virtual void end() = 0;

    // Accept a single pending connection. Returns nullptr if no client is
    // waiting; otherwise a connected ITcpServerClient owned by the caller.
    virtual std::unique_ptr<ITcpServerClient> accept() = 0;
};

// ── TCP Host Callbacks (narrow .ino/FirmwareApp delegation seam) ─────────────
// The TcpServerManager needs to reach four .ino-owned/FirmwareApp-owned
// behaviours that are out of its SRP scope (command dispatch, monitor flag,
// discovery backoff, WiFi-state read). Rather than widen TcpServerManager's
// dependencies to all of FirmwareApp, this interface exposes only those four.
// The .ino supplies a concrete adapter backed by firmwareApp.
class ITcpHostCallbacks {
public:
    virtual ~ITcpHostCallbacks() = default;

    // Route a TCP AT command through the vanilla dispatcher (frames reply as
    // "<resp>\r\r>" for the host HELO handshake).
    virtual void handleTcpAtCommand(const std::string& cmd) = 0;

    // Set the monitor-capture flag (ATMA toggles). Suppresses discovery
    // broadcasts while a buddy is connected.
    virtual void setMonitorActive(bool active) = 0;

    // Reset the discovery backoff timer (welcome a new buddy promptly).
    virtual void resetDiscoveryBackoff() = 0;

    // Current WiFi state model value (esp32_firmware::WiFiState::State as int).
    virtual int getWiFiState() const = 0;
};

} // namespace esp32_firmware

#endif // FIRMWARE_ITCP_SERVER_H
