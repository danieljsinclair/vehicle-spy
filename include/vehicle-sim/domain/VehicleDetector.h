#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <mutex>

namespace vehicle_sim::domain {

enum class VehicleMake {
    Unknown,
    Tesla,
    Audi,
    Volkswagen,
    BMW,
    MercedesBenz,
    Generic
};

enum class DetectionConfidence {
    None = 0,
    Low = 1,
    Medium = 2,
    High = 3
};

struct VehicleDetectionResult {
    VehicleMake make = VehicleMake::Unknown;
    std::string vin;
    std::string wmi;
    std::string suggestedVehicleId;
    bool isElectric = false;

    // CAN ID fingerprinting
    std::unordered_set<uint16_t> observedCanIds;
    int frameCount = 0;
    DetectionConfidence confidence = DetectionConfidence::None;
    std::string evidenceSummary;

    bool hasSuggestion() const noexcept {
        return !suggestedVehicleId.empty();
    }
};

// Data-driven vehicle fingerprint: maps a CAN ID to its vehicle association.
// Adding a new vehicle requires only adding entries to the registry map,
// not modifying conditional logic in the detector (OCP).
struct VehicleFingerprint {
    VehicleMake make;
    std::string configId;
    std::string evidenceTag; // e.g. "Tesla signals", "Audi MLB signals"
};

// Collects per-vehicle evidence counts from the CAN ID registry
struct VehicleEvidence {
    int totalFrameCount = 0;
    int uniqueIdCount = 0;
};

// Result of vehicle suggestion determination (data-driven)
struct VehicleSuggestion {
    std::string configId;
    VehicleMake make = VehicleMake::Unknown;
    std::string evidenceTag;
    int totalFrameCount = 0;
    int uniqueIdCount = 0;
    bool conflicting = false;
};

class VehicleDetector {
public:
    explicit VehicleDetector(int accumulationWindowMs = 3000);

    // Passive CAN ID observation (main detection mechanism)
    void observeFrame(const std::vector<std::uint8_t>& frame);

    // Active OBD2 query-based detection
    [[nodiscard]] static std::vector<std::uint8_t> buildVINQuery();
    [[nodiscard]] static std::vector<std::uint8_t> buildFuelTypeQuery();
    bool feedVINResponse(const std::vector<std::uint8_t>& response);
    bool feedFuelTypeResponse(const std::vector<std::uint8_t>& response);

    [[nodiscard]] VehicleDetectionResult getResult() const;
    void reset();

    // WMI decoding (static utility)
    [[nodiscard]] static VehicleMake decodeWMI(const std::string& wmi);
    [[nodiscard]] static std::string makeToConfigId(VehicleMake make, bool isElectric);
    [[nodiscard]] static std::string extractVINFromResponse(const std::vector<std::uint8_t>& response);

    // Raw frame history for debugging/visibility
    struct RawFrame {
        std::chrono::steady_clock::time_point timestamp;
        std::vector<std::uint8_t> data;
    };
    [[nodiscard]] std::vector<RawFrame> getRecentFrames(int maxCount = 20) const;
    [[nodiscard]] std::chrono::steady_clock::time_point lastFrameTime() const;
    [[nodiscard]] bool isReceivingData() const;

private:
    void feedCANFrame(uint16_t canId);
    void feedOBD2Frame(const std::vector<std::uint8_t>& data);

    [[nodiscard]] std::unordered_map<VehicleMake, VehicleEvidence> gatherEvidence(
        const std::unordered_set<uint16_t>& seenIds) const;

    [[nodiscard]] VehicleSuggestion determineSuggestion(
        const std::unordered_map<VehicleMake, VehicleEvidence>& evidence) const;

    [[nodiscard]] static DetectionConfidence calculateConfidence(
        VehicleMake make, int uniqueIdCount, int totalFrameCount);

    [[nodiscard]] std::string buildEvidenceSummary(
        int totalFrames,
        const std::unordered_set<uint16_t>& seenIds,
        const std::string& vin,
        const VehicleSuggestion& suggestion) const;

    void completeDetection();
    void addFrame(const std::vector<std::uint8_t>& data);

    // CAN ID fingerprinting registry (data-driven, OCP-compliant)
    static const std::unordered_map<uint16_t, VehicleFingerprint> canIdRegistry_;

    // CAN ID fingerprinting state
    std::unordered_map<uint16_t, int> canIdCounts_;
    int obd2ResponseCount_ = 0;

    // VIN/fuel type state
    std::string vin_;
    std::optional<bool> isElectric_;

    // Timing
    std::chrono::steady_clock::time_point startTime_{std::chrono::steady_clock::now()};

    // Raw frame ring buffer
    static constexpr int MAX_FRAME_HISTORY = 50;
    std::vector<RawFrame> frameHistory_;

    // Protects all mutable state above.
    // observeFrame/feedVINResponse/feedFuelTypeResponse write on the BLE callback thread;
    // getResult/getRecentFrames/isReceivingData/reset read on the main thread.
    // Without this, concurrent unordered_map iteration + insertion is UB (iterator invalidation,
    // mid-rehash corruption), vector push_back + read can crash on reallocation, and string
    // append + read can produce torn reads.
    mutable std::mutex stateMutex_;
};

} // namespace vehicle_sim::domain
