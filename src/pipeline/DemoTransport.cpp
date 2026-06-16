#include "vehicle-sim/pipeline/DemoTransport.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <sstream>

namespace vehicle_sim::pipeline {

namespace {

// Canonical Tesla CAN IDs (opendbc tesla_model3_party.dbc), in HEX (the form
// a raw CAN adapter streams — the normaliser parses the ID as hex).
constexpr std::uint32_t CAN_ID_DI_TORQUE = 0x108;        // 264 decimal — DI_torque
constexpr std::uint32_t CAN_ID_DI_SYSTEM_STATUS = 0x118; // 280 decimal — DI_systemStatus
constexpr std::uint32_t CAN_ID_DI_SPEED = 0x257;         // 599 decimal — DI_speed

// Gear codes (DI_gear VAL table): 1=P, 2=R, 3=N, 4=D.
constexpr std::uint32_t GEAR_DRIVE = 4;

// Bit layout helpers (all signals are little-endian @1 in the DBC).
// We pack raw values into the 8-byte payload at the documented bit offsets.

// Write `bits` bits of `value` into `data` starting at bit `startBit`
// (little-endian / Intel byte order: bit 0 = LSB of byte 0).
void packBits(std::array<std::uint8_t, 8>& data,
              std::uint32_t startBit,
              std::uint32_t bits,
              std::uint64_t value) noexcept {
    for (std::uint32_t i = 0; i < bits; ++i) {
        const std::uint32_t bitIndex = startBit + i;
        const std::size_t byteIdx = bitIndex / 8;
        const std::uint32_t bitInByte = bitIndex % 8;
        if (value & (std::uint64_t{1} << i)) {
            data[byteIdx] |= static_cast<std::uint8_t>(1u << bitInByte);
        } else {
            data[byteIdx] &= static_cast<std::uint8_t>(~(1u << bitInByte));
        }
        if (byteIdx >= data.size()) return;  // defensive
    }
}

// Encode DI_systemStatus (CAN 280): DI_accelPedalPos (bit 32, 8b, scale 0.4)
// and DI_gear (bit 21, 3b). throttlePct in [0,100].
std::array<std::uint8_t, 8> encodeSystemStatus(double throttlePct,
                                               std::uint32_t gear) noexcept {
    std::array<std::uint8_t, 8> data{};
    // raw = pct / 0.4 ; clamp to [0,255] (255 = SNA).
    double raw = throttlePct / 0.4;
    auto rawPedal = static_cast<std::uint64_t>(
        std::max(0.0, std::min(255.0, std::round(raw))));
    packBits(data, 32, 8, rawPedal);
    packBits(data, 21, 3, gear & 0x7u);
    return data;
}

// Encode DI_torque (CAN 264): DI_torqueActual (bit 27, 13b signed, scale 2).
// torqueNm in Nm.
std::array<std::uint8_t, 8> encodeTorque(double torqueNm) noexcept {
    std::array<std::uint8_t, 8> data{};
    // raw = Nm / 2 ; signed 13-bit two's complement.
    double rawD = torqueNm / 2.0;
    long raw = std::lround(rawD);
    // Clamp into signed 13-bit range [-4096, 4095].
    if (raw > 4095) raw = 4095;
    if (raw < -4096) raw = -4096;
    std::uint64_t raw13 = static_cast<std::uint64_t>(raw) & 0x1FFFu;
    packBits(data, 27, 13, raw13);
    return data;
}

// Encode DI_speed (CAN 599): DI_vehicleSpeed (bit 12, 12b, scale 0.08, off -40).
// speedKmh in kph.
std::array<std::uint8_t, 8> encodeSpeed(double speedKmh) noexcept {
    std::array<std::uint8_t, 8> data{};
    // raw = (kph + 40) / 0.08 ; clamp to [0, 4095] (4095 = SNA).
    double raw = (speedKmh + 40.0) / 0.08;
    auto rawSpeed = static_cast<std::uint64_t>(
        std::max(0.0, std::min(4095.0, std::round(raw))));
    packBits(data, 12, 12, rawSpeed);
    return data;
}

std::string formatFrame(std::uint32_t canId, const std::array<std::uint8_t, 8>& data) {
    // "<ID> <D0> <D1> ... <D7>" — same form a raw CAN adapter streams. The CAN
    // ID is emitted as HEX digits (the normaliser parses it as hex), data bytes
    // as two-digit hex.
    std::ostringstream ss;
    ss << std::hex << canId;
    for (std::uint8_t b : data) {
        char buf[4];
        std::snprintf(buf, sizeof(buf), " %02X", b);
        ss << buf;
    }
    return ss.str();
}

} // namespace

std::string DemoTransport::buildFrameLine(std::size_t i) noexcept {
    // Driving loop: a sine over the frame index ramps speed 0..~120..0 kph.
    // Phase advances ~0.05 rad/frame (same cadence as the legacy DemoSignalSource).
    double phase = static_cast<double>(i) * 0.05;
    double cycle = (std::sin(phase) + 1.0) / 2.0;  // [0,1]

    double speedKmh = cycle * 120.0;
    double throttlePct = cycle * 80.0;
    double torqueNm = (cycle < 0.6) ? (100.0 + cycle * 300.0)
                                   : (-50.0 - (cycle - 0.6) * 200.0);

    // Emit the three drive-signal frames in turn so each frame index advances
    // the loop by one frame's worth of phase. Round-robin across the IDs.
    switch (i % 3) {
        case 0:
            return formatFrame(CAN_ID_DI_SYSTEM_STATUS,
                               encodeSystemStatus(throttlePct, GEAR_DRIVE));
        case 1:
            return formatFrame(CAN_ID_DI_TORQUE, encodeTorque(torqueNm));
        case 2:
        default:
            return formatFrame(CAN_ID_DI_SPEED, encodeSpeed(speedKmh));
    }
}

DemoTransport::DemoTransport(std::size_t frameCount) : total_(frameCount) {}

bool DemoTransport::open() {
    opened_ = true;
    return true;
}

bool DemoTransport::isOpen() const noexcept {
    return opened_ && emitted_ < total_;
}

std::optional<std::string> DemoTransport::nextLine() {
    if (!opened_ || emitted_ >= total_) {
        return std::nullopt;
    }
    return buildFrameLine(emitted_++);
}

} // namespace vehicle_sim::pipeline
