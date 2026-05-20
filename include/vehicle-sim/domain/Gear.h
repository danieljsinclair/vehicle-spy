#pragma once
#include <cstdint>

namespace vehicle_sim::domain {

/**
 * Canonical gear identifiers owned by the API layer.
 *
 * Convention:
 *   Negative = special:  PARK=-2, REVERSE=-1
 *   Zero     = NEUTRAL
 *   Positive = forward gears: GEAR_1=1, GEAR_2=2, GEAR_3=3...
 *   High bit (0x1000) = AUTO mode qualifier: AUTO_1=0x1001, AUTO_2=0x1002...
 *
 * The API emits these constants. The display layer maps to labels via label().
 * If the CAN standard varies across vehicles, translation happens at the DBC
 * boundary — the API always emits the same canonical numbers.
 *
 * Tesla example: CAN DI_gear=4 (Drive, single-speed) → AUTO_1 (0x1001)
 * Manual example: CAN gear=1 (First) → GEAR_1 (1)
 * Auto example:   CAN gear=1 in auto mode → AUTO_1 (0x1001)
 */
struct Gear {
    static constexpr int32_t PARK    = -2;
    static constexpr int32_t REVERSE = -1;
    static constexpr int32_t NEUTRAL =  0;
    static constexpr int32_t GEAR_1  =  1;
    static constexpr int32_t GEAR_2  =  2;
    static constexpr int32_t GEAR_3  =  3;
    static constexpr int32_t GEAR_4  =  4;
    static constexpr int32_t GEAR_5  =  5;
    static constexpr int32_t GEAR_6  =  6;

    // AUTO mode qualifier (high bit)
    static constexpr int32_t AUTO_BIT = 0x1000;
    static constexpr int32_t AUTO_1 = AUTO_BIT | 1;  // 4097
    static constexpr int32_t AUTO_2 = AUTO_BIT | 2;  // 4098
    static constexpr int32_t AUTO_3 = AUTO_BIT | 3;  // 4099
    static constexpr int32_t AUTO_4 = AUTO_BIT | 4;
    static constexpr int32_t AUTO_5 = AUTO_BIT | 5;
    static constexpr int32_t AUTO_6 = AUTO_BIT | 6;

    /// Display label for a gear constant. Returns nullptr for unknown values.
    static const char* label(int32_t gear) noexcept {
        switch (gear) {
            case PARK:    return "P";
            case REVERSE: return "R";
            case NEUTRAL: return "N";
            case GEAR_1:  return "1";
            case GEAR_2:  return "2";
            case GEAR_3:  return "3";
            case GEAR_4:  return "4";
            case GEAR_5:  return "5";
            case GEAR_6:  return "6";
            case AUTO_1:  return "D";
            case AUTO_2:  return "D2";
            case AUTO_3:  return "D3";
            case AUTO_4:  return "D4";
            case AUTO_5:  return "D5";
            case AUTO_6:  return "D6";
            default:      return nullptr;
        }
    }

    /// True if the gear constant represents an automatic mode (high bit set).
    static constexpr bool isAuto(int32_t gear) noexcept {
        return (gear & AUTO_BIT) != 0;
    }

    /// Extract the gear number from an auto gear constant.
    static constexpr int32_t gearNumber(int32_t gear) noexcept {
        return gear & ~AUTO_BIT;
    }
};

} // namespace vehicle_sim::domain