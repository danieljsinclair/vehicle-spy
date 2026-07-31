#pragma once

// ArduinoMock.h - Mock Arduino core APIs for host testing
// Provides stubs for millis(), delay(), pinMode(), digitalRead(), etc.

#include <cstdint>
#include <functional>
#include <mutex>

namespace arduino_mock {

// Global time simulation
class MockTime {
public:
    static uint32_t millis() {
        return instance().current_millis_;
    }

    static void setMillis(uint32_t ms) {
        instance().current_millis_ = ms;
    }

    static void advanceMillis(uint32_t ms) {
        instance().current_millis_ += ms;
    }

    static void reset() {
        instance().current_millis_ = 0;
    }

private:
    MockTime() : current_millis_(0) {}
    static MockTime& instance() {
        static MockTime inst;
        return inst;
    }
    uint32_t current_millis_;
};

// Mock pin modes
enum PinMode {
    INPUT = 0,
    OUTPUT = 1,
    INPUT_PULLUP = 2,
    INPUT_PULLDOWN = 3
};

// Mock digital values
enum DigitalValue {
    LOW = 0,
    HIGH = 1
};

// Pin state tracking
struct PinState {
    PinMode mode = INPUT;
    DigitalValue value = LOW;
    bool initialized = false;
};

class MockPins {
public:
    static void pinMode(uint8_t pin, PinMode mode) {
        auto& pins = instance().pins_;
        if (pin >= pins.size()) pins.resize(pin + 1);
        pins[pin].mode = mode;
        pins[pin].initialized = true;
    }

    static void digitalWrite(uint8_t pin, DigitalValue value) {
        auto& pins = instance().pins_;
        if (pin >= pins.size()) pins.resize(pin + 1);
        pins[pin].value = value;
    }

    static DigitalValue digitalRead(uint8_t pin) {
        auto& pins = instance().pins_;
        if (pin >= pins.size()) return LOW;
        return pins[pin].value;
    }

    static PinState getPinState(uint8_t pin) {
        auto& pins = instance().pins_;
        if (pin >= pins.size()) return PinState{};
        return pins[pin];
    }

    static void reset() {
        instance().pins_.clear();
    }

private:
    MockPins() = default;
    static MockPins& instance() {
        static MockPins inst;
        return inst;
    }
    std::vector<PinState> pins_;
};

// Mock delay functions
inline void delay(uint32_t ms) {
    MockTime::advanceMillis(ms);
}

inline void delayMicroseconds(uint32_t us) {
    // For testing, we don't simulate microseconds
    (void)us;
}

// Mock Serial
class MockSerial {
public:
    using Callback = std::function<void(const char*)>;

    static void begin(uint32_t baud) {
        instance().baud_ = baud;
        instance().buffer_.clear();
    }

    static void end() {
        instance().buffer_.clear();
    }

    static int available() {
        return instance().buffer_.size();
    }

    static int read() {
        if (instance().buffer_.empty()) return -1;
        char c = instance().buffer_.front();
        instance().buffer_.erase(instance().buffer_.begin());
        return c;
    }

    static size_t write(uint8_t c) {
        instance().output_buffer_ += static_cast<char>(c);
        if (instance().write_callback_) {
            instance().write_callback_(instance().output_buffer_.c_str());
        }
        return 1;
    }

    static size_t print(const char* str) {
        instance().output_buffer_ += str;
        if (instance().write_callback_) {
            instance().write_callback_(instance().output_buffer_.c_str());
        }
        return strlen(str);
    }

    static size_t println(const char* str) {
        size_t n = print(str);
        n += print("\r\n");
        return n;
    }

    static void flush() {
        // No-op for mock
    }

    static void setWriteCallback(Callback cb) {
        instance().write_callback_ = std::move(cb);
    }

    static std::string getOutput() {
        return instance().output_buffer_;
    }

    static void clearOutput() {
        instance().output_buffer_.clear();
    }

    static void injectInput(const std::string& input) {
        instance().buffer_.insert(instance().buffer_.end(), input.begin(), input.end());
    }

    static void reset() {
        instance().baud_ = 0;
        instance().buffer_.clear();
        instance().output_buffer_.clear();
        instance().write_callback_ = nullptr;
    }

    static uint32_t getBaud() {
        return instance().baud_;
    }

private:
    MockSerial() : baud_(0) {}
    static MockSerial& instance() {
        static MockSerial inst;
        return inst;
    }
    uint32_t baud_;
    std::vector<char> buffer_;
    std::string output_buffer_;
    Callback write_callback_;
};

// Global mock functions that match Arduino API
inline uint32_t millis() { return MockTime::millis(); }
inline void pinMode(uint8_t pin, uint8_t mode) { MockPins::pinMode(pin, static_cast<PinMode>(mode)); }
inline void digitalWrite(uint8_t pin, uint8_t value) { MockPins::digitalWrite(pin, static_cast<DigitalValue>(value)); }
inline int digitalRead(uint8_t pin) { return static_cast<int>(MockPins::digitalRead(pin)); }

// Serial mock - these need to be defined in a cpp file
namespace Serial {
    inline void begin(uint32_t baud) { MockSerial::begin(baud); }
    inline void end() { MockSerial::end(); }
    inline int available() { return MockSerial::available(); }
    inline int read() { return MockSerial::read(); }
    inline size_t write(uint8_t c) { return MockSerial::write(c); }
    inline size_t print(const char* str) { return MockSerial::print(str); }
    inline size_t println(const char* str) { return MockSerial::println(str); }
    inline void flush() { MockSerial::flush(); }
    inline void setWriteCallback(MockSerial::Callback cb) { MockSerial::setWriteCallback(std::move(cb)); }
    inline std::string getOutput() { return MockSerial::getOutput(); }
    inline void clearOutput() { MockSerial::clearOutput(); }
    inline void injectInput(const std::string& input) { MockSerial::injectInput(input); }
    inline void reset() { MockSerial::reset(); }
    inline uint32_t getBaud() { return MockSerial::getBaud(); }
}

// Reset all mocks
inline void resetAllMocks() {
    MockTime::reset();
    MockPins::reset();
    MockSerial::reset();
}

} // namespace arduino_mock