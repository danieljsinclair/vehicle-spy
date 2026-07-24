// DeviceTag.cpp - Format device ID as tagged serial prefix
// Extracted from can-bridge.ino for host testability

#include "DeviceTag.h"

#include <cstdio>

namespace esp32_firmware {

std::string formatDeviceTag(const std::array<uint8_t, 16>& deviceId) {
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "[%02X%02X%02X%02X] ",
                  deviceId[0], deviceId[1], deviceId[2], deviceId[3]);
    return std::string(buf);
}

} // namespace esp32_firmware
