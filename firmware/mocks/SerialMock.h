#pragma once

// SerialMock.h - Mock Serial for host testing
// Implements ISerialCan and ISerialAt interfaces

#include <string>
#include <vector>
#include <functional>
#include "ArduinoMock.h"

namespace esp32_firmware {

class SerialMock : public ISerialCan, public ISerialAt {
public:
    SerialMock() = default;

    // ISerialCan interface
    size_t print(const char* str) override {
        outputBuffer_ += str;
        if (writeCallback_) {
            writeCallback_(str);
        }
        return str ? std::strlen(str) : 0;
    }

    void flush() override {
        // No-op for mock
    }

    // ISerialAt interface
    void println(const char* str) override {
        print(str);
        print("\r\n");
    }

    // Test helpers
    void setWriteCallback(std::function<void(const char*)> cb) {
        writeCallback_ = std::move(cb);
    }

    std::string getOutput() const {
        return outputBuffer_;
    }

    void clearOutput() {
        outputBuffer_.clear();
    }

    void injectInput(const std::string& input) {
        inputBuffer_ = input;
        inputPos_ = 0;
    }

    // Simulate Serial.available() and Serial.read() for ArduinoMock
    int available() const {
        return static_cast<int>(inputBuffer_.size() - inputPos_);
    }

    int read() {
        if (inputPos_ >= inputBuffer_.size()) return -1;
        return inputBuffer_[inputPos_++];
    }

    void reset() {
        outputBuffer_.clear();
        inputBuffer_.clear();
        inputPos_ = 0;
        writeCallback_ = nullptr;
    }

private:
    std::string outputBuffer_;
    std::string inputBuffer_;
    size_t inputPos_ = 0;
    std::function<void(const char*)> writeCallback_;
};

} // namespace esp32_firmware