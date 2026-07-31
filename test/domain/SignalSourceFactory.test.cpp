#include <gtest/gtest.h>
#include "vehicle-sim/domain/SignalSourceFactory.h"
#include "vehicle-sim/domain/DemoSignalSource.h"
#include "vehicle-sim/domain/SimulationSignalSource.h"
#include "vehicle-sim/domain/ISignalSource.h"
#include "vehicle-sim/domain/VehicleSignal.h"
#include <chrono>
#include <thread>

using namespace vehicle_sim::domain;

// SignalSourceFactory is the thin demo-source factory for the legacy
// ISignalSource path (still used by BLE/simulation). TCP-target parsing and
// the createTcp entry were removed when TCP moved to the pipeline
// (PipelineFactory::buildPipelineSource + pipeline::parseTcpTarget); the
// parseTcpTarget tests now live in test/pipeline/PipelineFactory.test.cpp.

class SignalSourceFactoryTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST(SignalSourceFactoryTest, CreateDemoSource_ReturnsDemoSignalSource) {
    auto source = SignalSourceFactory::create("demo", 100);

    ASSERT_NE(source, nullptr) << "Factory should return non-null source";
    EXPECT_NE(dynamic_cast<DemoSignalSource*>(source.get()), nullptr)
        << "Factory should return DemoSignalSource for 'demo' type";
}

TEST(SignalSourceFactoryTest, CreateDemoSource_UsesProvidedInterval) {
    const int expectedInterval = 250;
    auto source = SignalSourceFactory::create("demo", expectedInterval);

    auto* demoSource = dynamic_cast<DemoSignalSource*>(source.get());
    ASSERT_NE(demoSource, nullptr) << "Should be able to cast to DemoSignalSource";

    // The interval is used internally - we can verify it by checking behavior
    // For now, just ensure the source was created
    EXPECT_NE(source, nullptr);
}

TEST(SignalSourceFactoryTest, CreateDemoSource_DefaultIntervalWorks) {
    auto source = SignalSourceFactory::create("demo", 0);

    ASSERT_NE(source, nullptr) << "Factory should handle zero interval";
    EXPECT_NE(dynamic_cast<DemoSignalSource*>(source.get()), nullptr);
}

TEST(SignalSourceFactoryTest, CreateUnknownSource_ThrowsInvalidArgument) {
    EXPECT_THROW(
        (void)SignalSourceFactory::create("unknown_type", 100),
        std::invalid_argument
    );
}

TEST(SignalSourceFactoryTest, ReturnedSourceImplementsISignalSource) {
    auto source = SignalSourceFactory::create("demo", 100);

    // Verify the source can be used as ISignalSource
    ISignalSource* basePtr = source.get();
    EXPECT_NE(basePtr, nullptr);

    // Verify interface methods can be called (may need real implementation)
    EXPECT_NO_THROW({
        basePtr->start();
        auto signal = basePtr->latestSignal();
        (void)signal;  // Suppress unused warning
        basePtr->stop();
    });
}

// Factory wires a live VehicleSimulator into a SimulationSignalSource and
// returns it as a working ISignalSource. Beyond the type, this asserts the
// factory-built source actually polls: after start(), latestSignal() surfaces
// a live (timestamp > 0) signal from the simulator before stop(). Catches a
// mis-wired factory (e.g. null/placeholder simulator) that a type cast alone
// would miss.
TEST(SignalSourceFactoryTest, CreateSimulationSource_ReturnsWorkingSimulationSignalSource) {
    auto source = SignalSourceFactory::create("simulation", 1);

    ASSERT_NE(source, nullptr) << "Factory should return non-null source for 'simulation'";
    EXPECT_NE(dynamic_cast<SimulationSignalSource*>(source.get()), nullptr)
        << "Factory should return SimulationSignalSource for 'simulation' type";

    source->start();
    VehicleSignal observed{VehicleSignal::Params{.timestampUtcMs = 0}};
    for (int i = 0; i < 200; ++i) {
        observed = source->latestSignal();
        if (observed.getTimestampUtcMs() > 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    source->stop();

    EXPECT_GT(observed.getTimestampUtcMs(), 0ULL)
        << "factory-built simulation source never surfaced a live signal";
}
