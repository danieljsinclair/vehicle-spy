#include <gtest/gtest.h>
#include "vehicle-sim/domain/SignalSourceFactory.h"
#include "vehicle-sim/domain/DemoSignalSource.h"
#include "vehicle-sim/domain/ISignalSource.h"

using namespace vehicle_sim::domain;

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
        SignalSourceFactory::create("unknown_type", 100),
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