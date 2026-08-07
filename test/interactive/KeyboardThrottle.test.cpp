#include "vehicle-sim/interactive/KeyboardThrottle.h"
#include "vehicle-sim/interactive/IKeyboardInput.h"

#include <gtest/gtest.h>

#include <initializer_list>
#include <queue>

namespace {

// Test double: a scripted keyboard that returns queued keys then -1.
class ScriptedKeyboard : public vehicle_sim::interactive::IKeyboardInput {
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

std::unique_ptr<vehicle_sim::interactive::KeyboardThrottle> throttleWith(
    std::initializer_list<int> keys) {
    std::queue<int> q;
    for (int k : keys) q.push(k);
    return std::make_unique<vehicle_sim::interactive::KeyboardThrottle>(
        std::make_unique<ScriptedKeyboard>(std::move(q)));
}

} // namespace

TEST(KeyboardThrottleTest, DigitMapsToThrottlePercent) {
    auto t = throttleWith({'5'});
    auto s = t->poll();
    EXPECT_DOUBLE_EQ(s.throttle_percent, 50.0);
    EXPECT_FALSE(s.quit);
}

TEST(KeyboardThrottleTest, ZeroDigitIsFullThrottle) {
    auto t = throttleWith({'0'});
    auto s = t->poll();
    EXPECT_DOUBLE_EQ(s.throttle_percent, 100.0);
}

TEST(KeyboardThrottleTest, QuitSetsFlag) {
    auto t = throttleWith({'3', 'q'});
    t->poll();              // '3' -> 30%
    auto s = t->poll();     // 'q' -> quit
    EXPECT_TRUE(s.quit);
}

TEST(KeyboardThrottleTest, ArrowUpIncrementsGear) {
    auto t = throttleWith({27, '[', 'A'});   // ESC [ A = Up
    auto s = t->poll();
    EXPECT_EQ(s.gear, 2);   // started at 1
}

TEST(KeyboardThrottleTest, ArrowDownDecrementsGearButNotBelowOne) {
    auto t = throttleWith({27, '[', 'B'});   // ESC [ B = Down
    auto s = t->poll();
    EXPECT_EQ(s.gear, 1);   // clamped
}

TEST(KeyboardThrottleTest, ArrowRightSteersPositive) {
    auto t = throttleWith({27, '[', 'C'});   // ESC [ C = Right
    auto s = t->poll();
    EXPECT_DOUBLE_EQ(s.steering_angle_deg, 5.0);
}

TEST(KeyboardThrottleTest, ArrowLeftSteersNegative) {
    auto t = throttleWith({27, '[', 'D'});   // ESC [ D = Left
    auto s = t->poll();
    EXPECT_DOUBLE_EQ(s.steering_angle_deg, -5.0);
}

TEST(KeyboardThrottleTest, BrakeLatchesOnBAndReleasesThrottle) {
    auto t = throttleWith({'7', 'b'});   // 70% throttle, then brake
    t->poll();                        // '7'
    auto s = t->poll();              // 'b'
    EXPECT_DOUBLE_EQ(s.brake_percent, 100.0);
    EXPECT_DOUBLE_EQ(s.throttle_percent, 0.0);
}

TEST(KeyboardThrottleTest, StatePersistsAcrossPolls) {
    // Combined sequence within one instance proves retention: gear up, then
    // throttle 40% — the gear set by the first poll must survive the second.
    auto t = throttleWith({27, '[', 'A', '4'});
    t->poll();                 // gear -> 2
    auto s = t->poll();        // throttle 40%
    EXPECT_EQ(s.gear, 2);      // retained
    EXPECT_DOUBLE_EQ(s.throttle_percent, 40.0);
}
