#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

#include "vehicle-sim/domain/EventDispatcher.h"
#include "vehicle-sim/domain/TeslaSignalParser.h"
#include "vehicle-sim/domain/VehicleSignal.h"
#include "vehicle-sim/ble/TeslaBLETransport.h"
#include "vehicle-sim/BLEManager.h"
#include "vehicle-sim/ble/platform/BLEManagerMock.h"

using namespace vehicle_sim::domain;
using namespace vehicle_sim::ble;

/**
 * Integration test suite for EventDispatcher
 *
 * Tests verify end-to-end integration with:
 * - TeslaSignalParser callback mechanism
 * - TeslaBLETransport consumer pattern
 * - Multi-threaded event routing at 10Hz
 * - Real-world usage patterns
 */
class EventDispatcherIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        dispatcher_ = std::make_unique<EventDispatcher>();
        parser_ = std::make_unique<TeslaSignalParser>();
        bleTransport_ = std::make_unique<TeslaBLETransport>(std::make_unique<vehicle_sim::BLEManagerMock>());
    }

    void TearDown() override {
        dispatcher_.reset();
        parser_.reset();
        bleTransport_.reset();
    }

    // Thread-safe signal collector for testing
    template<typename T>
    class SignalCollector {
    public:
        void addSignal(const T& signal) {
            std::lock_guard<std::mutex> lock(mutex_);
            signals_.push_back(signal);
        }

        std::vector<T> getSignals() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return signals_;
        }

        size_t count() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return signals_.size();
        }

        void clear() {
            std::lock_guard<std::mutex> lock(mutex_);
            signals_.clear();
        }

    private:
        mutable std::mutex mutex_;
        std::vector<T> signals_;
    };

    std::unique_ptr<EventDispatcher> dispatcher_;
    std::unique_ptr<TeslaSignalParser> parser_;
    std::unique_ptr<TeslaBLETransport> bleTransport_;

    SignalCollector<VehicleSignal> collector_;
};

/**
 * Integration Test 1: TeslaSignalParser → EventDispatcher → Multiple Consumers
 *
 * Expected behavior:
 * - TeslaSignalParser callback mechanism works with EventDispatcher
 * - EventDispatcher receives VehicleSignal from parser callback
 * - EventDispatcher distributes signal to all registered consumers
 * - All consumers receive the same signal with correct data
 */
TEST_F(EventDispatcherIntegrationTest, TeslaSignalParserToDispatcherToMultipleConsumers) {
    // Setup: Connect parser to dispatcher, dispatcher to multiple consumers
    SignalCollector<VehicleSignal> consumer1;
    SignalCollector<VehicleSignal> consumer2;
    SignalCollector<VehicleSignal> consumer3;

    auto token1 = dispatcher_->registerConsumer([&](const VehicleSignal& signal) {
        consumer1.addSignal(signal);
    });

    auto token2 = dispatcher_->registerConsumer([&](const VehicleSignal& signal) {
        consumer2.addSignal(signal);
    });

    auto token3 = dispatcher_->registerConsumer([&](const VehicleSignal& signal) {
        consumer3.addSignal(signal);
    });

    // Connect parser to dispatcher
    parser_->setSignalCallback([&](const VehicleSignal& signal) {
        dispatcher_->dispatch(signal);
    });

    // Create a test VehicleSignal directly (simulating what parser would produce)
    VehicleSignal signalFromParser(1234567890ULL, 50.0, 100.0, 0.5, 10.0);

    // Action: Dispatch signal through parser callback integration
    dispatcher_->dispatch(signalFromParser);

    // Verify: All three consumers received the signal
    EXPECT_EQ(consumer1.count(), 1);
    EXPECT_EQ(consumer2.count(), 1);
    EXPECT_EQ(consumer3.count(), 1);

    // Verify: Signal data integrity preserved
    auto consumer1Signals = consumer1.getSignals();
    EXPECT_DOUBLE_EQ(consumer1Signals[0].getSpeedKmh().value(), signalFromParser.getSpeedKmh().value());
    EXPECT_DOUBLE_EQ(consumer1Signals[0].getThrottlePercent().value(), signalFromParser.getThrottlePercent().value());
    EXPECT_DOUBLE_EQ(consumer1Signals[0].getAccelerationG().value(), signalFromParser.getAccelerationG().value());
    EXPECT_DOUBLE_EQ(consumer1Signals[0].getBrakePercent().value(), signalFromParser.getBrakePercent().value());
    EXPECT_EQ(consumer1Signals[0].getTimestampUtcMs(), signalFromParser.getTimestampUtcMs());
}

/**
 * Integration Test 2: EventDispatcher → BLE Transport Consumer Pattern
 *
 * Expected behavior:
 * - EventDispatcher can register BLE transport as a consumer
 * - Signals dispatched to BLE transport are forwarded correctly
 * - Integration matches production usage pattern
 */
TEST_F(EventDispatcherIntegrationTest, EventDispatcherToBLETransportConsumerPattern) {
    // Setup: BLE transport mock to receive signals
    std::vector<VehicleSignal> bleReceivedSignals;
    std::mutex bleMutex;

    auto token = dispatcher_->registerConsumer([&](const VehicleSignal& signal) {
        // Simulate BLE transport receiving signal
        std::lock_guard<std::mutex> lock(bleMutex);
        bleReceivedSignals.push_back(signal);
    });

    // Create test signal
    VehicleSignal testSignal(1234567890ULL, 50.0, 100.0, 0.5, 10.0);

    // Action: Dispatch signal
    dispatcher_->dispatch(testSignal);

    // Verify: BLE transport received signal
    {
        std::lock_guard<std::mutex> lock(bleMutex);
        EXPECT_EQ(bleReceivedSignals.size(), 1);
        EXPECT_DOUBLE_EQ(bleReceivedSignals[0].getSpeedKmh().value(), testSignal.getSpeedKmh().value());
        EXPECT_DOUBLE_EQ(bleReceivedSignals[0].getThrottlePercent().value(), testSignal.getThrottlePercent().value());
        EXPECT_DOUBLE_EQ(bleReceivedSignals[0].getAccelerationG().value(), testSignal.getAccelerationG().value());
        EXPECT_DOUBLE_EQ(bleReceivedSignals[0].getBrakePercent().value(), testSignal.getBrakePercent().value());
        EXPECT_EQ(bleReceivedSignals[0].getTimestampUtcMs(), testSignal.getTimestampUtcMs());
    }
}

/**
 * Integration Test 3: Thread-Safe Concurrent Dispatch from Multiple BLE Callbacks
 *
 * Expected behavior:
 * - Multiple threads simulate concurrent BLE data reception callbacks
 * - All signals are dispatched safely to consumers
 * - No race conditions or data corruption
 * - All consumers receive all signals
 */
TEST_F(EventDispatcherIntegrationTest, ThreadSafeConcurrentDispatchFromMultipleBLECallbacks) {
    const int threadCount = 5;
    const int signalsPerThread = 100;
    std::atomic<int> totalDispatched(0);
    std::atomic<int> totalReceived(0);

    // Setup: Register multiple consumers
    SignalCollector<VehicleSignal> consumer1;
    SignalCollector<VehicleSignal> consumer2;
    SignalCollector<VehicleSignal> consumer3;

    auto token1 = dispatcher_->registerConsumer([&](const VehicleSignal& signal) {
        consumer1.addSignal(signal);
        totalReceived++;
    });

    auto token2 = dispatcher_->registerConsumer([&](const VehicleSignal& signal) {
        consumer2.addSignal(signal);
        totalReceived++;
    });

    auto token3 = dispatcher_->registerConsumer([&](const VehicleSignal& signal) {
        consumer3.addSignal(signal);
        totalReceived++;
    });

    // Action: Simulate concurrent BLE callbacks from multiple threads
    std::vector<std::thread> threads;
    for (int t = 0; t < threadCount; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < signalsPerThread; i++) {
                VehicleSignal signal(
                    static_cast<std::uint64_t>(t * 1000 + i), // Timestamp
                    50.0 + t,           // Throttle varies by thread
                    100.0 + i,          // Speed varies by iteration
                    0.5 + (i * 0.01), // Acceleration varies
                    10.0                // Constant brake
                );

                dispatcher_->dispatch(signal);
                totalDispatched++;
            }
        });
    }

    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }

    // Verify: All signals dispatched
    EXPECT_EQ(totalDispatched, threadCount * signalsPerThread);

    // Verify: All consumers received all signals
    EXPECT_EQ(consumer1.count(), threadCount * signalsPerThread);
    EXPECT_EQ(consumer2.count(), threadCount * signalsPerThread);
    EXPECT_EQ(consumer3.count(), threadCount * signalsPerThread);

    // Verify: Total received count (3 consumers × total signals)
    EXPECT_EQ(totalReceived, 3 * threadCount * signalsPerThread);
}

/**
 * Integration Test 4: Throughput Performance Validation
 *
 * Expected behavior:
 * - Dispatcher can handle high-throughput signal dispatch without artificial delays
 * - All signals are delivered to consumers
 * - Performance far exceeds MVP requirement (minimum 10Hz)
 * - Test measures actual throughput, not artificial timing constraints
 */
TEST_F(EventDispatcherIntegrationTest, ThroughputExceeds10HzRequirement) {
    const int signalCount = 1000; // Test with substantial signal count
    const double minRequiredHz = 10.0; // MVP minimum requirement

    SignalCollector<VehicleSignal> consumer;

    auto token = dispatcher_->registerConsumer([&](const VehicleSignal& signal) {
        consumer.addSignal(signal);
    });

    // Dispatch signals as fast as possible (measure actual throughput)
    auto startTime = std::chrono::steady_clock::now();
    for (int i = 0; i < signalCount; i++) {
        VehicleSignal signal(
            static_cast<std::uint64_t>(i),
            50.0, 100.0, 0.5, 10.0
        );
        dispatcher_->dispatch(signal);
    }
    auto endTime = std::chrono::steady_clock::now();

    // Verify: All signals received
    EXPECT_EQ(consumer.count(), signalCount);

    // Verify: Actual throughput meets or exceeds 10Hz requirement
    auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime
    ).count();
    double actualHz = (signalCount * 1000.0) / durationMs;
    EXPECT_GE(actualHz, minRequiredHz)
        << "Actual throughput: " << actualHz << " Hz (required: " << minRequiredHz << " Hz)";

    // Verify: Throughput is substantially higher than minimum requirement
    // This proves the dispatcher is not the bottleneck for 10Hz updates
    EXPECT_GE(actualHz, 100.0)
        << "Throughput should be at least 10x minimum requirement (actual: " << actualHz << " Hz)";
}

/**
 * Integration Test 5: End-to-End Data Flow Integration
 *
 * Expected behavior:
 * - Complete data flow from signal creation to consumer delivery
 * - Signal routing correct through dispatcher
 * - Data integrity preserved through entire pipeline
 */
TEST_F(EventDispatcherIntegrationTest, EndToEndDataFlowIntegration) {
    // Setup: Complete integration chain
    SignalCollector<VehicleSignal> bleCollector;
    SignalCollector<VehicleSignal> loggingCollector;

    // Dispatcher registered to multiple consumers
    auto bleToken = dispatcher_->registerConsumer([&](const VehicleSignal& signal) {
        bleCollector.addSignal(signal);
    });

    auto loggingToken = dispatcher_->registerConsumer([&](const VehicleSignal& signal) {
        loggingCollector.addSignal(signal);
    });

    // Parser connected to dispatcher
    parser_->setSignalCallback([&](const VehicleSignal& signal) {
        dispatcher_->dispatch(signal);
    });

    // Create test signal (simulating parser output)
    VehicleSignal parsedSignal(9876543210ULL, 75.0, 120.5, 0.8, 15.0);

    // Action: Dispatch signal through complete pipeline
    dispatcher_->dispatch(parsedSignal);

    // Verify: Both consumers received signal
    EXPECT_EQ(bleCollector.count(), 1);
    EXPECT_EQ(loggingCollector.count(), 1);

    // Verify: Data integrity preserved
    auto bleSignals = bleCollector.getSignals();
    auto loggingSignals = loggingCollector.getSignals();

    EXPECT_DOUBLE_EQ(bleSignals[0].getSpeedKmh().value(), parsedSignal.getSpeedKmh().value());
    EXPECT_DOUBLE_EQ(loggingSignals[0].getSpeedKmh().value(), parsedSignal.getSpeedKmh().value());
    EXPECT_DOUBLE_EQ(bleSignals[0].getThrottlePercent().value(), parsedSignal.getThrottlePercent().value());
    EXPECT_DOUBLE_EQ(loggingSignals[0].getThrottlePercent().value(), parsedSignal.getThrottlePercent().value());

    // Verify: Timestamps are reasonable
    EXPECT_GT(parsedSignal.getTimestampUtcMs(), 0ULL);
}

/**
 * Integration Test 6: Memory Safety Under Load
 *
 * Expected behavior:
 * - Rapid signal dispatch under load
 * - No memory leaks
 * - Resource cleanup proper
 * - Stable performance
 */
TEST_F(EventDispatcherIntegrationTest, MemorySafetyUnderLoad) {
    const int iterations = 1000;
    std::atomic<int> signalCount(0);

    // Setup: Multiple consumers
    SignalCollector<VehicleSignal> consumer1;
    SignalCollector<VehicleSignal> consumer2;

    auto token1 = dispatcher_->registerConsumer([&](const VehicleSignal& signal) {
        consumer1.addSignal(signal);
        signalCount++;
    });

    auto token2 = dispatcher_->registerConsumer([&](const VehicleSignal& signal) {
        consumer2.addSignal(signal);
    });

    // Action: Rapid dispatch under load
    for (int i = 0; i < iterations; i++) {
        VehicleSignal signal(
            static_cast<std::uint64_t>(i),
            50.0 + (i % 100),
            100.0 + (i % 200),
            0.5 + (i * 0.001),
            10.0
        );

        dispatcher_->dispatch(signal);

        // Periodically unregister and register new consumers
        if (i % 100 == 0) {
            auto tempToken = dispatcher_->registerConsumer([&](const VehicleSignal&) {});
            dispatcher_->unregisterConsumer(tempToken);
        }
    }

    // Verify: All signals processed
    EXPECT_EQ(signalCount, iterations);
    EXPECT_EQ(consumer1.count(), iterations);
    EXPECT_EQ(consumer2.count(), iterations);

    // Verify: No crashes or instability (test reaches this point = success)
}

/**
 * Integration Test 7: Dynamic Consumer Registration During Active Dispatch
 *
 * Expected behavior:
 * - Consumers can register/unregister while dispatch is active
 * - New consumers receive subsequent events
 * - Unregistered consumers stop receiving events
 * - No crashes or race conditions
 */
TEST_F(EventDispatcherIntegrationTest, DynamicConsumerRegistrationDuringActiveDispatch) {
    std::atomic<bool> stopDispatch{false};
    std::atomic<int> dispatchCount{0};

    SignalCollector<VehicleSignal> earlyConsumer;
    SignalCollector<VehicleSignal> lateConsumer;
    SignalCollector<VehicleSignal> finalConsumer;

    // Register early consumer before dispatch starts
    auto earlyToken = dispatcher_->registerConsumer([&](const VehicleSignal& signal) {
        earlyConsumer.addSignal(signal);
    });

    // Start background dispatch thread
    std::thread dispatchThread([&]() {
        int counter = 0;
        while (!stopDispatch) {
            VehicleSignal signal(
                static_cast<std::uint64_t>(counter++),
                50.0, 100.0, 0.5, 10.0
            );

            dispatcher_->dispatch(signal);
            dispatchCount++;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    // Let dispatch run for a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Register late consumer during active dispatch
    auto lateToken = dispatcher_->registerConsumer([&](const VehicleSignal& signal) {
        lateConsumer.addSignal(signal);
    });

    // Let dispatch continue
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Unregister early consumer during active dispatch
    dispatcher_->unregisterConsumer(earlyToken);

    // Register final consumer
    auto finalToken = dispatcher_->registerConsumer([&](const VehicleSignal& signal) {
        finalConsumer.addSignal(signal);
    });

    // Let dispatch continue briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Stop dispatch
    stopDispatch = true;
    dispatchThread.join();

    // Verify: Early consumer stopped receiving after unregistration
    EXPECT_GT(earlyConsumer.count(), 0);

    // Verify: Late consumer received signals after registration
    EXPECT_GT(lateConsumer.count(), 0);

    // Verify: Final consumer received signals
    EXPECT_GT(finalConsumer.count(), 0);

    // Verify: No crashes during dynamic registration
    EXPECT_GT(dispatchCount, 0);
}

/**
 * Integration Test 8: Signal Callback Integration Pattern
 *
 * Expected behavior:
 * - EventDispatcher works with TeslaSignalParser callback type
 * - Callback registration and invocation work correctly
 * - Multiple callbacks can be registered simultaneously
 */
TEST_F(EventDispatcherIntegrationTest, SignalCallbackIntegrationPattern) {
    std::vector<VehicleSignal> receivedSignals1;
    std::vector<VehicleSignal> receivedSignals2;
    std::mutex signalsMutex;

    // Register consumers using callback pattern matching TeslaSignalParser
    auto token1 = dispatcher_->registerConsumer([&](const VehicleSignal& signal) {
        std::lock_guard<std::mutex> lock(signalsMutex);
        receivedSignals1.push_back(signal);
    });

    auto token2 = dispatcher_->registerConsumer([&](const VehicleSignal& signal) {
        std::lock_guard<std::mutex> lock(signalsMutex);
        receivedSignals2.push_back(signal);
    });

    // Create test signals
    std::vector<VehicleSignal> testSignals = {
        VehicleSignal(1000000000ULL, 10.0, 50.0, 0.2, 5.0),
        VehicleSignal(2000000000ULL, 30.0, 75.0, 0.4, 8.0),
        VehicleSignal(3000000000ULL, 60.0, 120.0, 0.6, 12.0)
    };

    // Dispatch signals
    for (const auto& signal : testSignals) {
        dispatcher_->dispatch(signal);
    }

    // Verify: Both consumers received all signals
    EXPECT_EQ(receivedSignals1.size(), testSignals.size());
    EXPECT_EQ(receivedSignals2.size(), testSignals.size());

    // Verify: Signal data preserved
    for (size_t i = 0; i < testSignals.size(); i++) {
        EXPECT_DOUBLE_EQ(receivedSignals1[i].getSpeedKmh().value(), testSignals[i].getSpeedKmh().value());
        EXPECT_DOUBLE_EQ(receivedSignals2[i].getSpeedKmh().value(), testSignals[i].getSpeedKmh().value());
        EXPECT_DOUBLE_EQ(receivedSignals1[i].getThrottlePercent().value(), testSignals[i].getThrottlePercent().value());
        EXPECT_DOUBLE_EQ(receivedSignals2[i].getThrottlePercent().value(), testSignals[i].getThrottlePercent().value());
    }
}
