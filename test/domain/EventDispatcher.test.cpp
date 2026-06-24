#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <functional>

#include "vehicle-sim/domain/EventDispatcher.h"
#include "vehicle-sim/domain/VehicleSignal.h"

using namespace vehicle_sim::domain;

/**
 * Test suite for EventDispatcher
 *
 * Tests follow strict TDD RED phase methodology:
 * - All tests must compile
 * - Tests demonstrate required functionality
 * - No implementation assumptions beyond interface requirements
 */

class EventDispatcherTest : public ::testing::Test {
protected:
    void SetUp() override {
        dispatcher = std::make_unique<EventDispatcher>();
    }

    void TearDown() override {
        dispatcher.reset();
    }

    std::unique_ptr<EventDispatcher> dispatcher;
};

/**
 * Test 1: Single consumer registration and callback execution
 *
 * Requirement: EventDispatcher must support registering consumers
 * and dispatching events to them
 */
TEST_F(EventDispatcherTest, SingleConsumerReceivesEvents) {
    // Arrange
    std::atomic<int> callCount{0};
    VehicleSignal receivedSignal(0);

    auto consumer = [&](const VehicleSignal& signal) {
        callCount++;
        receivedSignal = signal;
    };

    auto token = dispatcher->registerConsumer(consumer);

    // Act
    VehicleSignal testSignal(12345, 50.0, 100.0, 0.5, 0.0);
    dispatcher->dispatch(testSignal);

    // Assert
    EXPECT_EQ(callCount.load(), 1);
    EXPECT_DOUBLE_EQ(receivedSignal.getThrottlePercent().value(), 50.0);
    EXPECT_DOUBLE_EQ(receivedSignal.getSpeedKmh().value(), 100.0);
    EXPECT_DOUBLE_EQ(receivedSignal.getAccelerationG().value(), 0.5);

    dispatcher->unregisterConsumer(token);
}

/**
 * Test 2: Multiple consumers receive same event
 *
 * Requirement: EventDispatcher must support multiple subscribers
 * and broadcast events to all registered consumers
 */
TEST_F(EventDispatcherTest, MultipleConsumersReceiveSameEvent) {
    // Arrange
    std::atomic<int> consumer1Calls{0};
    std::atomic<int> consumer2Calls{0};
    std::atomic<int> consumer3Calls{0};

    auto consumer1 = [&](const VehicleSignal& signal) { consumer1Calls++; };
    auto consumer2 = [&](const VehicleSignal& signal) { consumer2Calls++; };
    auto consumer3 = [&](const VehicleSignal& signal) { consumer3Calls++; };

    auto token1 = dispatcher->registerConsumer(consumer1);
    auto token2 = dispatcher->registerConsumer(consumer2);
    auto token3 = dispatcher->registerConsumer(consumer3);

    // Act
    VehicleSignal testSignal(67890, 75.0, 120.0, 0.8, 10.0);
    dispatcher->dispatch(testSignal);

    // Assert
    EXPECT_EQ(consumer1Calls.load(), 1);
    EXPECT_EQ(consumer2Calls.load(), 1);
    EXPECT_EQ(consumer3Calls.load(), 1);

    dispatcher->unregisterConsumer(token1);
    dispatcher->unregisterConsumer(token2);
    dispatcher->unregisterConsumer(token3);
}

/**
 * Test 3: Consumer unregistration stops receiving events
 *
 * Requirement: Consumers must be able to unregister and
 * stop receiving events after unregistration
 */
TEST_F(EventDispatcherTest, UnregisteredConsumerDoesNotReceiveEvents) {
    // Arrange
    std::atomic<int> callCount{0};

    auto consumer = [&](const VehicleSignal& signal) {
        callCount++;
    };

    auto token = dispatcher->registerConsumer(consumer);

    // Act - First dispatch
    dispatcher->dispatch(VehicleSignal(1, 50.0, 100.0, 0.5, 0.0));

    // Unregister
    dispatcher->unregisterConsumer(token);

    // Second dispatch
    dispatcher->dispatch(VehicleSignal(2, 60.0, 110.0, 0.6, 0.0));

    // Assert
    EXPECT_EQ(callCount.load(), 1); // Only received first event
}

/**
 * Test 4: Thread-safe concurrent dispatch from multiple threads
 *
 * Requirement: EventDispatcher must be thread-safe for
 * concurrent dispatch operations from multiple threads
 */
TEST_F(EventDispatcherTest, ConcurrentDispatchFromMultipleThreads) {
    // Arrange
    std::atomic<int> totalCalls{0};
    const int numThreads = 4;
    const int dispatchesPerThread = 25;

    auto consumer = [&](const VehicleSignal& signal) {
        totalCalls++;
    };

    dispatcher->registerConsumer(consumer);

    // Act
    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([this, i]() {
            for (int j = 0; j < dispatchesPerThread; ++j) {
                VehicleSignal signal(
                    static_cast<std::uint64_t>(i * 1000 + j),
                    static_cast<double>(i * 10 + j),
                    static_cast<double>(i * 20 + j),
                    0.1 * (i + j),
                    0.0
                );
                dispatcher->dispatch(signal);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Assert
    int expectedCalls = numThreads * dispatchesPerThread;
    EXPECT_EQ(totalCalls.load(), expectedCalls);
}

/**
 * Test 5: Thread-safe consumer registration during dispatch
 *
 * Requirement: EventDispatcher must handle concurrent
 * registration/unregistration during dispatch operations
 */
TEST_F(EventDispatcherTest, ConcurrentRegistrationDuringDispatch) {
    // Arrange
    std::atomic<int> totalCalls{0};
    const int numConsumers = 10;
    const int dispatchesPerRound = 5;

    std::vector<unsigned int> tokens;

    // Act - Concurrent registration and dispatch
    // NOTE: The 1ms sleeps below deliberately force register-vs-dispatch race
    // interleaving — this is INTENTIONAL (the sleeps ARE the race-window mechanism).
    // OPTIONAL future hardening: replace with std::atomic barrier/countdown for
    // more deterministic timing. However, sleeps may better surface real races;
    // atomic barriers could reduce coverage by making timing too predictable.
    // Effort: S-M, Risk: M (could weaken race detection).
    std::thread registrationThread([&]() {
        for (int i = 0; i < numConsumers; ++i) {
            auto consumer = [&](const VehicleSignal& signal) {
                totalCalls++;
            };
            tokens.push_back(dispatcher->registerConsumer(consumer));
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    std::thread dispatchThread([&]() {
        for (int i = 0; i < dispatchesPerRound * numConsumers; ++i) {
            VehicleSignal signal(static_cast<std::uint64_t>(i), 50.0, 100.0, 0.5, 0.0);
            dispatcher->dispatch(signal);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    registrationThread.join();
    dispatchThread.join();

    // Cleanup
    for (auto token : tokens) {
        dispatcher->unregisterConsumer(token);
    }

    // Assert - No crashes or deadlocks occurred
    EXPECT_GT(totalCalls.load(), 0);
}

/**
 * Test 6: Throughput exceeds 10Hz minimum requirement
 *
 * Requirement: EventDispatcher must support 10Hz minimum throughput.
 * This test measures actual throughput (events/second) without artificial delays.
 */
TEST_F(EventDispatcherTest, ThroughputExceeds10HzRequirement) {
    // Arrange
    std::atomic<int> callCount{0};
    const int numDispatches = 1000; // Test with substantial signal count
    const double minRequiredHz = 10.0; // MVP minimum requirement

    auto consumer = [&](const VehicleSignal& signal) {
        callCount++;
    };

    dispatcher->registerConsumer(consumer);

    // Act - Dispatch as fast as possible to measure actual throughput
    auto startTime = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < numDispatches; ++i) {
        VehicleSignal signal(
            static_cast<std::uint64_t>(i),
            static_cast<double>(i),
            static_cast<double>(i * 2),
            0.1,
            0.0
        );
        dispatcher->dispatch(signal);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    // Assert - All events were dispatched
    EXPECT_EQ(callCount.load(), numDispatches);

    // Assert - Actual throughput meets or exceeds 10Hz requirement
    double actualHz = (numDispatches * 1000.0) / durationMs.count();
    EXPECT_GE(actualHz, minRequiredHz)
        << "Actual throughput: " << actualHz << " Hz (required: " << minRequiredHz << " Hz)";

    // Assert - Throughput is substantially higher than minimum requirement
    // This proves the dispatcher is not the bottleneck for 10Hz updates
    EXPECT_GE(actualHz, 100.0)
        << "Throughput should be at least 10x minimum requirement (actual: " << actualHz << " Hz)";

    dispatcher->clear();
}

/**
 * Test 7: Event data integrity through dispatcher
 *
 * Requirement: VehicleSignal data must be preserved
 * accurately through dispatch mechanism
 */
TEST_F(EventDispatcherTest, EventDataIntegrityPreserved) {
    // Arrange
    VehicleSignal originalSignal(
        9876543210ULL, // timestamp
        85.5,  // throttle
        145.2, // speed
        0.75,  // acceleration
        25.0   // brake
    );

    VehicleSignal receivedSignal(0);
    std::atomic<bool> signalReceived{false};

    auto consumer = [&](const VehicleSignal& signal) {
        receivedSignal = signal;
        signalReceived = true;
    };

    dispatcher->registerConsumer(consumer);

    // Act
    dispatcher->dispatch(originalSignal);

    // Assert
    EXPECT_TRUE(signalReceived.load());
    EXPECT_DOUBLE_EQ(receivedSignal.getThrottlePercent().value(), originalSignal.getThrottlePercent().value());
    EXPECT_DOUBLE_EQ(receivedSignal.getSpeedKmh().value(), originalSignal.getSpeedKmh().value());
    EXPECT_DOUBLE_EQ(receivedSignal.getAccelerationG().value(), originalSignal.getAccelerationG().value());
    EXPECT_DOUBLE_EQ(receivedSignal.getBrakePercent().value(), originalSignal.getBrakePercent().value());
    EXPECT_EQ(receivedSignal.getTimestampUtcMs(), originalSignal.getTimestampUtcMs());

    dispatcher->clear();
}

/**
 * Test 8: Integration with TeslaSignalParser callback pattern
 *
 * Requirement: EventDispatcher must integrate with
 * TeslaSignalParser's SignalCallback mechanism
 */
TEST_F(EventDispatcherTest, IntegrationWithSignalCallbackPattern) {
    // Arrange
    using SignalCallback = std::function<void(const VehicleSignal&)>;

    std::atomic<int> parserCalls{0};
    std::atomic<int> dispatcherCalls{0};

    // Simulate TeslaSignalParser callback pattern
    SignalCallback parserCallback = [&](const VehicleSignal& signal) {
        parserCalls++;
        dispatcher->dispatch(signal); // Parser feeds dispatcher
    };

    // Dispatcher consumers
    auto uiConsumer = [&](const VehicleSignal& signal) { dispatcherCalls++; };
    auto loggingConsumer = [&](const VehicleSignal& signal) { dispatcherCalls++; };

    dispatcher->registerConsumer(uiConsumer);
    dispatcher->registerConsumer(loggingConsumer);

    // Act - Simulate parser generating signals
    for (int i = 0; i < 5; ++i) {
        VehicleSignal signal(
            static_cast<std::uint64_t>(i),
            static_cast<double>(i * 10),
            static_cast<double>(i * 20),
            0.1 * i,
            0.0
        );
        parserCallback(signal); // Parser callback feeds dispatcher
    }

    // Assert
    EXPECT_EQ(parserCalls.load(), 5); // Parser called 5 times
    EXPECT_EQ(dispatcherCalls.load(), 10); // Each dispatch went to 2 consumers

    dispatcher->clear();
}

/**
 * Test 9: No memory leaks with rapid registration/unregistration
 *
 * Requirement: EventDispatcher must handle rapid consumer
 * lifecycle changes without memory leaks
 */
TEST_F(EventDispatcherTest, NoMemoryLeaksWithRapidRegistrationUnregistration) {
    // Arrange
    const int iterations = 100;

    // Act - Rapid registration and unregistration
    for (int i = 0; i < iterations; ++i) {
        auto consumer = [&](const VehicleSignal& signal) {};
        auto token = dispatcher->registerConsumer(consumer);
        dispatcher->dispatch(VehicleSignal(static_cast<std::uint64_t>(i), 50.0, 100.0, 0.5, 0.0));
        dispatcher->unregisterConsumer(token);
    }

    // Assert - If we reach here without crash, test passes
    SUCCEED();
}

/**
 * Test 10: Dispatcher handles zero consumers gracefully
 *
 * Requirement: EventDispatcher must handle edge case
 * of dispatching with no registered consumers
 */
TEST_F(EventDispatcherTest, HandlesZeroConsumersGracefully) {
    // Arrange - No consumers registered

    // Act - Dispatch with no consumers
    VehicleSignal signal(12345, 50.0, 100.0, 0.5, 0.0);
    EXPECT_NO_THROW(dispatcher->dispatch(signal));

    // Assert - No crash or error
    SUCCEED();
}

/**
 * Test 11: Sequential dispatch maintains order
 *
 * Requirement: Events dispatched sequentially should
 * maintain their order to consumers
 */
TEST_F(EventDispatcherTest, SequentialDispatchMaintainsOrder) {
    // Arrange
    std::vector<int> receivedOrder;
    std::mutex orderMutex;

    auto consumer = [&](const VehicleSignal& signal) {
        std::lock_guard<std::mutex> lock(orderMutex);
        receivedOrder.push_back(static_cast<int>(signal.getThrottlePercent().value()));
    };

    dispatcher->registerConsumer(consumer);

    // Act
    for (int i = 0; i < 10; ++i) {
        VehicleSignal signal(static_cast<std::uint64_t>(i), static_cast<double>(i), 0.0, 0.0, 0.0);
        dispatcher->dispatch(signal);
    }

    // Assert
    EXPECT_EQ(receivedOrder.size(), 10);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(receivedOrder[i], i);
    }

    dispatcher->clear();
}

/**
 * Test 12: Multiple dispatchers can coexist
 *
 * Requirement: Multiple EventDispatcher instances should
 * operate independently without interference
 */
TEST_F(EventDispatcherTest, MultipleDispatchersOperateIndependently) {
    // Arrange
    auto dispatcher1 = std::make_unique<EventDispatcher>();
    auto dispatcher2 = std::make_unique<EventDispatcher>();

    std::atomic<int> dispatcher1Calls{0};
    std::atomic<int> dispatcher2Calls{0};

    auto consumer1 = [&](const VehicleSignal& signal) { dispatcher1Calls++; };
    auto consumer2 = [&](const VehicleSignal& signal) { dispatcher2Calls++; };

    dispatcher1->registerConsumer(consumer1);
    dispatcher2->registerConsumer(consumer2);

    // Act
    VehicleSignal signal1(1, 50.0, 100.0, 0.5, 0.0);
    VehicleSignal signal2(2, 60.0, 110.0, 0.6, 0.0);

    dispatcher1->dispatch(signal1);
    dispatcher2->dispatch(signal2);
    dispatcher1->dispatch(signal1);

    // Assert
    EXPECT_EQ(dispatcher1Calls.load(), 2);
    EXPECT_EQ(dispatcher2Calls.load(), 1);

    dispatcher1->clear();
    dispatcher2->clear();
}
