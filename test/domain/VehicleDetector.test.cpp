#include <gtest/gtest.h>
#include "vehicle-sim/domain/VehicleDetector.h"
#include <thread>
#include <chrono>

using namespace vehicle_sim::domain;

// --- Helpers ---

static std::vector<uint8_t> canFrame(uint16_t canId, const std::vector<uint8_t>& data = {0,0,0,0,0,0,0,0}) {
    std::vector<uint8_t> frame(10);
    frame[0] = canId & 0xFF;
    frame[1] = (canId >> 8) & 0xFF;
    std::copy(data.begin(), data.begin() + std::min<size_t>(data.size(), 8), frame.begin() + 2);
    return frame;
}

static std::vector<uint8_t> obd2Frame(uint8_t mode, uint8_t pid, const std::vector<uint8_t>& data = {}) {
    std::vector<uint8_t> frame = {static_cast<uint8_t>(mode + 0x40), pid};
    frame.insert(frame.end(), data.begin(), data.end());
    return frame;
}

// ================================================
// WMI Decoding Tests
// ================================================

TEST(VehicleDetector, DecodeWMI_Tesla5YJ) {
    EXPECT_EQ(VehicleDetector::decodeWMI("5YJ"), VehicleMake::Tesla);
}

TEST(VehicleDetector, DecodeWMI_Tesla7SA) {
    EXPECT_EQ(VehicleDetector::decodeWMI("7SA"), VehicleMake::Tesla);
}

TEST(VehicleDetector, DecodeWMI_AudiWAU) {
    EXPECT_EQ(VehicleDetector::decodeWMI("WAU"), VehicleMake::Audi);
}

TEST(VehicleDetector, DecodeWMI_AudiWA1) {
    EXPECT_EQ(VehicleDetector::decodeWMI("WA1"), VehicleMake::Audi);
}

TEST(VehicleDetector, DecodeWMI_AudiTRU) {
    EXPECT_EQ(VehicleDetector::decodeWMI("TRU"), VehicleMake::Audi);
}

TEST(VehicleDetector, DecodeWMI_VolkswagenWVW) {
    EXPECT_EQ(VehicleDetector::decodeWMI("WVW"), VehicleMake::Volkswagen);
}

TEST(VehicleDetector, DecodeWMI_BMWWBA) {
    EXPECT_EQ(VehicleDetector::decodeWMI("WBA"), VehicleMake::BMW);
}

TEST(VehicleDetector, DecodeWMI_Unknown) {
    // "999" starts with a digit > 5, so not a valid region prefix
    EXPECT_EQ(VehicleDetector::decodeWMI("999"), VehicleMake::Unknown);
}

TEST(VehicleDetector, DecodeWMI_Generic) {
    EXPECT_EQ(VehicleDetector::decodeWMI("1G1"), VehicleMake::Generic);
}

// ================================================
// Config ID Mapping Tests
// ================================================

TEST(VehicleDetector, MakeToConfigId_TeslaElectric) {
    EXPECT_EQ(VehicleDetector::makeToConfigId(VehicleMake::Tesla, true), "tesla");
}

TEST(VehicleDetector, MakeToConfigId_AudiElectric) {
    EXPECT_EQ(VehicleDetector::makeToConfigId(VehicleMake::Audi, true), "audi_mlb_evo");
}

TEST(VehicleDetector, MakeToConfigId_AudiGasoline) {
    EXPECT_EQ(VehicleDetector::makeToConfigId(VehicleMake::Audi, false), "generic");
}

TEST(VehicleDetector, MakeToConfigId_Generic) {
    EXPECT_EQ(VehicleDetector::makeToConfigId(VehicleMake::Generic, false), "generic");
}

// ================================================
// VIN Extraction Tests
// ================================================

TEST(VehicleDetector, FeedVINResponse_SingleFrame) {
    VehicleDetector detector;
    std::vector<uint8_t> response = {
        0x49, 0x02, 0x01, 0x00, 0x00, 0x00, 0x35, 0x59,
        0x4A, 0x33, 0x53, 0x32, 0x44, 0x58, 0x4D, 0x48,
        0x31, 0x30, 0x35, 0x37, 0x36
    };
    EXPECT_TRUE(detector.feedVINResponse(response));
    auto result = detector.getResult();
    EXPECT_EQ(result.vin, "5YJ3S2DXMH10576");
}

TEST(VehicleDetector, ExtractVINFromResponse_EmptyResponse) {
    EXPECT_TRUE(VehicleDetector::extractVINFromResponse({}).empty());
}

// ================================================
// feedVINResponse — multi-frame / header / rejection contract
// (BLIND coverage locking VIN assembly for the S5566 index-loop refactor)
// ================================================

// OBD2 mode 0x09 PID 0x02 responses carry a 0x49/0x02 mode+PID echo followed
// by a frame index, then the VIN bytes (0x49 = 0x09+0x40 positive-response).
// Leading zero-pad bytes between the index and the real VIN characters must be
// skipped so the assembled VIN contains only the printable VIN content.

TEST(VehicleDetector, FeedVINResponse_MultiFrameWithHeaderAssemblesSeventeenCharVin) {
    // A VIN split across two 0x49/0x02 frames, each with the 3-byte header and
    // leading zero padding before the real characters. Frames must concatenate
    // (skipping headers + pad bytes) into the full 17-char VIN.
    VehicleDetector detector;
    // Frame 1: header 49 02 01, then two pad bytes, then first 9 VIN chars.
    std::vector<uint8_t> frame1 = {
        0x49, 0x02, 0x01, 0x00, 0x00,
        '5', 'Y', 'J', '3', 'S', '2', 'D', 'X', 'M'};
    // Frame 2: header 49 02 02, then one pad byte, then remaining 8 VIN chars.
    std::vector<uint8_t> frame2 = {
        0x49, 0x02, 0x02, 0x00,
        'H', '1', '0', '5', '7', '6', '3', '2'};

    EXPECT_TRUE(detector.feedVINResponse(frame1));
    EXPECT_TRUE(detector.feedVINResponse(frame2));

    auto result = detector.getResult();
    EXPECT_EQ(result.vin, "5YJ3S2DXMH1057632");
    EXPECT_EQ(result.vin.size(), 17u);
}

TEST(VehicleDetector, FeedVINResponse_HeaderFrameWithOnlyPadBytesAddsNothing) {
    // A 0x49/0x02 frame (size >= 8) whose payload after the 3-byte header is all
    // zero-pad bytes contributes nothing to the VIN. The previously-assembled
    // partial VIN must be preserved unchanged. (Frames must be >= 8 bytes to be
    // recognised as the 0x49/0x02 header branch.)
    VehicleDetector detector;
    // Frame 1: header + 4 pad + 'W','B','A','X'  (8 payload bytes after header)
    std::vector<uint8_t> frameWithChars = {
        0x49, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 'W', 'B', 'A', 'X'};
    // Frame 2: header + 5 zero pad bytes only.
    std::vector<uint8_t> frameAllPad = {
        0x49, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00};

    ASSERT_TRUE(detector.feedVINResponse(frameWithChars));
    ASSERT_TRUE(detector.feedVINResponse(frameAllPad));

    EXPECT_EQ(detector.getResult().vin, "WBAX");
}

TEST(VehicleDetector, FeedVINResponse_NonHeaderResponseExtractsNonZeroBytes) {
    // A response that does NOT match the 0x49/0x02 header (but is non-empty)
    // still extracts its non-zero bytes as VIN content.
    VehicleDetector detector;
    // No 0x49/0x02 header; embedded zeros must be skipped.
    std::vector<uint8_t> response = {'A', 0x00, 'B', 0x00, 'C'};
    EXPECT_TRUE(detector.feedVINResponse(response));
    EXPECT_EQ(detector.getResult().vin, "ABC");
}

TEST(VehicleDetector, FeedVINResponse_EmptyResponseRejected) {
    // An empty response carries no VIN information and must be rejected.
    VehicleDetector detector;
    EXPECT_FALSE(detector.feedVINResponse({}));
    EXPECT_TRUE(detector.getResult().vin.empty());
}

TEST(VehicleDetector, FeedVINResponse_TruncatesToSeventeenCharacters) {
    // A VIN response longer than 17 characters must be clamped to exactly 17.
    VehicleDetector detector;
    std::vector<uint8_t> longResponse = {
        0x49, 0x02, 0x01, 0x00,
        '1', 'H', 'G', 'C', 'M', '8', '2', '6', '3', '3',
        'A', '0', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT_TRUE(detector.feedVINResponse(longResponse));
    auto vin = detector.getResult().vin;
    EXPECT_EQ(vin.size(), 17u);
    EXPECT_EQ(vin, "1HGCM82633A001234");
}

// ================================================
// Fuel Type Tests
// ================================================

TEST(VehicleDetector, FeedFuelTypeResponse_Electric) {
    VehicleDetector detector;
    EXPECT_TRUE(detector.feedFuelTypeResponse({0x49, 0x51, 0x08}));
    auto result = detector.getResult();
    EXPECT_TRUE(result.isElectric);
}

TEST(VehicleDetector, FeedFuelTypeResponse_Gasoline) {
    VehicleDetector detector;
    EXPECT_TRUE(detector.feedFuelTypeResponse({0x49, 0x51, 0x01}));
    auto result = detector.getResult();
    EXPECT_FALSE(result.isElectric);
}

// ================================================
// CAN ID Fingerprinting Tests
// ================================================

TEST(VehicleDetector, EmptyDetector_NoSuggestion) {
    VehicleDetector detector;
    auto result = detector.getResult();
    EXPECT_FALSE(result.hasSuggestion());
    EXPECT_EQ(DetectionConfidence::None, result.confidence);
    EXPECT_TRUE(result.observedCanIds.empty());
    EXPECT_EQ(0, result.frameCount);
}

TEST(VehicleDetector, SingleTeslaCANId_Accumulates) {
    VehicleDetector detector;
    detector.observeFrame(canFrame(0x108));
    auto result = detector.getResult();
    EXPECT_EQ(1, result.frameCount);
    EXPECT_TRUE(result.observedCanIds.count(0x108));
}

TEST(VehicleDetector, MultipleCANIds_Accumulate) {
    VehicleDetector detector;
    detector.observeFrame(canFrame(0x108));
    detector.observeFrame(canFrame(0x118));
    detector.observeFrame(canFrame(0x129));
    auto result = detector.getResult();
    EXPECT_EQ(3, result.frameCount);
    EXPECT_EQ(3u, result.observedCanIds.size());
}

TEST(VehicleDetector, DuplicateCANIds_Deduplicate) {
    VehicleDetector detector;
    detector.observeFrame(canFrame(0x108));
    detector.observeFrame(canFrame(0x108));
    auto result = detector.getResult();
    EXPECT_EQ(2, result.frameCount);
    EXPECT_EQ(1u, result.observedCanIds.size());
}

TEST(VehicleDetector, TeslaDetection_HighConfidence_AllThreeIds) {
    VehicleDetector detector;
    detector.observeFrame(canFrame(0x108));
    detector.observeFrame(canFrame(0x118));
    detector.observeFrame(canFrame(0x129));
    auto result = detector.getResult();
    EXPECT_EQ("tesla", result.suggestedVehicleId);
    EXPECT_EQ(VehicleMake::Tesla, result.make);
    EXPECT_EQ(DetectionConfidence::High, result.confidence);
}

TEST(VehicleDetector, TeslaDetection_MediumConfidence_TwoIds) {
    VehicleDetector detector;
    detector.observeFrame(canFrame(0x108));
    detector.observeFrame(canFrame(0x118));
    auto result = detector.getResult();
    EXPECT_EQ("tesla", result.suggestedVehicleId);
    EXPECT_EQ(DetectionConfidence::Medium, result.confidence);
}

TEST(VehicleDetector, TeslaDetection_LowConfidence_OneId) {
    VehicleDetector detector;
    detector.observeFrame(canFrame(0x108));
    auto result = detector.getResult();
    EXPECT_EQ("tesla", result.suggestedVehicleId);
    EXPECT_EQ(DetectionConfidence::Low, result.confidence);
}

TEST(VehicleDetector, AudiDetection_HighConfidence) {
    VehicleDetector detector;
    for (int i = 0; i < 5; ++i) detector.observeFrame(canFrame(0x100));
    auto result = detector.getResult();
    EXPECT_EQ("audi_mlb_evo", result.suggestedVehicleId);
    EXPECT_EQ(VehicleMake::Audi, result.make);
    EXPECT_EQ(DetectionConfidence::High, result.confidence);
    EXPECT_EQ(5, result.frameCount);
}

TEST(VehicleDetector, AudiDetection_LowConfidence) {
    VehicleDetector detector;
    detector.observeFrame(canFrame(0x100));
    auto result = detector.getResult();
    EXPECT_EQ("audi_mlb_evo", result.suggestedVehicleId);
    EXPECT_EQ(DetectionConfidence::Low, result.confidence);
}

TEST(VehicleDetector, GenericOBD2Detection) {
    VehicleDetector detector;
    detector.observeFrame(obd2Frame(0x01, 0x0D, {0x20}));
    auto result = detector.getResult();
    EXPECT_EQ("generic", result.suggestedVehicleId);
    EXPECT_GE(result.confidence, DetectionConfidence::Low);
}

TEST(VehicleDetector, ShortFrameTreatedAsOBD2NotCAN) {
    VehicleDetector detector;
    detector.observeFrame({0x41, 0x0D, 0x20});
    auto result = detector.getResult();
    EXPECT_EQ(0u, result.observedCanIds.size());
}

TEST(VehicleDetector, ConflictingEvidence_NoClearSuggestion) {
    VehicleDetector detector;
    detector.observeFrame(canFrame(0x108));
    detector.observeFrame(canFrame(0x100));
    auto result = detector.getResult();
    EXPECT_EQ(DetectionConfidence::None, result.confidence);
}

TEST(VehicleDetector, UnknownCANId_NoSuggestion) {
    VehicleDetector detector;
    detector.observeFrame(canFrame(0x200));
    auto result = detector.getResult();
    EXPECT_FALSE(result.hasSuggestion());
    EXPECT_EQ(1u, result.observedCanIds.size());
}

// ================================================
// Evidence Summary Tests
// ================================================

TEST(VehicleDetector, EvidenceSummary_ContainsCANIds) {
    VehicleDetector detector;
    detector.observeFrame(canFrame(0x108));
    detector.observeFrame(canFrame(0x118));
    auto result = detector.getResult();
    EXPECT_FALSE(result.evidenceSummary.empty());
}

// ================================================
// State Management Tests
// ================================================

TEST(VehicleDetector, Reset_ClearsState) {
    VehicleDetector detector;
    detector.observeFrame(canFrame(0x108));
    detector.observeFrame(canFrame(0x118));
    detector.reset();
    auto result = detector.getResult();
    EXPECT_TRUE(result.observedCanIds.empty());
    EXPECT_EQ(0, result.frameCount);
    EXPECT_FALSE(result.hasSuggestion());
}

TEST(VehicleDetector, Reset_ClearsVINState) {
    VehicleDetector detector;
    detector.feedVINResponse({0x49, 0x02, 0x01, 0x00, 0x00, 0x00, 0x35, 0x59,
                              0x4A, 0x33, 0x53, 0x32, 0x44, 0x58, 0x4D, 0x48,
                              0x31, 0x30, 0x35, 0x37, 0x36});
    ASSERT_FALSE(detector.getResult().vin.empty());
    detector.reset();
    EXPECT_TRUE(detector.getResult().vin.empty());
}

// ================================================
// Raw Frame History Tests
// ================================================

TEST(VehicleDetector, RecentFrames_ReturnsLastN) {
    VehicleDetector detector;
    for (int i = 0; i < 5; ++i) {
        detector.observeFrame(canFrame(0x100 + static_cast<uint16_t>(i)));
    }
    auto frames = detector.getRecentFrames(3);
    EXPECT_EQ(3u, frames.size());
    // Last 3 frames should have CAN IDs 0x102, 0x103, 0x104
}

TEST(VehicleDetector, RecentFrames_ReturnsAllIfLessThanMax) {
    VehicleDetector detector;
    detector.observeFrame(canFrame(0x100));
    auto frames = detector.getRecentFrames(10);
    EXPECT_EQ(1u, frames.size());
}

TEST(VehicleDetector, IsReceivingData_TrueWhenRecent) {
    VehicleDetector detector;
    detector.observeFrame(canFrame(0x108));
    EXPECT_TRUE(detector.isReceivingData());
}

TEST(VehicleDetector, IsReceivingData_FalseWhenNoFrames) {
    VehicleDetector detector;
    EXPECT_FALSE(detector.isReceivingData());
}

// ================================================
// Build Query Tests
// ================================================

TEST(VehicleDetector, BuildVINQuery) {
    auto query = VehicleDetector::buildVINQuery();
    ASSERT_EQ(2u, query.size());
    EXPECT_EQ(0x09, query[0]);
    EXPECT_EQ(0x02, query[1]);
}

TEST(VehicleDetector, BuildFuelTypeQuery) {
    auto query = VehicleDetector::buildFuelTypeQuery();
    ASSERT_EQ(2u, query.size());
    EXPECT_EQ(0x01, query[0]);
    EXPECT_EQ(0x51, query[1]);
}

// ================================================
// Realistic Tesla CAN Stream Test
// ================================================

TEST(VehicleDetector, RealisticTeslaStream) {
    VehicleDetector detector;
    std::vector<uint8_t> dirData = {0x00, 0x00, 0x00, 0x90, 0x01, 0x10, 0x27, 0x00};
    std::vector<uint8_t> diData = {0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00};
    std::vector<uint8_t> sccmData = {0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00};

    for (int i = 0; i < 10; ++i) {
        detector.observeFrame(canFrame(0x108, dirData));
        detector.observeFrame(canFrame(0x118, diData));
    }
    for (int i = 0; i < 5; ++i) {
        detector.observeFrame(canFrame(0x129, sccmData));
    }

    auto result = detector.getResult();
    EXPECT_EQ("tesla", result.suggestedVehicleId);
    EXPECT_EQ(DetectionConfidence::High, result.confidence);
    EXPECT_EQ(25, result.frameCount);
    EXPECT_EQ(3u, result.observedCanIds.size());
}
