#pragma once

#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include "vehicle-sim/domain/VehicleSignal.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/DBCParser.h"

namespace vehicle_sim::domain {

enum class VehicleProtocol {
    CAN,
    OBD2,
    Simulation
};

class DBCTranslationService {
public:
    /**
     * Self-diagnosing detail of the most recent failed loadVehicle* call.
     * Reset to Stage::None at the start of every load attempt, so after any
     * successful load it reports "no failure".
     */
    struct DBCLoadFailure {
        enum class Stage {
            None,           ///< no failure recorded (or load succeeded)
            UnknownVehicle, ///< vehicle id not present in the registry
            Open,           ///< resolved DBC path exists nowhere / could not be opened
            ZeroSignals     ///< DBC opened and parsed but produced 0 signals
        };

        Stage stage{Stage::None};
        std::string vehicleId;                    ///< vehicle whose load failed
        std::string resourcePath;                 ///< DBC path as declared/passed (often PWD-relative)
        std::string resolvedPath;                 ///< concrete path handed to the parser
        std::vector<std::string> candidatesTried; ///< every candidate path resolveResource checked

        /// Short lowercase label for error messages ("open", "zero-signals", ...).
        [[nodiscard]] const char* stageLabel() const noexcept;
    };

    DBCTranslationService();
    ~DBCTranslationService();

    bool loadVehicle(const std::string& vehicleId, VehicleProtocol protocol);
    bool loadVehicleWithContent(const std::string& vehicleId, VehicleProtocol protocol, const std::string& dbcContent);
    bool loadVehicleFromPath(const std::string& vehicleId, VehicleProtocol protocol, const std::string& dbcAbsolutePath);
    /**
     * Decode a single raw CAN/OBD2 frame into a VehicleSignal.
     *
     * @param rawData       Raw frame bytes (DBC: [canId lo, canId hi, d0..d7])
     * @param timestampUtcMs Optional original capture timestamp (epoch ms). When
     *                       supplied (replay path) it is stamped onto the emitted
     *                       signal; when nullopt (live/BLE path) the translator
     *                       falls back to wall-clock now().
     */
    [[nodiscard]] std::optional<VehicleSignal> processFrame(
        const std::vector<std::uint8_t>& rawData,
        std::optional<std::uint64_t> timestampUtcMs = std::nullopt
    ) const noexcept;
    [[nodiscard]] VehicleProtocol getProtocol() const noexcept;
    [[nodiscard]] std::string getVehicleId() const noexcept;
    [[nodiscard]] bool isLoaded() const noexcept;

    /**
     * Detail of the most recent failed loadVehicle* call (stage, vehicle id,
     * paths tried). Lets callers (e.g. VehicleConfigResolver building a
     * DBCLoadException) report WHY a load failed instead of a bare "false".
     */
    [[nodiscard]] const DBCLoadFailure& lastLoadFailure() const noexcept;

    VehicleConfigRegistry& registry();
    const VehicleConfigRegistry& registry() const;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace vehicle_sim::domain
