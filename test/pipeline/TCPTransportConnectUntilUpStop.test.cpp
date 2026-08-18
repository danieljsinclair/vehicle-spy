// TCPTransportConnectUntilUpStop.test.cpp — TDD spec for the stop-check in
// connectUntilUp(): requestStop() must abort the first-connect retry loop
// promptly, not only after the full budget is exhausted.
//
// CHARACTERISATION test: locks the externally-observable contract that a
// stop signal during the first-connect retries causes open() to return false
// without hanging. Uses FakeSocket (always-refused connect) + FakeClock
// (instant backoff, budget never exhausts without stop).

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

// ── requestStop during first-connect retries ────────────────────────────────
//
// With a FakeSocket that always refuses and a FakeClock (instant backoff,
// virtual time stuck at epoch so the CONNECT_FIRST_BUDGET_MS check never
// fires), connectUntilUp() would spin forever unless the stop_ flag is
// checked. This test asserts the stop check is honoured: requestStop() causes
// open() to return false promptly.

TEST(TCPTransportConnectUntilUpStopTest, RequestStop_AbortsFirstConnectRetry) {
    test::FakeSocket sock;
    // connect() always returns -1 — models a peer that never comes up.
    // Without a stop check, the FakeClock backoff would loop forever
    // (virtual time never advances, so the 30 s budget never exhausts).
    sock.enqueue("127.0.0.1", test::failConnect());

    g_testStop->reset();
    TCPTransport t(TransportEndpoint{"127.0.0.1", 3333, "raw"},
                   std::make_shared<StdOut>(), TcpReadTiming{1000, -1, 1}, g_testStop,
                   HuntResilienceConfig{}, std::make_shared<util::FakeClock>(),
                   std::shared_ptr<ISocket>(&sock, [](ISocket*) {}));

    std::atomic<bool> opened{false};
    std::thread th([&] { opened = t.open(); });

    // Allow the thread to enter the retry loop (FakeSocket+FakeClock loop is
    // microsecond-fast, so a brief yield is sufficient).
    std::this_thread::yield();

    // Signal stop — the transport must abort the retry loop promptly.
    g_testStop->requestStop();

    // join() must not hang: if the stop check is missing, the loop spins
    // forever (FakeClock never advances the budget).
    th.join();

    // open() must return false (not true, not hang).
    EXPECT_FALSE(opened.load())
        << "open() must return false when requestStop() fires during the "
           "first-connect retry loop, not hang or eventually succeed.";

    g_testStop->reset();
}

// ── connectUntilUp respects stop at the top of each iteration ────────────────
//
// Even when the socket eventually succeeds, a stop flag set before the
// successful connect must win.
//
// NOTE: there is a theoretical race between the stop flag and the queued
// success script. In production the connect() call runs on the real network
// stack and a stop-flagged thread may observe either the stop or the success
// first. Under FakeClock the backoff loop is instant (virtual time never
// advances), so connectUntilUp() spins at full speed — stop always wins
// because the yield/join window gives the stop flag time to propagate before
// the next iteration's stop check. No timing-dependent flakiness.

TEST(TCPTransportConnectUntilUpStopTest, RequestStop_BeforeSuccessfulConnect_ReturnsFalse) {
    test::FakeSocket sock;
    // First two connects fail, third succeeds.
    sock.enqueue("127.0.0.1", test::failConnect());
    sock.enqueue("127.0.0.1", test::failConnect());
    sock.enqueue("127.0.0.1", test::handshakeConnect());

    g_testStop->reset();
    TCPTransport t(TransportEndpoint{"127.0.0.1", 3333, "raw"},
                   std::make_shared<StdOut>(), TcpReadTiming{1000, -1, 1}, g_testStop,
                   HuntResilienceConfig{}, std::make_shared<util::FakeClock>(),
                   std::shared_ptr<ISocket>(&sock, [](ISocket*) {}));

    std::atomic<bool> opened{false};
    std::thread th([&] { opened = t.open(); });

    // Allow the thread to enter the retry loop.
    std::this_thread::yield();

    // Signal stop before the third (eventually-successful) connect fires.
    g_testStop->requestStop();
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
