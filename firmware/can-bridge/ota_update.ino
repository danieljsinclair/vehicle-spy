// ESP32 WiFi OTA update — Ed25519ph-signed firmware over HTTP.
//
// THIN VENEER: this file only constructs the Arduino adapters + the vanilla
// OtaUpdateServer and delegates otaSetup/otaLoop/otaMarkValidOnBoot to it. All
// OTA logic — the START/WRITE/END/ABORTED upload state machine, the signature
// verification (verifyPartition), the POST/GET HTTP handlers, the timeout guard
// — lives in firmware/vanilla/OtaUpdateServer.{h,cpp} and is host-tested via
// firmware/tests/OtaUpdateServer_test.cpp (219/219). The inline implementation
// was deleted in extraction Stage 5 (#41).
//
// Security boundary: signature verification, partition-size guard, and the
// signing key are all crypto-domain. The key is owned by ArduinoCrypto (from
// OtaPublicKey.h); the vanilla carries no key material.
//
// Lifecycle note: can-bridge.ino calls otaMarkValidOnBoot() BEFORE otaSetup(),
// so the adapters + server are constructed once lazily (ensureOtaServer) and
// shared by both entry points.

#include <WiFi.h>
#include <memory>
#include "OtaUpdateServer.h"
#include "ArduinoHttpServer.h"
#include "ArduinoHttpUpdateServer.h"
#include "ArduinoUpdate.h"
#include "ArduinoPartition.h"
#include "ArduinoCrypto.h"

#if VEHICLE_SIM_ENABLE_OTA_SERVER

using esp32_firmware::OtaUpdateServer;
using esp32_firmware::ArduinoHttpServer;
using esp32_firmware::ArduinoHttpUpdateServer;
using esp32_firmware::ArduinoUpdate;
using esp32_firmware::ArduinoPartition;
using esp32_firmware::ArduinoCrypto;
using esp32_firmware::OtaConfig;

// arduinoTime (static ArduinoTime, declared in can-bridge.ino) is in scope here:
// arduino-cli concatenates every .ino in the sketch into one translation unit
// (can-bridge.ino sorts before ota_update.ino), so its earlier declaration is
// visible without an extern. Same reason RED/GREEN/NC are visible below.

namespace {
// OTA server + its 5 Arduino adapters. Constructed once (lazily) and shared by
// otaMarkValidOnBoot() + otaSetup(). The WebServer is owned by ArduinoHttpServer;
// ArduinoHttpUpdateServer borrows it via raw().
std::unique_ptr<ArduinoHttpServer>        otaHttp;
std::unique_ptr<ArduinoHttpUpdateServer>  otaUpdater;
std::unique_ptr<ArduinoUpdate>            otaUpdateLib;
std::unique_ptr<ArduinoPartition>         otaPartition;
std::unique_ptr<ArduinoCrypto>            otaCrypto;
std::unique_ptr<OtaUpdateServer>          otaServer;

// Build the adapters + vanilla server once. Idempotent — safe to call from both
// otaMarkValidOnBoot() and otaSetup().
void ensureOtaServer() {
    if (otaServer) {
        return;
    }
    otaHttp       = std::make_unique<ArduinoHttpServer>(OtaConfig::HTTP_PORT);
    otaUpdater    = std::make_unique<ArduinoHttpUpdateServer>(otaHttp->raw());
    otaUpdateLib  = std::make_unique<ArduinoUpdate>();
    otaPartition  = std::make_unique<ArduinoPartition>();
    otaCrypto     = std::make_unique<ArduinoCrypto>();
    otaServer = std::make_unique<OtaUpdateServer>(
        *otaHttp, *otaUpdater, *otaUpdateLib, *otaPartition, *otaCrypto, arduinoTime);
}
} // namespace

void otaMarkValidOnBoot() {
    ensureOtaServer();
    otaServer->markValidOnBoot();
}

void otaSetup() {
    ensureOtaServer();

    // Gap 2 (reboot): on a fully-verified accept, the vanilla's handlePost has
    // already flushed the client + sent HTTP OK + fired this callback. We then
    // delay + restart so the client receives its response before the device
    // resets into the new image.
    otaServer->setSuccessCallback([]() {
        delay(OtaConfig::REBOOT_DELAY_MS);
        ESP.restart();
    });

    otaServer->setup();
    Serial.printf("OTA HTTP server on port %u\r\n",
                  static_cast<unsigned>(OtaConfig::HTTP_PORT));
}

void otaLoop() {
    if (otaServer) {
        otaServer->loop();
    }
}

#else  // VEHICLE_SIM_ENABLE_OTA_SERVER == 0

// OTA disabled (VEHICLE_SIM_ENABLE_OTA_SERVER=0) — no-op stubs so can-bridge.ino
// links unchanged. Each body is intentionally empty: the feature is compiled
// out, so there is nothing to set up, service, or mark valid.
void otaSetup() {
    // No-op: OTA server compiled out.
}
void otaLoop() {
    // No-op: OTA server compiled out.
}
void otaMarkValidOnBoot() {
    // No-op: OTA server compiled out.
}

#endif
