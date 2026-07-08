#pragma once

// ESPMock.h - Mock ESP functions for host testing
// Implements IEspAt and IEsp interfaces

#include <functional>

namespace esp32_firmware {

class ESPMock {
public:
    ESPMock() = default;

    void restart() {
        restartCalled_ = true;
        if (restartCallback_) {
            restartCallback_();
        }
    }

    // Test helpers
    void setRestartCallback(std::function<void()> cb) {
        restartCallback_ = std::move(cb);
    }

    bool wasRestartCalled() const { return restartCalled_; }
    void clearRestartFlag() { restartCalled_ = false; }

    void reset() {
        restartCalled_ = false;
        restartCallback_ = nullptr;
    }

private:
    bool restartCalled_ = false;
    std::function<void()> restartCallback_;
};

// Global instance for easy access
inline ESPMock& espMock() {
    static ESPMock instance;
    return instance;
}

} // namespace esp32_firmware