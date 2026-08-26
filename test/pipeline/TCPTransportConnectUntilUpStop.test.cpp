// TCPTransportConnectUntilUpStop.test.cpp — TDD spec for the stop-check in
// connectUntilUp(): requestStop() must abort the first-connect retry loop
// promptly, not only after the full budget is exhausted.
//
// CHARACTERISATION test: locks the externally-observable contract that a stop
// signal during the first-connect retries aborts the loop — proven by the
// number of connect attempts, not just by open() returning false.
//
// NOTE on FakeClock: sleepFor() ADVANCES virtual time, so the 30 s first-connect
// budget DOES exhaust on its own under the fake clock (~301 attempts). open()
// therefore returns false whether or not the stop check exists, which is why the
// attempt count — not the return value — is the load-bearing assertion here.
// (An earlier revision of this file asserted only the bool and silently passed
// with the production stop check deleted.)

#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/pipeline/StopToken.h"
#include "vehicle-sim/pipeline/FakeSocket.h"
#include "vehicle-sim/util/IClock.h"

#include <atomic>
#include <memory>
#include <thread>

using namespace vehicle_sim::pipeline;
namespace util = vehicle_sim::util;

static std::shared_ptr<StopToken> g_testStop = std::make_shared<StopToken>();

namespace {

// A socket that ALWAYS refuses to connect, counts the attempts, and trips the
// stop flag from inside the Nth connect() call.
//
// Why this exists: asserting only "open() returned false" cannot distinguish
// "the stop check aborted the retry loop" from "the 30 s first-connect budget
// exhausted". Under FakeClock, sleepFor() ADVANCES virtual time, so the budget
// really does exhaust on its own (~300 attempts at the 100 ms-sliced backoff)
// and open() returns false either way — a test that only checks the bool passes
// even with the stop check deleted (verified by mutation).
//
// Counting attempts makes the two causes observable: honouring stop stops at
// exactly `stopAtAttempt`, whereas ignoring it burns the whole budget. Tripping
// the flag from inside connect() also removes the old yield/thread race — this
// is single-threaded and fully deterministic.
class StopOnNthConnectSocket final : public ISocket {
public:
    StopOnNthConnectSocket(std::shared_ptr<StopToken> stop, int stopAtAttempt)
        : stop_(std::move(stop)), stopAtAttempt_(stopAtAttempt) {}

    int connect(const std::string& /*host*/, int /*port*/,
                const StopToken* /*stop*/) override {
        ++attempts_;
        if (attempts_ == stopAtAttempt_) {
            stop_->requestStop();
        }
        return -1;  // always refused: the peer never comes up
    }

    ssize_t recv(char* /*buf*/, size_t /*len*/) override { return -1; }
    int selectReadable(int /*timeoutUs*/) override { return 0; }
    void close() noexcept override {}
    bool setRecvTimeout(int /*ms*/) override { return true; }
    bool sendAll(std::string_view /*data*/) override { return true; }
    bool setNoDelay(bool /*enable*/) override { return true; }

    int attempts() const { return attempts_; }

private:
    std::shared_ptr<StopToken> stop_;
    int stopAtAttempt_;
    int attempts_ = 0;
};

} // namespace

// ── requestStop during first-connect retries ────────────────────────────────
//
// connectUntilUp() must ABORT the retry loop at the top-of-iteration stop check
// once the flag trips — it must not keep retrying until the 30 s first-connect
// budget exhausts. The socket refuses every connect and trips stop from INSIDE
// the 3rd connect() call, so:
//   * honouring stop  → the loop stops after attempt 3 (the next top-of-loop
//                        check returns false), open() == false;
//   * ignoring stop    → the loop burns the whole budget (hundreds of attempts)
//                        before returning false.
// Asserting the ATTEMPT COUNT (not merely the bool) is what makes this test
// able to fail: under FakeClock, sleepFor() advances virtual time so the budget
// really does exhaust on its own and open() returns false EITHER WAY — a
// bool-only assertion passes even with the stop check deleted (proven by
// mutation). Single-threaded and deterministic: no yield/join race.

TEST(TCPTransportConnectUntilUpStopTest, RequestStop_AbortsFirstConnectRetry) {
    g_testStop->reset();
    constexpr int kStopAtAttempt = 3;
    auto sock = std::make_shared<StopOnNthConnectSocket>(g_testStop, kStopAtAttempt);

    TCPTransport t(TransportEndpoint{"127.0.0.1", 3333, "raw"},
                   std::make_shared<StdOut>(), TcpReadTiming{1000, -1, 1}, g_testStop,
                   HuntResilienceConfig{}, std::make_shared<util::FakeClock>(),
                   sock);

    const bool opened = t.open();

    EXPECT_FALSE(opened)
        << "open() must return false when stop is requested during the "
           "first-connect retry loop.";
    // The load-bearing assertion: the loop aborted at the stop check right after
    // the attempt that tripped the flag, rather than exhausting the budget. If
    // the stop check is removed, this connect count balloons to the full budget
    // (~hundreds of attempts) and the test fails.
    EXPECT_EQ(sock->attempts(), kStopAtAttempt)
        << "connectUntilUp() must stop retrying at the top-of-loop stop check "
           "immediately after stop is set, not exhaust the 30 s budget "
           "(attempts=" << sock->attempts() << ").";

    g_testStop->reset();
}

// ── connectUntilUp respects stop at the top of each iteration ────────────────
//
// Even when the socket would eventually succeed, a stop flag set before the
// connect loop runs must win. connectUntilUp() checks stop_ at the TOP of every
// iteration, so the very first iteration returns false before any connect()
// attempt — the queued successful script is never consumed. This is fully
// deterministic: no thread race, no timing dependence. (FakeClock advances its
// virtual time on sleepFor, so the budget/exhaustion path is also exercised by
// other tests; here we pin the stop-precedence contract directly.)

TEST(TCPTransportConnectUntilUpStopTest, RequestStop_BeforeSuccessfulConnect_ReturnsFalse) {
    test::FakeSocket sock;
    // First two connects fail, third succeeds — models a peer that would come
    // up on the third attempt if given the chance.
    sock.enqueue("127.0.0.1", test::failConnect());
    sock.enqueue("127.0.0.1", test::failConnect());
    sock.enqueue("127.0.0.1", test::handshakeConnect());

    g_testStop->reset();
    TCPTransport t(TransportEndpoint{"127.0.0.1", 3333, "raw"},
                   std::make_shared<StdOut>(), TcpReadTiming{1000, -1, 1}, g_testStop,
                   HuntResilienceConfig{}, std::make_shared<util::FakeClock>(),
                   std::shared_ptr<ISocket>(&sock, [](ISocket*) {}));

    // Set stop BEFORE launching the worker so connectUntilUp() observes the flag
    // at the top of its first iteration and returns false before attempting any
    // connect. The queued successful script must NOT win. This removes the
    // thread-race that made the old "yield then requestStop" form flaky: the
    // worker (FakeClock → instant backoff) could complete all three connects
    // before the main thread's stop propagated, returning true.
    g_testStop->requestStop();

    std::atomic<bool> opened{false};
    std::thread th([&] { opened = t.open(); });
    th.join();

    // open() must return false — the stop signal takes precedence over the
    // queued successful connect script.
    EXPECT_FALSE(opened.load())
        << "open() must return false when stop is requested before the "
           "eventually-successful connect, not succeed on the queued script.";

    g_testStop->reset();
}

// ── stop during silent poll must set exhausted_ (restores OLD nextLine semantics) ──
//
// When the linked peer goes silent (selectReadable returns 0 forever) and
// requestStop() fires, nextLine() must set exhausted_ so isOpen() becomes
// false. The OLD nextLine() did this explicitly; the TcpReader extraction
// briefly lost it — this test pins the restored behaviour via the
// ReadFailureCallback extension (ReadFailureKind::StopRequested).

TEST(TCPTransportConnectUntilUpStopTest, RequestStop_DuringSilentPoll_SetsExhausted) {
    test::FakeSocket sock;
    // Successful handshake connect so open() returns true.
    sock.enqueue("127.0.0.1", test::handshakeConnect());

    g_testStop->reset();
    TCPTransport t(TransportEndpoint{"127.0.0.1", 3333, "raw"},
                   std::make_shared<StdOut>(), TcpReadTiming{1000, -1, 1}, g_testStop,
                   HuntResilienceConfig{}, std::make_shared<util::FakeClock>(),
                   std::shared_ptr<ISocket>(&sock, [](ISocket*) {}));

    ASSERT_TRUE(t.open()) << "open() must succeed against a handshake socket";

    // Put the socket into silent mode: selectReadable returns 0, recv returns -1.
    // This models a peer that connected but then stopped sending.
    sock.setSilent(true);

    // Signal stop before calling nextLine — the transport must treat a
    // stop-during-silent-poll as a give-up and mark itself exhausted.
    g_testStop->requestStop();
    auto result = t.nextLine();
    EXPECT_FALSE(result.has_value())
        << "nextLine() must return nullopt when stop fires during a silent poll";

    // The OLD semantics set exhausted_=true on this path; the restored
    // callback contract (ReadFailureKind::StopRequested) must do the same.
    EXPECT_FALSE(t.isOpen())
        << "isOpen() must be false after stop-during-silent-poll (exhausted_ set)";

    g_testStop->reset();
}
