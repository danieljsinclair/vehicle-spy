#include "SerialCommandFramer.h"

namespace esp32_firmware {

SerialCommandFramer::SerialCommandFramer(ISerialSource& source, std::size_t maxLen)
    : source_(source), maxLen_(maxLen) {}

void SerialCommandFramer::drain(const CommandHandler& handler) {
    // Read every currently-available byte (read() returns < 0 when empty),
    // mirroring the original `while (Serial.available())` loop.
    while (true) {
        const int next = source_.read();
        if (next < 0) {
            break;
        }
        handleByte(static_cast<char>(next), handler);
    }
}

void SerialCommandFramer::handleByte(char c, const CommandHandler& handler) {
    if (c == '\r' || c == '\n') {
        // Terminate the current line; dispatch only if it is non-empty (an empty
        // line between two terminators is a no-op, not an empty dispatch).
        if (!buffer_.empty()) {
            handler(buffer_);
            buffer_.clear();
        }
        return;
    }

    buffer_.push_back(c);
    // Overflow guard: a line that exceeds the budget is discarded in full so a
    // runaway sender cannot grow the buffer without bound. The byte just pushed
    // counts toward the budget, so the reset triggers once size() exceeds maxLen_.
    if (buffer_.size() > maxLen_) {
        buffer_.clear();
    }
}

} // namespace esp32_firmware
