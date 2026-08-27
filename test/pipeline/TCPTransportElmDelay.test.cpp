// TCPTransportElmDelay.test.cpp — TDD spec for the perCommandDelayMs override
// path: when TcpReadTiming::atInitDelayMs >= 0, every ELM327 AT-init command
// must sleep for that override value, ignoring each command's own delayMs.
//
// CHARACTERISATION tests: pin the override contract using a RecordingClock
// (captures sleepFor durations) and a handshake script that includes the ELM
// AT-init sequence. Tests run on the "elm327" protocol path.

#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/pipeline/StopToken.h"
#include "vehicle-sim/pipeline/FakeSocket.h"
#include "vehicle-sim/util/IClock.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace vehicle_sim::pipeline;
namespace util = vehicle_sim::util;

// ── Recording clock: captures every sleepFor duration ──────────────────────

class RecordingClock : public util::IClock {
public:
    using time_point = util::IClock::time_point;
    using duration = util::IClock::duration;

    RecordingClock() : now_(time_point::min()) {}

    [[nodiscard]] time_point now() const override { return now_; }

    void sleepFor(std::chrono::milliseconds d) override {
        sleeps_.push_back(d);
        // Advance virtual time so the budget checks in connectUntilUp behave
        // realistically (the recorded durations are the assertion target; the
        // time advance is a side-effect that keeps the retry loop bounded).
        now_ += d;
    }

    const std::vector<std::chrono::milliseconds>& sleeps() const { return sleeps_; }

protected:
    [[nodiscard]] bool waitForImpl(
        std::condition_variable& /*cv*/,
        std::unique_lock<std::mutex>& /*lock*/,
        const std::function<bool()>& /*pred*/,
        time_point /*deadline*/) const override {
        return false; // not used in these tests
    }

private:
    time_point now_;
    std::vector<std::chrono::milliseconds> sleeps_;
};

// ── Helpers ────────────────────────────────────────────────────────────────

static std::shared_ptr<StopToken> g_testStop = std::make_shared<StopToken>();

std::unique_ptr<TCPTransport> makeElmTransport(test::FakeSocket& sock,
                                                std::shared_ptr<RecordingClock> clock,
                                                int atInitDelayMs) {
    return std::make_unique<TCPTransport>(
        TransportEndpoint{"127.0.0.1", 3333, "elm327"},
        std::make_shared<StdOut>(),
        TcpReadTiming{1000, atInitDelayMs, 1},
        g_testStop,
        HuntResilienceConfig{},
        clock,
        std::shared_ptr<ISocket>(&sock, [](ISocket*) {}));
}

bool runOpen(TCPTransport& t) {
    std::atomic<bool> opened{false};
    std::thread th([&] { opened = t.open(); });
    th.join();
    return opened.load();
}

// ── Tests ──────────────────────────────────────────────────────────────────

// (gap b) When atInitDelayMs >= 0 the override wins over every command's
// own delayMs: all 5 ELM327 AT-init sleeps must equal the override, not the
// per-command defaults (which vary: ATZ=1000ms, ATE0/ATSP6/ATH1/ATMA=50ms).

TEST(TCPTransportElmDelayTest, AtInitDelayMsOverride_AllCommandsUseOverride) {
    test::FakeSocket sock;
    sock.enqueue("127.0.0.1", test::elmHandshakeConnect());

    auto clock = std::make_shared<RecordingClock>();
    g_testStop->reset();
    auto t = makeElmTransport(sock, clock, 200); // override: 200 ms for every command
    ASSERT_TRUE(runOpen(*t));

    // The 5 ELM327 AT-init commands (ATZ, ATE0, ATSP6, ATH1, ATMA) each
    // paced via perCommandDelayMs — with override=200 every one must be 200.
    // sendHeloAndParseAck adds one extra clock_->sleepFor(50ms) after ATI,
    // so the total recorded sleeps are 6.
    const auto& sleeps = clock->sleeps();
    ASSERT_EQ(sleeps.size(), 6u)
        << "expected 5 ELM327 AT-init + 1 post-ATI sleep calls; got: "
        << testing::PrintToString(sleeps);

    for (std::size_t i = 0; i < 5u; ++i) {
        EXPECT_EQ(sleeps[i], std::chrono::milliseconds(200))
            << "AT-init command " << i << ": override=200ms must win over "
               "the command's own delayMs";
    }
    // The 6th sleep is the 50 ms post-ATI pacing in sendHeloAndParseAck.
    EXPECT_EQ(sleeps[5], std::chrono::milliseconds(50))
        << "post-ATI pacing must remain 50 ms regardless of the ELM327 override";

    g_testStop->requestStop();
    g_testStop->reset();
}

// When atInitDelayMs == -1 (the production default), each command uses its
// own delayMs (falling back to DEFAULT_PER_COMMAND_DELAY_MS=50 for any 0).
// The elmHandshakeConnect scripted responses include the 5 AT-init OKs; the
// first command is ATZ (delay 1000 ms from ELM327Transport), the rest are
// 50 ms. Verify the mixed durations appear in the recording.

TEST(TCPTransportElmDelayTest, AtInitDelayMsUnset_CommandsUseOwnDelay) {
    test::FakeSocket sock;
    sock.enqueue("127.0.0.1", test::elmHandshakeConnect());

    auto clock = std::make_shared<RecordingClock>();
    g_testStop->reset();
    auto t = makeElmTransport(sock, clock, -1); // production default: no override
    ASSERT_TRUE(runOpen(*t));

    const auto& sleeps = clock->sleeps();
    ASSERT_EQ(sleeps.size(), 6u);

    // ATZ is the first command and carries the longest delay (>= 500 ms).
    EXPECT_GE(sleeps[0].count(), 500)
        << "ATZ (first ELM327 command) must use its own long delay when no override is set";
    // Remaining four ELM327 commands use their own shorter delays (50 ms each).
    for (std::size_t i = 1; i < 5u; ++i) {
        EXPECT_EQ(sleeps[i], std::chrono::milliseconds(50))
            << "AT-init command " << i
            << " must use its own 50 ms delay when no override is set";
    }
    // The 6th sleep is the 50 ms post-ATI pacing in sendHeloAndParseAck.
    EXPECT_EQ(sleeps[5], std::chrono::milliseconds(50))
        << "post-ATI pacing must remain 50 ms regardless of the ELM327 override";

    g_testStop->requestStop();
    g_testStop->reset();
}

// atInitDelayMs == 0 is a valid override (zero pacing). All 5 sleeps must
// be 0 ms, not the command's own delayMs.

TEST(TCPTransportElmDelayTest, AtInitDelayMsZero_AllCommandsSleepZero) {
    test::FakeSocket sock;
    sock.enqueue("127.0.0.1", test::elmHandshakeConnect());

    auto clock = std::make_shared<RecordingClock>();
    g_testStop->reset();
    auto t = makeElmTransport(sock, clock, 0); // zero override: no pacing at all
    ASSERT_TRUE(runOpen(*t));

    const auto& sleeps = clock->sleeps();
    ASSERT_EQ(sleeps.size(), 6u);
    for (std::size_t i = 0; i < 5u; ++i) {
        EXPECT_EQ(sleeps[i], std::chrono::milliseconds(0))
            << "AT-init command " << i
            << ": override=0 must win, sleep must be 0 ms";
    }
    // The 6th sleep is the 50 ms post-ATI pacing in sendHeloAndParseAck.
    EXPECT_EQ(sleeps[5], std::chrono::milliseconds(50))
        << "post-ATI pacing must remain 50 ms regardless of the ELM327 override";

    g_testStop->requestStop();
    g_testStop->reset();
}
