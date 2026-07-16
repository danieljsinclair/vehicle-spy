#pragma once

// ArduinoUpdate.h - Arduino Update library implementation for IUpdate.
// Bridges the ESP32 UpdateClass API to the vanilla IUpdate interface used by
// OtaUpdateServer::handleUpload to stream firmware chunks into flash.
//
// Gap 3 (begin size): the vanilla passes OtaConfig::FLASH_MAX_SIZE (a 1MB
// placeholder so the vanilla carries no ESP headers) — the REAL sketch space is
// computed here exactly as the inline ota_update.ino did:
//   ((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)
// so the live device uses its actual free partition space, not the placeholder.
//
// Production implementation used in the .ino. Host tests use MockUpdate.
// Only available when building for Arduino (ARDUINO defined).

#ifdef ARDUINO

#include <stddef.h>
#include <stdint.h>
#include <string.h>  // memcpy
#include "OtaUpdateServer.h"
#include <Update.h>

namespace esp32_firmware {

class ArduinoUpdate : public IUpdate {
public:
    ArduinoUpdate() = default;

    // The size arg is the vanilla's placeholder; the real sketch space is
    // computed from ESP.getFreeSketchSpace() (gap 3 — matches inline L252).
    bool begin(size_t /*size*/, int command) override {
        const uint32_t maxSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        const bool ok = Update.begin(maxSpace, command);
        return ok;
    }

    size_t write(const uint8_t* data, size_t len) override {
        // The vanilla's IUpdate::write carries a const-correct contract (the
        // bytes are read, not modified). UpdateClass::write on ESP32 core 2.0.17
        // takes a non-const uint8_t* — the API is const-incorrect. Rather than
        // cast away const (cpp:S859), copy into a fixed stack buffer the Arduino
        // API accepts. Upload chunks are bounded by HTTP_UPLOAD_BUFLEN (1436B),
        // so a fixed buffer suffices — no VLA, no heap, and the memcpy is
        // negligible vs the flash-write latency that follows.
        constexpr size_t CHUNK_CAP = 1436;  // HTTP_UPLOAD_BUFLEN (WebServer.h)
        uint8_t buf[CHUNK_CAP];
        if (len > CHUNK_CAP) {
            len = CHUNK_CAP;  // defensive: never overrun the stack buffer
        }
        memcpy(buf, data, len);
        const size_t written = Update.write(buf, len);
        return written;
    }

    bool end(bool evenIfError) override {
        const bool ok = Update.end(evenIfError);
        return ok;
    }

    bool hasError() const override {
        return Update.hasError();
    }

    void abort() override {
        Update.abort();
    }
};

} // namespace esp32_firmware

#endif // ARDUINO
