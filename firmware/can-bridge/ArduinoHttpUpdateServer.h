#pragma once

// ArduinoHttpUpdateServer.h - Arduino HTTPUpdateServer impl for IHttpUpdateServer.
// Bridges the ESP32 HTTPUpdateServer library to the vanilla IHttpUpdateServer
// interface.
//
// The vanilla passes the IHttpServer* (abstract) to setup(); the Arduino
// HTTPUpdateServer needs the concrete WebServer*. Both adapters are constructed
// in ota_update.ino against the SAME WebServer instance, so this adapter holds
// that WebServer& at construction and uses it directly — the abstract pointer
// arg is intentionally ignored (it refers to the same object by construction).
//
// Production implementation used in the .ino. Host tests use MockHttpUpdateServer.
// Only available when building for Arduino (ARDUINO defined).

#ifdef ARDUINO

#include "OtaUpdateServer.h"
#include <HTTPUpdateServer.h>
#include <WebServer.h>

namespace esp32_firmware {

class ArduinoHttpUpdateServer : public IHttpUpdateServer {
public:
    explicit ArduinoHttpUpdateServer(WebServer& server) : server_(server) {}

    // The IHttpServer* arg is the same WebServer this adapter already holds a
    // reference to (wired in ota_update.ino); pass the concrete instance.
    void setup(IHttpServer* /*server*/, const char* path,
               const char* username, const char* password) override {
        updater_.setup(&server_, path, username, password);
    }

private:
    WebServer& server_;
    HTTPUpdateServer updater_;
};

} // namespace esp32_firmware

#endif // ARDUINO
