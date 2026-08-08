// InteractiveRunContext.test.cpp - interactive mode emission + quit
//
// Business value: interactive mode must emit the same --stdout-csv schema as
// live/replay (so latency tests can't distinguish them) and must terminate
// cleanly on 'q'. We inject a scripted keyboard (no terminal) and a FakeClock
// (no real-time sleeps) so the test is deterministic.

#include "input/IKeyboardInput.h"
#include "vehicle-sim/cli/InteractiveRunContext.h"
#include "vehicle-sim/telemetry/CsvRowFormatter.h"
#include "vehicle-sim/util/IClock.h"

#include "telemetry/CsvShape.h"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <sstream>
#include <vector>

namespace {

class FakeKeyboard : public ::IKeyboardInput {
public:
    explicit FakeKeyboard(std::vector<int> keys) : keys_{std::move(keys)} {}
    // An embedded -1 is consumed and reported as "no key left this frame",
    // which ends the bridge's drain loop — that is how a script expresses a
    // frame boundary. Past the end, -1 is returned forever.
    int getKey() override {
        int key = -1;
        if (pos_ < keys_.size()) {
            key = keys_[pos_++];
        }
        return key;
    }

private:
    std::vector<int> keys_;
    std::size_t pos_{0};
};

// Factory that returns a fresh FakeKeyboard each call (matches the injected
// factory signature used by InteractiveRunContext::run).
std::function<std::unique_ptr<::IKeyboardInput>()> fakeKeyboardFactory(
    std::vector<int> keys) {
    return [keys]() -> std::unique_ptr<::IKeyboardInput> {
        return std::make_unique<FakeKeyboard>(keys);
    };
}

} // namespace

TEST(InteractiveRunContextTest, EmitsCanonicalHeaderAndQuitsOnQ) {
    // Press '4' (40% throttle) for a frame, then 'q' to quit.
    //
    // The -1 between them matters: it ends the bridge provider's per-frame
    // drain loop, so '4' and 'q' land in SEPARATE frames. The bridge's
    // processKeys() returns early once quit is seen, so a same-frame '4','q'
    // would quit before any throttle was applied and never emit a 40% row.
    vehicle_sim::util::FakeClock clock;
    std::vector<int> keys{'4', -1, 'q'};

    std::ostringstream out;
    const int rc = vehicle_sim::cli::InteractiveRunContext::run(
        "tesla", /*intervalMs=*/20, out, clock, fakeKeyboardFactory(keys));

    EXPECT_EQ(rc, 0);
    auto lines = vehicle_sim::test::splitLines(out.str());
    ASSERT_GE(lines.size(), 2u);
    // Header is the canonical --stdout-csv schema.
    EXPECT_EQ(lines[0], vehicle_sim::telemetry::csvHeaderLine());
    // At least one data row was emitted with the throttle we set.
    bool sawThrottle = false;
    for (size_t i = 1; i < lines.size(); ++i) {
        auto cells = vehicle_sim::test::cellsByColumn(lines[0], lines[i]);
        if (cells.at("throttle_percent") == "40.00") {
            sawThrottle = true;
        }
    }
    EXPECT_TRUE(sawThrottle);
}

TEST(InteractiveRunContextTest, ReturnsErrorWhenKeyboardFactoryYieldsNull) {
    vehicle_sim::util::FakeClock clock;
    std::function<std::unique_ptr<::IKeyboardInput>()> nullFactory =
        []() -> std::unique_ptr<::IKeyboardInput> { return nullptr; };

    std::ostringstream out;
    const int rc = vehicle_sim::cli::InteractiveRunContext::run(
        "tesla", 20, out, clock, nullFactory);
    EXPECT_EQ(rc, 1);
}
