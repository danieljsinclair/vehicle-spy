#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "vehicle-sim/domain/SimulationSignalSource.h"
#include "vehicle-sim/domain/IVehicleSimulator.h"
#include "vehicle-sim/domain/VehicleSignal.h"
#include "vehicle-sim/VehicleSim.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

using namespace vehicle_sim;
using namespace vehicle_sim::domain;
using ::testing::_;
using ::testing::Return;

namespace {

// gmock double for IVehicleSimulator. Verifies the adapter's worker-loop
// contract (which methods it calls, in what order, and that it propagates the
// simulator's getLatestSignal() return value) without a live simulation
// thread or wall-clock timing dependence.
class MockVehicleSimulator : public IVehicleSimulator {
public:
    MOCK_METHOD(bool, initialize, (const std::string&), (const, override));
    MOCK_METHOD(bool, start, (), (override));
    MOCK_METHOD(void, stop, (), (override));
    MOCK_METHOD(void, update, (), (override));
    MOCK_METHOD(VehicleSignal, getLatestSignal, (), (const, override));
};

// A signal carrying a recognizable live value, so a test can distinguish "the
// adapter polled the simulator" from "the adapter returned its stale default".
VehicleSignal makeLiveSignal(std::uint64_t ts, double speedKmh) {
    return VehicleSignal(VehicleSignal::Params{
        .timestampUtcMs = ts,
        .speedKmh = speedKmh,
    });
}

} // namespace

// ============================================================
// SimulationSignalSource — worker-loop contracts (mock-backed)
//
// These tests exercise REAL production code: SimulationSignalSource drives an
// IVehicleSimulator exactly as it does in production. The mock only stands in
// for the simulator so the worker loop can be verified deterministically.
// ============================================================

// Contract: start() initializes the simulator, starts it, and spawns a worker
// that repeatedly drives update() + getLatestSignal(). Asserts the lifecycle
// call sequence and that the polled value reaches latestSignal().
TEST(SimulationSignalSourceTest, StartDrivesInitializeStartAndPollsLatestSignal) {
    auto mock = std::make_unique<MockVehicleSimulator>();
    MockVehicleSimulator& sim = *mock;
    SimulationSignalSource source(std::move(mock), 1);

    const VehicleSignal live = makeLiveSignal(1234ULL, 42.0);
    EXPECT_CALL(sim, initialize(_)).WillOnce(Return(true));
    EXPECT_CALL(sim, start()).WillOnce(Return(true));
    // Worker calls update() + getLatestSignal() repeatedly; return the live
    // value on every poll so latestSignal() observes it.
    EXPECT_CALL(sim, update()).Times(::testing::AtLeast(1));
    EXPECT_CALL(sim, getLatestSignal()).Times(::testing::AtLeast(1)).WillRepeatedly(Return(live));

    source.start();

    // Poll for the adapter to have snapshotted the live value (poll interval 1ms).
    VehicleSignal observed{VehicleSignal::Params{.timestampUtcMs = 0}};
    for (int i = 0; i < 200; ++i) {
        observed = source.latestSignal();
        if (observed.getTimestampUtcMs() == live.getTimestampUtcMs()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    source.stop();

    EXPECT_EQ(observed.getTimestampUtcMs(), 1234ULL);
    ASSERT_TRUE(observed.getSpeedKmh().has_value());
    EXPECT_DOUBLE_EQ(observed.getSpeedKmh().value(), 42.0);
}

// Contract: stop() stops the simulator and joins the worker thread. After
// stop(), the simulator's stop() was invoked exactly once (no leak/duplicate),
// AND stop() returns within a bounded wall-clock budget. The slow update()
// forces the worker to be mid-tick during stop(): if join() were missing, the
// destructor/return path would either deadlock (joinable thread destroyed) or
// blow the 200ms budget — either fails the test. This pins "join works" rather
// than relying on destruction-abort UB to coincidentally pass.
TEST(SimulationSignalSourceTest, StopStopsSimulatorAndJoinsWorker) {
    auto mock = std::make_unique<MockVehicleSimulator>();
    MockVehicleSimulator& sim = *mock;
    SimulationSignalSource source(std::move(mock), 1);

    EXPECT_CALL(sim, initialize(_)).WillOnce(Return(true));
    EXPECT_CALL(sim, start()).WillOnce(Return(true));
    // Each update() sleeps so the worker is very likely mid-tick when stop()
    // is called below — making a missing join() observable.
    EXPECT_CALL(sim, update())
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Invoke([] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }));
    EXPECT_CALL(sim, getLatestSignal())
        .Times(::testing::AnyNumber())
        .WillRepeatedly(Return(makeLiveSignal(1ULL, 0.0)));
    EXPECT_CALL(sim, stop()).Times(1);

    source.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // stop() must join the worker and return promptly. Budget 200ms covers one
    // slow update() tick (50ms) plus scheduler slack; a missing join() would
    // either block here (std::thread destructor on a joinable thread calls
    // std::terminate) or far exceed it.
    const auto t0 = std::chrono::steady_clock::now();
    source.stop();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - t0).count();
    EXPECT_LE(elapsed, 200)
        << "stop() took " << elapsed << "ms — worker join missing or deadlocked";
}

// Contract: latestSignal() returns the default-constructed (timestamp == 0)
// signal before start() — the "not running" state. Pins the pre-start read.
TEST(SimulationSignalSourceTest, LatestSignalDefaultsToTimestampZeroBeforeStart) {
    auto mock = std::make_unique<MockVehicleSimulator>();
    SimulationSignalSource source(std::move(mock), 1);

    const VehicleSignal s = source.latestSignal();
    EXPECT_EQ(s.getTimestampUtcMs(), 0ULL);
}

// Contract: double-start idempotency — a second start() is a no-op. The
// simulator's initialize()/start() are each invoked exactly once even though
// start() is called twice. No second worker thread is spawned.
TEST(SimulationSignalSourceTest, DoubleStartIsIdempotent) {
    auto mock = std::make_unique<MockVehicleSimulator>();
    MockVehicleSimulator& sim = *mock;
    SimulationSignalSource source(std::move(mock), 1);

    EXPECT_CALL(sim, initialize(_)).Times(1).WillOnce(Return(true));
    EXPECT_CALL(sim, start()).Times(1).WillOnce(Return(true));
    EXPECT_CALL(sim, update()).Times(::testing::AnyNumber());
    EXPECT_CALL(sim, getLatestSignal())
        .Times(::testing::AnyNumber())
        .WillRepeatedly(Return(makeLiveSignal(9ULL, 0.0)));
    EXPECT_CALL(sim, stop()).Times(1);

    source.start();
    source.start();  // Second start must NOT re-initialize/re-start the sim.
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    source.stop();
}

// Contract: stop() before start() is a safe no-op (does not touch the
// simulator). Covers the !running_ early-return in stop().
TEST(SimulationSignalSourceTest, StopBeforeStartIsNoOp) {
    auto mock = std::make_unique<MockVehicleSimulator>();
    MockVehicleSimulator& sim = *mock;
    SimulationSignalSource source(std::move(mock), 1);

    // No simulator methods should be called at all.
    EXPECT_CALL(sim, initialize(_)).Times(0);
    EXPECT_CALL(sim, start()).Times(0);
    EXPECT_CALL(sim, stop()).Times(0);

    source.stop();  // No-op: was never started.
}

// ============================================================
// SimulationSignalSource — real VehicleSimulator integration
//
// Confirms the adapter actually polls a live VehicleSimulator and surfaces its
// changing values through latestSignal() (the behavior that was broken: the
// adapter previously returned a stale default forever).
// ============================================================

// Integration: the adapter polls a live VehicleSimulator and surfaces UPDATING
// data (this is the bug-fix verification — previously latestSignal() returned a
// stale default forever). Captures TWO snapshots separated by update ticks and
// asserts their timestamps are distinct, proving polling surfaces fresh reads,
// not one cached value. The range check is kept as a bound on the live value.
TEST(SimulationSignalSourceTest, PollsLiveVehicleSimulatorAndSurfacesChangingSpeed) {
    SimulationSignalSource source(std::make_unique<VehicleSimulator>(), 1);
    source.start();

    // Helper: spin until the adapter has snapshotted a signal with a timestamp
    // strictly newer than `since`, or time out.
    auto waitForNewerThan = [&source](std::uint64_t since) {
        VehicleSignal s{VehicleSignal::Params{.timestampUtcMs = 0}};
        for (int i = 0; i < 200; ++i) {
            s = source.latestSignal();
            if (s.getTimestampUtcMs() > since) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return s;
    };

    const VehicleSignal first = waitForNewerThan(0ULL);
    // Yield to the worker for a few poll ticks so a genuinely newer snapshot is
    // produced, then capture the second.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const VehicleSignal second = waitForNewerThan(first.getTimestampUtcMs());
    source.stop();

    ASSERT_GT(first.getTimestampUtcMs(), 0ULL)
        << "adapter never surfaced a live signal from VehicleSimulator";
    // The core assertion: a later read observed a DIFFERENT (newer) snapshot —
    // polling is surfacing updating data, not returning one stale value.
    EXPECT_NE(second.getTimestampUtcMs(), first.getTimestampUtcMs())
        << "second snapshot matched the first — adapter is not polling fresh data";

    // Bound: whatever the live speed is, it stays within its contract range.
    ASSERT_TRUE(second.getSpeedKmh().has_value());
    EXPECT_GE(second.getSpeedKmh().value(), VehicleSignal::SPEED_MIN);
    EXPECT_LE(second.getSpeedKmh().value(), VehicleSignal::SPEED_MAX);
}
