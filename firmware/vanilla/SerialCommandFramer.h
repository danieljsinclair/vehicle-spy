#pragma once

// SerialCommandFramer.h - Vanilla C++ serial command-line framer.
// Extracted from can-bridge.ino::drainSerialATCommands() for host testability.
//
// The .ino's serial AT-command path reads bytes from the USB console, accumulates
// them into a line buffer, and dispatches each CR/LF-terminated line to
// FirmwareApp. The framing rules — which delimiter terminates a line, that empty
// lines are ignored, and that an over-length accumulation is reset — are pure
// logic with no Arduino dependency. This vanilla class owns those rules; the .ino
// supplies an ISerialSource backed by `Serial` and a CommandHandler that forwards
// to FirmwareApp. Mirrors the AtCommandDispatcher DI-seam pattern.
//
// Behavior preserved exactly from the original inline loop:
//   - '\r' OR '\n' terminates the current line and dispatches it (if non-empty).
//   - A terminator with an empty buffer is a no-op (no empty-line dispatch).
//   - Any byte that would grow the buffer past maxLen resets the buffer (overflow
//     guard), discarding the partial line so a runaway sender can't exhaust RAM.

#include <cstddef>
#include <functional>
#include <string>

namespace esp32_firmware {

// Byte source for the framer. read() returns the next byte (0..255) or a
// negative value when no byte is available (mirrors Arduino Serial.read(), which
// returns -1 when the RX buffer is empty).
struct ISerialSource {
    virtual int read() = 0;
    virtual ~ISerialSource() = default;
};

// Invoked once per complete, non-empty line.
using CommandHandler = std::function<void(const std::string&)>;

class SerialCommandFramer {
public:
    // `maxLen` is the per-line byte budget; an accumulation that reaches it resets
    // (matches the .ino's MAX_SERIAL_CMD_LENGTH overflow branch).
    SerialCommandFramer(ISerialSource& source, std::size_t maxLen);

    // Consume every currently-available byte, dispatching each completed line to
    // `handler`. Safe to call once per loop tick (the line buffer persists across
    // calls so a line split across reads still completes).
    void drain(const CommandHandler& handler);

    // Test/inspection seam: the in-progress line (without committing a dispatch).
    const std::string& pendingLine() const noexcept { return buffer_; }

private:
    ISerialSource& source_;
    const std::size_t maxLen_;
    std::string buffer_;

    void handleByte(char c, const CommandHandler& handler);
};

} // namespace esp32_firmware
