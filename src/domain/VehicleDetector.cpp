#include "vehicle-sim/domain/VehicleDetector.h"
#include <cctype>
#include <sstream>

namespace vehicle_sim::domain {

// --- Data-driven CAN ID Registry ---
// To add a new vehicle: add entries here. No conditional chains to modify (OCP).

const std::unordered_map<uint16_t, VehicleFingerprint> VehicleDetector::canIdRegistry_ = {
    {0x108, {VehicleMake::Tesla,  "tesla", "Tesla signals"}},
    {0x118, {VehicleMake::Tesla,  "tesla", "Tesla signals"}},
    {0x129, {VehicleMake::Tesla,  "tesla", "Tesla signals"}},
    {0x100, {VehicleMake::Audi,   "audi_mlb_evo", "Audi MLB signals"}},
};

VehicleDetector::VehicleDetector(int accumulationWindowMs)
    : startTime_(std::chrono::steady_clock::now()),
      accumulationWindowMs_(accumulationWindowMs) {}

// --- Passive CAN ID Observation ---

void VehicleDetector::observeFrame(const std::vector<std::uint8_t>& frame) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    addFrame(frame);

    if (frame.size() == 10) {
        uint16_t canId = frame[0] | (static_cast<uint16_t>(frame[1]) << 8);
        feedCANFrame(canId);
    } else if (frame.size() >= 2 && frame.size() <= 10) {
        feedOBD2Frame(frame);
    }
}

void VehicleDetector::feedCANFrame(uint16_t canId) {
    canIdCounts_[canId]++;
}

void VehicleDetector::feedOBD2Frame(const std::vector<std::uint8_t>& /*data*/) {
    obd2ResponseCount_++;
}

// --- Evidence Gathering ---

auto VehicleDetector::gatherEvidence(const std::unordered_set<uint16_t>& seenIds) const
    -> std::unordered_map<VehicleMake, VehicleEvidence> {
    std::unordered_map<VehicleMake, VehicleEvidence> evidence;

    for (uint16_t id : seenIds) {
        auto it = canIdRegistry_.find(id);
        if (it == canIdRegistry_.end()) continue;

        VehicleMake make = it->second.make;
        auto countIt = canIdCounts_.find(id);
        int frameCount = (countIt != canIdCounts_.end()) ? countIt->second : 0;

        evidence[make].uniqueIdCount++;
        evidence[make].totalFrameCount += frameCount;
    }

    return evidence;
}

// --- Suggestion Determination (data-driven) ---

auto VehicleDetector::determineSuggestion(
    const std::unordered_map<VehicleMake, VehicleEvidence>& evidence) const -> VehicleSuggestion {

    if (evidence.empty()) {
        if (obd2ResponseCount_ > 0) {
            return {"generic", VehicleMake::Generic, "OBD2 responses",
                    obd2ResponseCount_, 0, false};
        }
        return {};
    }

    if (evidence.size() > 1) {
        return {"", VehicleMake::Unknown, "", 0, 0, true};
    }

    auto it = evidence.begin();
    VehicleMake make = it->first;
    const auto& ev = it->second;

    std::string configId;
    std::string evidenceTag;
    for (const auto& [id, fp] : canIdRegistry_) {
        if (fp.make == make) {
            configId = fp.configId;
            evidenceTag = fp.evidenceTag;
            break;
        }
    }

    return {configId, make, evidenceTag, ev.totalFrameCount, ev.uniqueIdCount, false};
}

// --- Confidence Calculation ---

DetectionConfidence VehicleDetector::calculateConfidence(
    VehicleMake make, int uniqueIdCount, int totalFrameCount) {
    switch (make) {
        case VehicleMake::Tesla:
            if (uniqueIdCount >= 3) return DetectionConfidence::High;
            if (uniqueIdCount >= 2) return DetectionConfidence::Medium;
            return DetectionConfidence::Low;

        case VehicleMake::Audi:
            if (totalFrameCount >= 5) return DetectionConfidence::High;
            if (totalFrameCount >= 2) return DetectionConfidence::Medium;
            return DetectionConfidence::Low;

        case VehicleMake::Generic:
            if (totalFrameCount >= 3) return DetectionConfidence::Medium;
            return DetectionConfidence::Low;

        default:
            return DetectionConfidence::None;
    }
}

// --- Evidence Summary Formatting ---

std::string VehicleDetector::buildEvidenceSummary(
    int totalFrames,
    const std::unordered_set<uint16_t>& seenIds,
    const std::string& vin,
    const VehicleSuggestion& suggestion) const {

    std::ostringstream summary;
    summary << "Frames: " << totalFrames;

    if (!seenIds.empty()) {
        summary << " | CAN IDs:";
        for (uint16_t id : seenIds) {
            summary << " 0x" << std::hex << id << std::dec;
        }
    }

    if (!vin.empty()) {
        summary << " | VIN: " << vin;
    } else if (suggestion.conflicting) {
        summary << " | Conflicting evidence";
    } else if (!suggestion.evidenceTag.empty()) {
        summary << " | " << suggestion.evidenceTag;
    }

    return summary.str();
}

// --- Result Building (single source of truth) ---

VehicleDetectionResult VehicleDetector::getResult() const {
    std::lock_guard<std::mutex> lock(stateMutex_);

    VehicleDetectionResult result;
    result.vin = vin_;
    result.wmi = vin_.length() >= 3 ? vin_.substr(0, 3) : "";
    result.isElectric = isElectric_.value_or(false);

    int totalFrames = 0;
    for (const auto& [id, count] : canIdCounts_) {
        result.observedCanIds.insert(id);
        totalFrames += count;
    }
    totalFrames += obd2ResponseCount_;
    result.frameCount = totalFrames;

    auto evidence = gatherEvidence(result.observedCanIds);
    auto suggestion = determineSuggestion(evidence);

    if (!vin_.empty() && vin_.length() >= 3) {
        VehicleMake make = decodeWMI(vin_.substr(0, 3));
        bool isElectric = isElectric_.value_or(false);
        result.make = make;
        result.suggestedVehicleId = makeToConfigId(make, isElectric);
    } else if (!suggestion.configId.empty()) {
        result.suggestedVehicleId = suggestion.configId;
        result.make = suggestion.make;
        result.confidence = calculateConfidence(
            suggestion.make, suggestion.uniqueIdCount, suggestion.totalFrameCount);
    }

    result.evidenceSummary = buildEvidenceSummary(
        totalFrames, result.observedCanIds, vin_, suggestion);

    return result;
}

// --- Active OBD2 Query Detection ---

std::vector<std::uint8_t> VehicleDetector::buildVINQuery() {
    return {0x09, 0x02};
}

std::vector<std::uint8_t> VehicleDetector::buildFuelTypeQuery() {
    return {0x01, 0x51};
}

bool VehicleDetector::feedVINResponse(const std::vector<std::uint8_t>& response) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    addFrame(response);

    std::string newPart;
    if (response.size() >= 8 && response[0] == 0x49 && response[1] == 0x02) {
        size_t start = 3;
        while (start < response.size() && response[start] == 0x00) start++;
        for (size_t i = start; i < response.size(); ++i) {
            if (response[i] != 0x00) newPart += static_cast<char>(response[i]);
        }
    } else if (response.size() >= 1) {
        for (size_t i = 0; i < response.size(); ++i) {
            if (response[i] != 0x00) newPart += static_cast<char>(response[i]);
        }
    } else {
        return false;
    }

    vin_ += newPart;
    if (vin_.length() >= 17) vin_ = vin_.substr(0, 17);
    completeDetection();
    return true;
}

bool VehicleDetector::feedFuelTypeResponse(const std::vector<std::uint8_t>& response) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    addFrame(response);

    if (response.size() < 3) return false;
    if ((response[0] != 0x41 && response[0] != 0x49) || response[1] != 0x51) return false;

    isElectric_ = (response[2] == 0x08);
    completeDetection();
    return true;
}

// --- Raw Frame History ---

void VehicleDetector::addFrame(const std::vector<std::uint8_t>& data) {
    frameHistory_.push_back({std::chrono::steady_clock::now(), data});
    if (static_cast<int>(frameHistory_.size()) > MAX_FRAME_HISTORY) {
        frameHistory_.erase(frameHistory_.begin());
    }
}

std::vector<VehicleDetector::RawFrame> VehicleDetector::getRecentFrames(int maxCount) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (maxCount >= static_cast<int>(frameHistory_.size())) return frameHistory_;
    return {frameHistory_.end() - maxCount, frameHistory_.end()};
}

std::chrono::steady_clock::time_point VehicleDetector::lastFrameTime() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return frameHistory_.empty() ? std::chrono::steady_clock::time_point{} : frameHistory_.back().timestamp;
}

bool VehicleDetector::isReceivingData() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (frameHistory_.empty()) return false;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - frameHistory_.back().timestamp);
    return elapsed.count() < 1000;
}

// --- WMI Decoding ---

VehicleMake VehicleDetector::decodeWMI(const std::string& wmi) {
    if (wmi.length() < 3) return VehicleMake::Unknown;

    std::string upper;
    upper.reserve(3);
    for (int i = 0; i < 3; ++i) upper += static_cast<char>(std::toupper(wmi[i]));

    if (upper == "5YJ" || upper == "7SA") return VehicleMake::Tesla;
    if (upper == "WAU" || upper == "WUA" || upper == "WA1" || upper == "TRU") return VehicleMake::Audi;
    if (upper == "WVW" || upper == "WV1" || upper == "WV2" || upper == "WV0") return VehicleMake::Volkswagen;
    if (upper == "WBA" || upper == "WBS" || upper == "WBX") return VehicleMake::BMW;
    if (upper == "WDB" || upper == "WDC") return VehicleMake::MercedesBenz;

    bool validFormat = std::isalnum(upper[0]) && std::isalnum(upper[1]) && std::isalnum(upper[2]);
    bool knownRegion = (upper[0] >= '1' && upper[0] <= '5') ||
                       (upper[0] >= 'A' && upper[0] <= 'H') ||
                       (upper[0] >= 'J' && upper[0] <= 'R') ||
                       (upper[0] >= 'S' && upper[0] <= 'Z');

    if (validFormat && knownRegion) return VehicleMake::Generic;
    return VehicleMake::Unknown;
}

std::string VehicleDetector::makeToConfigId(VehicleMake make, bool isElectric) {
    switch (make) {
        case VehicleMake::Tesla: return "tesla";
        case VehicleMake::Audi: return isElectric ? "audi_mlb_evo" : "generic";
        case VehicleMake::Volkswagen: return isElectric ? "audi_mlb_evo" : "generic";
        case VehicleMake::BMW:
        case VehicleMake::MercedesBenz:
            return "generic";
        case VehicleMake::Unknown:
        case VehicleMake::Generic:
        default:
            return "generic";
    }
}

std::string VehicleDetector::extractVINFromResponse(const std::vector<std::uint8_t>& response) {
    std::string vin;
    if (response.size() < 8 || response[0] != 0x49 || response[1] != 0x02) return vin;
    for (size_t i = 3; i < response.size(); ++i) {
        if (response[i] != 0x00) vin += static_cast<char>(response[i]);
    }
    return vin;
}

void VehicleDetector::reset() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    canIdCounts_.clear();
    obd2ResponseCount_ = 0;
    vin_.clear();
    isElectric_.reset();
    frameHistory_.clear();
    startTime_ = std::chrono::steady_clock::now();
}

void VehicleDetector::completeDetection() {
    // VIN + fuel type detection is handled via getResult() combining all evidence
}

} // namespace vehicle_sim::domain
