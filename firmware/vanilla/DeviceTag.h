#pragma once

// DeviceTag.h - Format a device ID as a tagged serial prefix
// Extracted from can-bridge.ino for host testability
//
// This is pure formatting logic: takes a 16-byte device ID and produces a
// [XXXX] tag string (first 4 bytes as uppercase hex). The .ino calls this
// and wraps the result in Arduino String for Serial.printf use.

#include <cstdint>
#include <array>
#include <string>

namespace esp32_firmware {

// Format a 16-byte device ID as a [XXXX] tag string using the first 4 bytes.
// The result is used to prefix serial diagnostic messages with device identity.
//
// This is a pure function (no side effects, no hardware) — fully host-testable.
std::string formatDeviceTag(const std::array<uint8_t, 16>& deviceId);

} // namespace esp32_firmware
