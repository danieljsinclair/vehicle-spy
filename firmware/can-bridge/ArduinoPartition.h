#pragma once

// ArduinoPartition.h - ESP-IDF partition implementation for IPartition.
// Bridges esp_ota_ops / esp_partition APIs to the vanilla IPartition interface
// used by OtaUpdateServer for signature verification + boot-partition selection.
//
// OtaPartitionRef is the type-safe opaque handle declared (not defined) in the
// vanilla header: it wraps a `const esp_partition_t*` and is defined ONLY here
// so the vanilla never pulls ESP-IDF headers. This replaces an earlier raw
// `const void*` (cpp:S5008) with a named handle the compiler can type-check.
//
// Production implementation used in the .ino. Host tests use MockPartition.
// Only available when building for Arduino (ARDUINO defined).

#ifdef ARDUINO

#include <stdint.h>
#include "OtaUpdateServer.h"
#include <esp_ota_ops.h>
#include <esp_partition.h>

namespace esp32_firmware {

// Opaque-to-vanilla handle around an ESP-IDF partition pointer. Defined only in
// this adapter TU; the vanilla forward-declares it and threads it untouched.
struct OtaPartitionRef {
    const esp_partition_t* part = nullptr;
};

class ArduinoPartition : public IPartition {
public:
    ArduinoPartition() = default;

    const OtaPartitionRef* getRunningPartition() override {
        running_ = OtaPartitionRef{esp_ota_get_running_partition()};
        return &running_;
    }

    const OtaPartitionRef* getNextUpdatePartition(const OtaPartitionRef* running) override {
        const esp_partition_t* r = running ? running->part : nullptr;
        next_ = OtaPartitionRef{esp_ota_get_next_update_partition(r)};
        return &next_;
    }

    // Gap 4b: capacity of the OTA partition in bytes. The vanilla's
    // verifyPartition rejects an image whose declared size exceeds this (mirrors
    // the inline `size > part->size` guard). Real ESP32 OTA partitions are
    // typically 1.2-1.6MB; reporting the true capacity lets legitimate firmware
    // through that the previous hardcoded 1MB cap rejected.
    uint32_t size(const OtaPartitionRef* partition) override {
        if (!partition || !partition->part) {
            return 0;
        }
        return partition->part->size;
    }

    int read(const OtaPartitionRef* partition, uint32_t offset, uint8_t* data, size_t sz) override {
        return esp_partition_read(partition->part, offset, data, sz);
    }

    int getStatePartition(const OtaPartitionRef* partition, int* state) override {
        esp_ota_img_states_t img = ESP_OTA_IMG_UNDEFINED;
        const int rc = esp_ota_get_state_partition(partition->part, &img);
        *state = static_cast<int>(img);
        return rc;
    }

    int setBootPartition(const OtaPartitionRef* partition) override {
        return esp_ota_set_boot_partition(partition->part);
    }

    int markAppValidCancelRollback() override {
        return esp_ota_mark_app_valid_cancel_rollback();
    }

private:
    // Stable storage for the handles returned to the vanilla. The vanilla holds
    // only the pointers; the backing objects live here for the call duration
    // (matching the inline's use of esp_ota_get_*_partition return values).
    OtaPartitionRef running_;
    OtaPartitionRef next_;
};

} // namespace esp32_firmware

#endif // ARDUINO
