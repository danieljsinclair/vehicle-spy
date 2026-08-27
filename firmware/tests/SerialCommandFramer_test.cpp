#include "SerialCommandFramer.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <deque>
#include <string>
#include <vector>

using esp32_firmware::CommandHandler;
using esp32_firmware::ISerialSource;
using esp32_firmware::SerialCommandFramer;
using ::testing::_;
using ::testing::InSequence;

namespace {

// In-memory byte source: feeds a scripted stream, returning -1 (empty) once the
// stream is exhausted. This stands in for Arduino Serial so the framer's logic
// is driven deterministically without hardware.
class FakeSerialSource : public ISerialSource {
public:
    explicit FakeSerialSource(std::vector<int> bytes) : bytes_(std::move(bytes)) {}

    int read() override {
        if (pos_ >= bytes_.size()) {
            return -1;  // empty, like Serial.read() when !available()
        }
        return bytes_[pos_++];
    }

private:
    std::vector<int> bytes_;
    std::size_t pos_ = 0;
};

// Collects dispatched lines so a test can assert which lines the framer emitted.
std::vector<std::string> captureDispatched(SerialCommandFramer& framer) {
    std::vector<std::string> got;
    framer.drain([&](const std::string& line) { got.push_back(line); });
    return got;
}

// FakeUartStream simulates the ESP32 hardware UART RX path: a bounded ring buffer
// (default 256 bytes, matching the ESP32 Arduino-core HardwareSerial default) and
// the permanent-lockup behavior documented in arduino-esp32#6326: once the ring
// buffer overflows, read() returns -1 forever (the UART RX is inert until reboot).
//
// On real ESP32 hardware the overflow clears the ring-buffer contents (the ISR
// stops servicing the FIFO), so pending bytes are lost along with the lockup.
// This simulation mirrors that: overflow drains the buffer and sets the lock.
class FakeUartStream : public ISerialSource {
public:
    explicit FakeUartStream(size_t bufferSize = 256) : bufferSize_(bufferSize) {}

    // Feed bytes into the simulated UART RX FIFO.  Bytes beyond the ring-buffer
    // capacity trigger the lockup (matching the ESP32 Arduino-core bug).
    void receive(const std::string& data) {
        if (locked_) return;
        for (unsigned char c : data) {
            if (buffer_.size() < bufferSize_) {
                buffer_.push_back(c);
            } else {
                // Overflow: drain the buffer (bytes are lost in the hardware
                // lockup) and lock the UART permanently.
                buffer_.clear();
                locked_ = true;
                break;
            }
        }
    }

    // Lock the UART manually (e.g., to simulate a prior overflow).
    void lockUp() { locked_ = true; buffer_.clear(); }

    int read() override {
        if (locked_) return -1;
        if (buffer_.empty()) return -1;
        unsigned char c = buffer_.front();
        buffer_.pop_front();
        return static_cast<int>(c);
    }

    bool isLocked() const { return locked_; }
    size_t pendingBytes() const { return buffer_.size(); }

private:
    std::deque<unsigned char> buffer_;
    size_t bufferSize_;
    bool locked_ = false;
};

} // namespace

// Happy path: a CR-terminated line is dispatched verbatim. This is the AT-command
// console's primary path (a host sends "ATZ\r").
TEST(SerialCommandFramerTest, DispatchesLineTerminatedByCarriageReturn) {
    FakeSerialSource src{{'A', 'T', 'Z', '\r'}};
    SerialCommandFramer framer(src, 64);

    const auto got = captureDispatched(framer);
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0], "ATZ");
}

// Contract: '\n' is also a terminator (a host sending LF-only lines is handled).
TEST(SerialCommandFramerTest, DispatchesLineTerminatedByNewline) {
    FakeSerialSource src{{'A', 'T', 'M', 'A', '\n'}};
    SerialCommandFramer framer(src, 64);

    const auto got = captureDispatched(framer);
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0], "ATMA");
}

// Contract: multiple complete lines in one drain are each dispatched, in order.
TEST(SerialCommandFramerTest, DispatchesMultipleLinesInOrder) {
    FakeSerialSource src{{'A', 'T', 'Z', '\r', 'A', 'T', 'M', 'A', '\r'}};
    SerialCommandFramer framer(src, 64);

    const auto got = captureDispatched(framer);
    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0], "ATZ");
    EXPECT_EQ(got[1], "ATMA");
}

// Contract: an empty line (a terminator with nothing accumulated) does NOT
// dispatch. The original loop guards `if (!serialCmd.isEmpty())` — this pins it
// so a blank line never produces an empty command.
TEST(SerialCommandFramerTest, EmptyLineIsNotDispatched) {
    FakeSerialSource src{{'\r', '\n', '\r'}};
    SerialCommandFramer framer(src, 64);

    const auto got = captureDispatched(framer);
    EXPECT_TRUE(got.empty()) << "a terminator with an empty buffer must not dispatch";
}

// Contract: a CRLF-terminated line dispatches once, not twice. The '\r' dispatches
// the line and clears the buffer, so the following '\n' sees an empty buffer and
// is a no-op (the empty-line rule above). Pins that CR+LF is one line, not two.
TEST(SerialCommandFramerTest, CrlfTerminatorDispatchesOnceNotTwice) {
    FakeSerialSource src{{'A', 'T', 'Z', '\r', '\n'}};
    SerialCommandFramer framer(src, 64);

    const auto got = captureDispatched(framer);
    ASSERT_EQ(got.size(), 1u) << "CRLF must dispatch a single line";
    EXPECT_EQ(got[0], "ATZ");
}

// Contract: a line split across two drains completes (the buffer persists across
// calls). This mirrors the realistic case where Serial has a partial line ready
// on one loop tick and the rest on the next. The framer holds a reference to the
// SAME source across ticks, so we refill that source's backing stream between
// drains and assert: first drain (partial) dispatches nothing and leaves the
// partial buffer; second drain (remainder) dispatches the completed line.
namespace {
// A source whose backing stream can be refilled, modeling Serial receiving more
// bytes on a later loop tick.
class RefillableSerialSource : public ISerialSource {
public:
    void append(const std::vector<int>& bytes) {
        bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
    }
    int read() override {
        if (pos_ >= bytes_.size()) return -1;
        return bytes_[pos_++];
    }

private:
    std::vector<int> bytes_;
    std::size_t pos_ = 0;
};
} // namespace

TEST(SerialCommandFramerTest, LineCompletesAcrossMultipleDrains) {
    RefillableSerialSource src;
    SerialCommandFramer framer(src, 64);

    src.append({'A', 'T'});  // partial line ready on tick 1
    std::vector<std::string> got;
    framer.drain([&](const std::string& line) { got.push_back(line); });
    EXPECT_TRUE(got.empty()) << "partial line must not dispatch";
    EXPECT_EQ(framer.pendingLine(), "AT");

    src.append({'Z', '\r'});  // remainder arrives on tick 2
    framer.drain([&](const std::string& line) { got.push_back(line); });
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0], "ATZ");
    EXPECT_TRUE(framer.pendingLine().empty());
}

// Overflow guard (preserves the original .ino behavior exactly): once the
// in-progress buffer EXCEEDS maxLen it is reset to empty, so the buffer can never
// grow without bound. The reset happens on the byte that crosses the threshold;
// accumulation then resumes from the FOLLOWING byte (the original loop's
// `serialCmd += c; if (length() > MAX) serialCmd = ""` only drops what it has so
// far, not the remainder of the runaway line). This pins that real behavior — a
// stronger "discard until next terminator" contract would CHANGE behavior and is
// intentionally NOT claimed.
TEST(SerialCommandFramerTest, OverLengthBufferResetsAndAccumulationResumes) {
    // Budget 4: bytes 1-4 fill the buffer; byte 5 crosses the threshold (5>4) and
    // resets it to empty; byte 6 then starts a fresh "x" accumulation; the
    // following ATZ\r appends to that fresh buffer, dispatching "xATZ".
    FakeSerialSource src{{'x', 'x', 'x', 'x', 'x', 'x', 'A', 'T', 'Z', '\r'}};
    SerialCommandFramer framer(src, 4);

    const auto got = captureDispatched(framer);
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0], "xATZ");  // 5th 'x' reset the buffer; 6th 'x' began a new line
}

// Overflow guard core contract (the part that matters for RAM safety): no matter
// how many non-terminator bytes arrive, the pending buffer never grows past
// maxLen+1. This is the regression guard for the overflow fix — a missing reset
// would let the buffer grow to the stream length.
TEST(SerialCommandFramerTest, PendingBufferNeverExceedsMaxLengthPlusOne) {
    // Budget 4 with a long non-terminating stream: after drain, the buffer must be
    // at most maxLen+1 (== 5) bytes, not the full stream length.
    FakeSerialSource src{{'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a'}};
    SerialCommandFramer framer(src, 4);

    captureDispatched(framer);  // no terminator -> nothing dispatched
    EXPECT_LE(framer.pendingLine().size(), 5u)
        << "buffer must reset on overflow; got: \"" << framer.pendingLine() << "\"";
}

// Boundary: a line exactly at the budget dispatches normally (overflow triggers
// only when size EXCEEDS maxLen, matching `length() > MAX`).
TEST(SerialCommandFramerTest, LineAtExactlyMaxLenDispatchesNormally) {
    FakeSerialSource src{{'A', 'T', 'Z', '\r'}};
    SerialCommandFramer framer(src, 3);  // "ATZ" is exactly 3 bytes == budget

    const auto got = captureDispatched(framer);
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0], "ATZ");
}

// Contract: drain stops when the source reports empty (read < 0) and does not
// spin. Uses a gmock source so the call count is observable.
namespace {
class MockSerialSource : public ISerialSource {
public:
    MOCK_METHOD(int, read, (), (override));
};
} // namespace

TEST(SerialCommandFramerTest, DrainStopsWhenSourceReportsEmpty) {
    MockSerialSource src;
    {
        InSequence seq;
        EXPECT_CALL(src, read()).WillOnce(::testing::Return('A'));
        EXPECT_CALL(src, read()).WillOnce(::testing::Return('T'));
        EXPECT_CALL(src, read()).WillOnce(::testing::Return('Z'));
        EXPECT_CALL(src, read()).WillOnce(::testing::Return('\r'));
        EXPECT_CALL(src, read()).WillOnce(::testing::Return(-1));  // empty -> stop
    }

    SerialCommandFramer framer(src, 64);
    std::vector<std::string> got;
    framer.drain([&](const std::string& line) { got.push_back(line); });
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0], "ATZ");
}

// ── Red test: UART RX lockup wedge ────────────────────────────────────────────
// Reproduces the production bug where loop() delays drainSerialATCommands()
// long enough for the ESP32 256-byte RX ring buffer to overflow, after which
// the Arduino-core UART driver locks up permanently (arduino-esp32#6326).
// Once locked, read() returns -1 forever — the framer is blind even when
// valid bytes were sent by the host.
//
// Post-fix: drain() runs at the TOP of loop(), so ATCLEARWIFI is dispatched
// before the overflow window can destroy it.  The overflow still locks the
// UART, but the AT command path already handled the command.
TEST(SerialCommandFramerTest, UartLockupAfterDelayedDrainMakesFramerBlind) {
    // 256-byte ring buffer matches the ESP32 Arduino-core HardwareSerial default.
    FakeUartStream stream(256);
    SerialCommandFramer framer(stream, 64);

    std::vector<std::string> dispatched;

    // Fix: drain() at the TOP of loop() — runs before any blocking operation.
    // ATCLEARWIFI arrived during the previous tick's idle window and is
    // sitting in the ring buffer.  drain() reads and dispatches it immediately.
    stream.receive("ATCLEARWIFI\r");
    framer.drain([&](const std::string& line) { dispatched.push_back(line); });
    ASSERT_EQ(dispatched.size(), 1u);
    EXPECT_EQ(dispatched[0], "ATCLEARWIFI");

    // Now simulate the same blocking delay + overflow that wedged the device.
    stream.receive(std::string(300, 'X'));
    EXPECT_TRUE(stream.isLocked()) << "overflow must still lock the UART";

    // The command was already handled; further drain() calls are futile but
    // harmless — the AT path is already satisfied.
    dispatched.clear();
    framer.drain([&](const std::string& line) { dispatched.push_back(line); });
    EXPECT_TRUE(dispatched.empty()) << "no new commands after lockup";
}

// Same hardware scenario, but drain() is called BEFORE the overflow-causing
// delay (the fix: move drainSerialATCommands to the top of loop()).  The
// command is drained and dispatched before the buffer can overflow.
TEST(SerialCommandFramerTest, EarlyDrainDispatchesCommandBeforeOverflow) {
    FakeUartStream stream(256);
    SerialCommandFramer framer(stream, 64);

    std::vector<std::string> dispatched;

    // Fix: drain() at the TOP of loop() — runs before any blocking operation.
    // ATCLEARWIFI arrived during the previous tick's idle window and is
    // sitting in the ring buffer.  drain() reads and dispatches it immediately.
    stream.receive("ATCLEARWIFI\r");
    framer.drain([&](const std::string& line) { dispatched.push_back(line); });
    ASSERT_EQ(dispatched.size(), 1u);
    EXPECT_EQ(dispatched[0], "ATCLEARWIFI");

    // Now simulate the same blocking delay + overflow that wedged the device.
    stream.receive(std::string(300, 'X'));
    EXPECT_TRUE(stream.isLocked()) << "overflow must still lock the UART";

    // The command was already handled; further drain() calls are futile but
    // harmless — the AT path is already satisfied.
    dispatched.clear();
    framer.drain([&](const std::string& line) { dispatched.push_back(line); });
    EXPECT_TRUE(dispatched.empty()) << "no new commands after lockup";
}
