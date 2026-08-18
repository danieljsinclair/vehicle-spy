// TCPTransportDiagnostics.test.cpp — TDD spec for the failure-path diagnostics
// added to connectAndAuth(): every silent `return false` now emits a distinct
// `[tcp] <step> ...` err() message so a real failed connect NAMES the step that
// died (connect / recv-timeout / nodelay / AUTH send / AUTH recv / AUTH reject /
// ELM327 init / HELO) instead of being swallowed into a generic "connect not up".
//
// These are CHARACTERISATION tests: they lock the observable contract that each
// named failure path logs a message containing the failing step keyword. We use
// a scriptable FakeSocket (no real network) and a CapturingOutput so the exact
// err() calls are asserted.

#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/pipeline/ITransportOutput.h"
#include "vehicle-sim/pipeline/StopToken.h"
#include "vehicle-sim/pipeline/FakeSocket.h"
#include "vehicle-sim/util/IClock.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace vehicle_sim::pipeline;
namespace util = vehicle_sim::util;

namespace {

// Capturing sink reused from the ITransportOutput contract tests.
class CapturingOutput final : public ITransportOutput {
public:
    void out(const std::string& msg) override {
        std::lock_guard<std::mutex> lk(mu_);
        outLines_.push_back(msg);
    }
    void err(const std::string& msg) override {
        std::lock_guard<std::mutex> lk(mu_);
        errLines_.push_back(msg);
    }
    std::vector<std::string> outLines() const {
        std::lock_guard<std::mutex> lk(mu_);
        return outLines_;
    }
    std::vector<std::string> errLines() const {
        std::lock_guard<std::mutex> lk(mu_);
        return errLines_;
    }

private:
    mutable std::mutex mu_;
    std::vector<std::string> outLines_;
    std::vector<std::string> errLines_;
};

// Drive connectAndAuth() via the public open() surface (connectAndAuth is
// private) and return the transport so a test can assert the resulting err() lines.
std::unique_ptr<TCPTransport> makeTransport(test::FakeSocket& sock,
                                            std::shared_ptr<CapturingOutput> out,
                                            const std::string& protocol = "raw") {
    return std::make_unique<TCPTransport>(
        TransportEndpoint{"127.0.0.1", 3333, protocol},
        out, TcpReadTiming{1000, -1, 1}, std::make_shared<StopToken>(),
        HuntResilienceConfig{}, std::make_shared<util::FakeClock>(),
        std::shared_ptr<ISocket>(&sock, [](ISocket*) {}));
}

bool errContains(const std::vector<std::string>& lines, std::string_view needle) {
    for (const auto& l : lines) {
        if (l.find(needle) != std::string::npos) return true;
    }
    return false;
}

// Run open() to completion on a thread and return whether it succeeded. open()
// calls connectUntilUp() -> connectAndAuth(), surfacing the diagnostics.
bool runOpen(TCPTransport& t) {
    std::atomic<bool> opened{false};
    std::thread th([&] { opened = t.open(); });
    th.join();
    return opened.load();
}

// ── connect() failure names the step ──────────────────────────────────────
TEST(TCPTransportDiagnostics, ConnectFailure_NamesStep) {
    test::FakeSocket sock;
    sock.enqueue("127.0.0.1", test::failConnect());  // connect() returns -1
    auto out = std::make_shared<CapturingOutput>();
    auto t = makeTransport(sock, out);
    EXPECT_FALSE(runOpen(*t));
    EXPECT_TRUE(errContains(out->errLines(), "connect failed"))
        << "expected an err() naming the connect step; got: "
        << testing::PrintToString(out->errLines());
}

// ── AUTH recv EOF (peer closed before OK) names the step ───────────────────
TEST(TCPTransportDiagnostics, AuthRecvEof_NamesStep) {
    test::FakeSocket sock;
    // Connect ok, AUTH sent, but peer closes immediately (empty script => EOF).
    sock.enqueue("127.0.0.1", test::FakeConnectScript{true, {}});
    auto out = std::make_shared<CapturingOutput>();
    auto t = makeTransport(sock, out);
    EXPECT_FALSE(runOpen(*t));
    EXPECT_TRUE(errContains(out->errLines(), "AUTH recv failed"))
        << "expected an err() naming AUTH recv; got: "
        << testing::PrintToString(out->errLines());
}

// ── AUTH rejected (peer answers but not "OK") names the step + reply ───────
TEST(TCPTransportDiagnostics, AuthRejected_NamesStepAndReply) {
    test::FakeSocket sock;
    sock.enqueue("127.0.0.1", test::authRejectedConnect("ERROR unauthorized\r"));
    auto out = std::make_shared<CapturingOutput>();
    auto t = makeTransport(sock, out);
    EXPECT_FALSE(runOpen(*t));
    EXPECT_TRUE(errContains(out->errLines(), "AUTH rejected"))
        << "expected an err() naming AUTH rejected; got: "
        << testing::PrintToString(out->errLines());
    EXPECT_TRUE(errContains(out->errLines(), "ERROR unauthorized"))
        << "expected the peer reply to be surfaced; got: "
        << testing::PrintToString(out->errLines());
}

// ── HELO failure (ATI/ATHELO never ACKs) names the step ────────────────────
TEST(TCPTransportDiagnostics, HeloFailure_NamesStep) {
    test::FakeSocket sock;
    // AUTH ok, but no ATI/ATHELO response (EOF after OK).
    std::deque<std::string> chunks{"OK\r"};
    sock.enqueue("127.0.0.1", test::FakeConnectScript{true, std::move(chunks)});
    auto out = std::make_shared<CapturingOutput>();
    auto t = makeTransport(sock, out);
    EXPECT_FALSE(runOpen(*t));
    EXPECT_TRUE(errContains(out->errLines(), "HELO"))
        << "expected an err() naming HELO; got: "
        << testing::PrintToString(out->errLines());
}

} // namespace
