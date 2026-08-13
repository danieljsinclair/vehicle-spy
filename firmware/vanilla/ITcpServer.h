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

    // NON-BLOCKING line read for the command-mode path. Only consumes bytes
    // already in the receive buffer; returns "" (and does NOT block) when no
    // complete line is buffered yet. The manager accumulates partial lines in
    // its own buffer and only dispatches a command once a delimiter arrives.
    // This is the fix for the per-tick TX starvation: readLine() (readStringUntil)
    // can block up to the Stream timeout when a line is only partially present,
    // which delays CanBridge::processFrames (the CAN-TX path) by that window.
    virtual std::string readAvailableLine(char delimiter) = 0;

    // Write a line (newline appended) to the client.
    virtual void println(const std::string& line) = 0;

    // Flush any buffered output to the client.
    virtual void flush() = 0;

    // Toggle TCP_NODELAY on the underlying client socket (disable Nagle's
    // algorithm). The single most important latency lever for the CAN stream:
    // without it a <MSS frame can sit in the LwIP send buffer up to the
    // delayed-ACK window (~40-200 ms). Set true on accept for every client.
    virtual void setNoDelay(bool enable) = 0;

    // Remote IP address of the connected peer as a string ("ipv4" or "ipv6").
    // Empty string when not connected. Used by TcpServerManager to capture the
    // client IP at accept time for observability events and state snapshots.
    virtual std::string remoteIP() const = 0;
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

    // ── Serial observability callbacks (FirmwareApp owns the single IEventLogger) ─
    // TcpServerManager calls these at client lifecycle transitions; FirmwareApp
    // routes them to its owned IEventLogger. No logger is injected into the manager.
    virtual void onClientConnected(const std::string& ip) = 0;
    virtual void onAuthFailed(const std::string& ip) = 0;
    virtual void onClientDisconnected(const std::string& ip, int reason) = 0;
};

} // namespace esp32_firmware

#endif // FIRMWARE_ITCP_SERVER_H
