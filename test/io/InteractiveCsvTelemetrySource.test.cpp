#include "vehicle-sim/io/InteractiveCsvTelemetrySource.h"
#include "vehicle-sim/cli/KeyboardThrottle.h"
#include "vehicle-sim/cli/IKeyboardInput.h"
#include "vehicle-sim/util/IClock.h"

#include <gtest/gtest.h>

#include <initializer_list>
#include <queue>

namespace {

class ScriptedKeyboard : public vehicle_sim::cli::IKeyboardInput {
public:
    explicit ScriptedKeyboard(std::queue<int> keys) : keys_{std::move(keys)} {}
    int getKey() override {
        if (keys_.empty()) return -1;
        int k = keys_.front();
        keys_.pop();
        return k;
    }
private:
    std::queue<int> keys_;
};

// A FakeClock whose sleepFor is a no-op (virtual time still advances via
// advance(), but the source only calls sleepFor, which must NOT sleep in tests).
class InstantClock : public vehicle_sim::util::IClock {
public:
    [[nodiscard]] time_point now() const override { return time_point{}; }
    void sleepFor(std::chrono::milliseconds) override { /* instant */ }
protected:
    [[nodiscard]] bool waitForImpl(std::condition_variable&,
                                   std::unique_lock<std::mutex>&,
                                   const std::function<bool()>&,
                                   time_point) const override { return false; }
};

std::unique_ptr<vehicle_sim::cli::KeyboardThrottle> throttleWith(
    std::initializer_list<int> keys) {
    std::queue<int> q;
    for (int k : keys) q.push(k);
    return std::make_unique<vehicle_sim::cli::KeyboardThrottle>(
        std::make_unique<ScriptedKeyboard>(std::move(q)));
}

} // namespace

TEST(InteractiveCsvTelemetrySourceTest, TimestampsAdvanceByInterval) {
    InstantClock clock;
    auto src = std::make_unique<vehicle_sim::io::InteractiveCsvTelemetrySource>(
        throttleWith({}), clock, "tesla", 20);

    ASSERT_TRUE(src->hasNext());
    auto r1 = src->next();
    EXPECT_EQ(r1.timestamp_ms, 20u);
    auto r2 = src->next();
    EXPECT_EQ(r2.timestamp_ms, 40u);
    auto r3 = src->next();
    EXPECT_EQ(r3.timestamp_ms, 60u);
    EXPECT_EQ(r3.vehicle_id, "tesla");
}

TEST(InteractiveCsvTelemetrySourceTest, ThrottleDrivesDerivedColumns) {
    InstantClock clock;
    auto src = std::make_unique<vehicle_sim::io::InteractiveCsvTelemetrySource>(
        throttleWith({'8'}), clock, "tesla", 20);   // 80% throttle

    auto r = src->next();
    EXPECT_DOUBLE_EQ(r.throttle_percent, 80.0);
    EXPECT_DOUBLE_EQ(r.speed_kmh, 80.0 * 4.0);
    EXPECT_DOUBLE_EQ(r.motor_hv_current, 80.0 * 2.0);
    EXPECT_DOUBLE_EQ(r.motor_torque_nm, 80.0 * 15.0);
    EXPECT_EQ(r.dbc_signal_count, 42);
}

TEST(InteractiveCsvTelemetrySourceTest, QuitEndsSource) {
    InstantClock clock;
    // First poll: throttle 5. Second poll: 'q' -> quit.
    auto src = std::make_unique<vehicle_sim::io::InteractiveCsvTelemetrySource>(
        throttleWith({'5', 'q'}), clock, "tesla", 20);

    auto r1 = src->next();
    EXPECT_FALSE(r1.vehicle_id.empty());   // still producing
    // After 'q' the source reports no next.
    EXPECT_FALSE(src->hasNext());
}
