#pragma once

// ArduinoCrypto.h - Arduino libsodium implementation for the ICrypto interface.
// Bridges the ESP32 libsodium crypto_sign_ed25519ph_* API to the vanilla ICrypto
// interface used by OtaUpdateServer::verifyPartition.
//
// This is the production implementation used in the .ino. Host tests use
// MockCrypto from tests/OtaUpdateServer_test.cpp instead.
//
// IMPORTANT: This file is only available when building for Arduino (ARDUINO
// defined). Host tests use mocks.

#ifdef ARDUINO

#include <stdint.h>
#include <string.h>
extern "C" {
#include <sodium.h>
}
#include "OtaUpdateServer.h"
#include "OtaPublicKey.h"  // Gap 6: the real device signing key (adapter-owned, not vanilla).

namespace esp32_firmware {

// ArduinoCrypto - production ICrypto implementation backed by libsodium.
//
// Gap 4a (crypto state): the vanilla ICrypto passes an opaque void* state
// through init/update/finalVerify so the vanilla carries no libsodium types.
// The real Ed25519ph state is owned HERE (a crypto_sign_ed25519ph_state member);
// the passed void* is intentionally ignored — there is exactly one verification
// in flight at a time, so a single member is sufficient (mirrors the inline
// ota_update.ino which used a function-local state across one verify cycle).
//
// Gap 4c (WDT feed): esp_partition_read + hashing over a 1MB+ image takes long
// enough to trip the watchdog; the inline fed it every VERIFY_YIELD_INTERVAL
// (16KB) via delay(1). We do the same in signEd25519phUpdate.
class ArduinoCrypto : public ICrypto {
public:
    ArduinoCrypto() = default;

    int sodiumInit() override {
        return sodium_init();
    }

    // The vanilla threads no state (gap 4a) — the Ed25519ph state is the member
    // below, reset per verify cycle.
    int signEd25519phInit() override {
        verifyBytes_ = 0;  // reset per verify cycle (gap 4c cadence tracking)
        return crypto_sign_ed25519ph_init(&state_);
    }

    int signEd25519phUpdate(const uint8_t* data, size_t len) override {
        const int rc = crypto_sign_ed25519ph_update(&state_, data, len);

        // Gap 4c (WDT feed): hashing a 1MB+ image takes long enough to trip the
        // task watchdog and boot-loop. Feed it on the same cadence as the inline
        // ota_update.ino L124-127 — every VERIFY_YIELD_INTERVAL (0x3FFF = 16KB)
        // of accumulated data — via delay(1). The running byte count is reset on
        // each signEd25519phInit so one verify cycle self-contained.
        if (rc == 0) {
            verifyBytes_ += len;
            if ((verifyBytes_ & OtaConfig::VERIFY_YIELD_INTERVAL) == 0) {
                delay(1);
            }
        }
        return rc;
    }

    // Gap 6 (signing key): the vanilla carries NO key material, so it passes a
    // sentinel through pubKey. The real key is crypto-domain and lives HERE
    // (from OtaPublicKey.h, auto-generated per device owner). The passed pubKey
    // is intentionally ignored.
    int signEd25519phFinalVerify(const uint8_t* sig, const uint8_t* /*pubKey*/) override {
        return crypto_sign_ed25519ph_final_verify(&state_, sig, OTA_PUBLIC_KEY);
    }

private:
    crypto_sign_ed25519ph_state state_{};
    size_t verifyBytes_ = 0;  // accumulated bytes since init (gap 4c WDT cadence)
};

} // namespace esp32_firmware

#endif // ARDUINO
