#include <gtest/gtest.h>
#include <memory>
#include <string>
#include "vehicle-sim/domain/SignalTranslatorFactory.h"
#include "vehicle-sim/domain/ISignalTranslator.h"
#include "vehicle-sim/domain/OBD2SignalTranslator.h"
#include "vehicle-sim/domain/TeslaSignalTranslator.h"

using namespace vehicle_sim::domain;

// ================================================
// SignalTranslatorFactory Tests
// TDD RED Phase — tests assert correct behaviour
// Factory creates the right translator by vehicle type
// ================================================

class SignalTranslatorFactoryTest : public ::testing::Test {
protected:
    SignalTranslatorFactory factory;
};

// ================================================
// Generic (default) vehicle type
// ================================================

TEST_F(SignalTranslatorFactoryTest, DefaultCreatesOBD2Translator) {
    auto translator = factory.create("generic");
    ASSERT_NE(translator, nullptr);

    // Verify it's an OBD2 translator by checking OBD2 response validation
    std::vector<uint8_t> obd2Resp = {0x41, 0x0D, 0x64};  // Mode 01 response: speed 100
    EXPECT_TRUE(translator->isValidPacket(obd2Resp));
}

TEST_F(SignalTranslatorFactoryTest, EmptyStringCreatesOBD2Translator) {
    auto translator = factory.create("");
    ASSERT_NE(translator, nullptr);

    std::vector<uint8_t> obd2Resp = {0x41, 0x0D, 0x64};
    EXPECT_TRUE(translator->isValidPacket(obd2Resp));
}

// ================================================
// Tesla vehicle type
// ================================================

TEST_F(SignalTranslatorFactoryTest, TeslaCreatesTeslaTranslator) {
    auto translator = factory.create("tesla");
    ASSERT_NE(translator, nullptr);

    // Verify it's a Tesla translator by checking Tesla packet validation
    // Tesla packet: [0xAA][0x55][length=5][throttle][speed_lo][speed_hi][accel][brake][checksum]
    std::vector<uint8_t> teslaPkt = {0xAA, 0x55, 0x05, 0x32, 0x64, 0x00, 0x05, 0x00, 0x9F};
    EXPECT_TRUE(translator->isValidPacket(teslaPkt));
}

// ================================================
// Case insensitive
// ================================================

TEST_F(SignalTranslatorFactoryTest, CaseInsensitive) {
    auto t1 = factory.create("GENERIC");
    auto t2 = factory.create("Generic");
    auto t3 = factory.create("TESLA");
    auto t4 = factory.create("Tesla");

    ASSERT_NE(t1, nullptr);
    ASSERT_NE(t2, nullptr);
    ASSERT_NE(t3, nullptr);
    ASSERT_NE(t4, nullptr);
}

// ================================================
// Unknown vehicle type falls back to generic
// ================================================

TEST_F(SignalTranslatorFactoryTest, UnknownTypeFallsBackToGeneric) {
    auto translator = factory.create("unknown_brand");
    ASSERT_NE(translator, nullptr);

    // Should behave as generic OBD2
    std::vector<uint8_t> obd2Resp = {0x41, 0x0D, 0x64};
    EXPECT_TRUE(translator->isValidPacket(obd2Resp));
}

// ================================================
// Available types
// ================================================

TEST_F(SignalTranslatorFactoryTest, ListsAvailableVehicleTypes) {
    auto types = factory.availableTypes();

    EXPECT_NE(std::find(types.begin(), types.end(), "generic"), types.end());
    EXPECT_NE(std::find(types.begin(), types.end(), "tesla"), types.end());
    EXPECT_GE(types.size(), 2u);
}
