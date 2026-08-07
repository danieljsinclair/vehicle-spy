#include <gtest/gtest.h>
#include "firmware/vanilla/TcpServerManager.h"
#include "firmware/vanilla/ITcpServer.h"
#include "firmware/vanilla/WiFiManager.h"
#include "firmware/vanilla/WiFiReasonCodes.h"
#include "firmware/vanilla/DiscoveryManager.h"

#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace esp32_firmware;

namespace {

// ── Mock TCP client ────────────────────────────────────────────────────────────
// Records stop()/println/flush and lets the test script the wire state
// (connected, bytes available, the line returned by readLine, remote IP).
class MockTcpClient : public ITcpServerClient {
public:
    bool connected_ = true;
    bool stopped_ = false;
    std::string remoteIp_ = "10.0.0.5";
    // Queue of lines readLine() will return, one per call. An empty string in
    // the queue models a read that returned nothing (auth timeout / empty line).
    std::vector<std::string> readLines_;
    size_t readIdx_ = 0;
    std::vector<std::string> written_;
    bool flushed_ = false;
    uint32_t lastTimeout_ = 0;

    bool connected() const override { return connected_ && !stopped_; }
    void stop() override { stopped_ = true; connected_ = false; }
    void setTimeout(uint32_t ms) override { lastTimeout_ = ms; }
    int available() const override {
        // Model: if the next readLine would return non-empty, report a byte.
        if (readIdx_ < readLines_.size() && !readLines_[readIdx_].empty()) {
            return static_cast<int>(readLines_[readIdx_].size());
        }
        return 0;
    }
    std::string readLine(char) override {
        if (readIdx_ < readLines_.size()) {
            return readLines_[readIdx_++];
        }
        return {};  // no more queued input (timeout/empty)
    }
    void println(const std::string& line) override { written_.push_back(line); }
    void flush() override { flushed_ = true; }
    std::string remoteIP() const override {
        return (connected_ && !stopped_) ? remoteIp_ : std::string{};
    }
};

// ── Mock TCP server ─────────────────────────────────────────────────────────────
// Returns the next queued client on accept(); nullptr when the queue is empty.
class MockTcpServer : public ITcpServer {
public:
    std::vector<std::unique_ptr<MockTcpClient>> pending_;
    bool beginCalled_ = false;
    bool endCalled_ = false;
    size_t acceptCount_ = 0;

    void begin() override { beginCalled_ = true; }
    void end() override { endCalled_ = true; }
    std::unique_ptr<ITcpServerClient> accept() override {
        ++acceptCount_;
        if (pending_.empty()) {
            return nullptr;
        }
        auto c = std::move(pending_.front());
        pending_.erase(pending_.begin());
        return std::unique_ptr<ITcpServerClient>(c.release());
    }
    // Enqueue a freshly-connected client to be returned by the next accept().
    MockTcpClient* enqueueClient(const std::string& ip) {
        auto c = std::make_unique<MockTcpClient>();
        c->remoteIp_ = ip;
        auto* raw = c.get();
        pending_.push_back(std::move(c));
        return raw;
    }
};

// ── Mock host callbacks ────────────────────────────────────────────────────────
class MockHost : public ITcpHostCallbacks {
public:
    int connectedCalls_ = 0;
    int authFailedCalls_ = 0;
    int disconnectedCalls_ = 0;
    int monitorActiveCalls_ = 0;
    int backoffCalls_ = 0;
    bool monitorActive_ = false;
    std::vector<std::string> commands_;
    std::vector<std::string> connectedIps_;
    std::vector<std::string> disconnectedIps_;

    void handleTcpAtCommand(const std::string& cmd) override { commands_.push_back(cmd); }
    void setMonitorActive(bool active) override { monitorActive_ = active; ++monitorActiveCalls_; }
    void resetDiscoveryBackoff() override { ++backoffCalls_; }
    int getWiFiState() const override { return 0; }
    void onClientConnected(const std::string& ip) override { ++connectedCalls_; connectedIps_.push_back(ip); }
    void onAuthFailed(const std::string& ip) override { ++authFailedCalls_; (void)ip; }
    void onClientDisconnected(const std::string& ip, int) override {
        ++disconnectedCalls_; disconnectedIps_.push_back(ip);
    }
};

constexpr char kToken[] = "vehicle-sim-2026";

MockTcpClient* makeConnected(MockTcpServer& srv, const std::string& ip) {
    return srv.enqueueClient(ip);
}

// ═══════════════════════════════════════════════════════════════════════════════
// (1) PROVE THE TCP-REFUSAL CAUSE at the logic level
// ═══════════════════════════════════════════════════════════════════════════════

// Accept → AUTH → dispatch: a valid AUTH line authenticates and opens the stream.
TEST(TcpRefusalTest, ValidAuthConnectsAndActivatesMonitor) {
    MockTcpServer srv; MockHost host;
    TcpServerManager mgr(srv, kToken, host);
    auto* c = makeConnected(srv, "10.0.0.9");
    c->readLines_ = {"AUTH " + std::string(kToken)};

    mgr.cycle(0);

    ASSERT_TRUE(mgr.hasClient());
    EXPECT_EQ(mgr.getClientIp(), "10.0.0.9");
    EXPECT_EQ(c->written_.size(), 1u);
    EXPECT_EQ(c->written_[0], "OK");
    EXPECT_TRUE(c->flushed_);
    EXPECT_TRUE(host.monitorActive_);          // stream-on-connect
    EXPECT_EQ(host.connectedCalls_, 1);
}

// AUTH failure rejects and FREES the slot immediately (no stuck slot). The
// manager deletes the client on reject, so only observable host/manager state
// is asserted (not the destroyed mock's write buffer).
TEST(TcpRefusalTest, BadAuthRejectsAndFreesSlot) {
    MockTcpServer srv; MockHost host;
    TcpServerManager mgr(srv, kToken, host);
    auto* c = makeConnected(srv, "10.0.0.9");
    c->readLines_ = {"AUTH wrong-token"};

    mgr.cycle(0);

    EXPECT_FALSE(mgr.hasClient());                  // slot freed — no stuck slot
    EXPECT_EQ(host.authFailedCalls_, 1);            // rejection observed
    EXPECT_FALSE(host.monitorActive_);              // stream never opened
}

// CORE CLAIM: after a client CLEAN-disconnects, a NEW client can connect
// (last-wins, never a stuck slot → no TCP refusal).
TEST(TcpRefusalTest, CleanDisconnectThenNewClientConnects) {
    MockTcpServer srv; MockHost host;
    TcpServerManager mgr(srv, kToken, host);

    auto* a = makeConnected(srv, "10.0.0.1");
    a->readLines_ = {"AUTH " + std::string(kToken)};
    mgr.cycle(0);
    ASSERT_TRUE(mgr.hasClient());

    // Client A cleanly disconnects (FIN → connected() false).
    a->connected_ = false;
    mgr.cycle(0);  // disconnect cleanup fires
    EXPECT_FALSE(mgr.hasClient());
    EXPECT_EQ(host.disconnectedCalls_, 1);

    // New client B connects on the NEXT tick.
    auto* b = makeConnected(srv, "10.0.0.2");
    b->readLines_ = {"AUTH " + std::string(kToken)};
    mgr.cycle(0);
    ASSERT_TRUE(mgr.hasClient());
    EXPECT_EQ(mgr.getClientIp(), "10.0.0.2");  // last-wins, new IP adopted
    EXPECT_TRUE(host.monitorActive_);
}

// DIRTY/ABRUPT disconnect: the peer vanishes (RST-less) so the handle is still
// "connected" until the stack notices. A NEW accept() on a later tick must
// last-wins and evict the stale handle — NO refusal of the new client.
TEST(TcpRefusalTest, DirtyDisconnectThenNewAcceptEvictsStaleSlot) {
    MockTcpServer srv; MockHost host;
    TcpServerManager mgr(srv, kToken, host);

    auto* a = makeConnected(srv, "10.0.0.1");
    a->readLines_ = {"AUTH " + std::string(kToken)};
    mgr.cycle(0);
    ASSERT_TRUE(mgr.hasClient());

    // Simulate abrupt drop: the socket still REPORTS connected() (TCP keepalive
    // hasn't fired yet) but a brand-new client arrives. The manager must evict A
    // and adopt B (last-wins on every accept()).
    auto* b = makeConnected(srv, "10.0.0.2");
    b->readLines_ = {"AUTH " + std::string(kToken)};
    mgr.cycle(0);

    EXPECT_TRUE(a->stopped_);   // old client stopped (evicted)
    ASSERT_TRUE(mgr.hasClient());
    EXPECT_EQ(mgr.getClientIp(), "10.0.0.2");
    EXPECT_EQ(host.connectedCalls_, 2);
}

// Dirty disconnect detected later (connected() finally reports false): the slot
// is released on that tick — never permanently stuck.
TEST(TcpRefusalTest, StaleConnectedSlotReleasedWhenDropDetected) {
    MockTcpServer srv; MockHost host;
    TcpServerManager mgr(srv, kToken, host);

    auto* a = makeConnected(srv, "10.0.0.1");
    a->readLines_ = {"AUTH " + std::string(kToken)};
    mgr.cycle(0);
    ASSERT_TRUE(mgr.hasClient());

    // Still "connected" for a while (no new accept, no input).
    mgr.cycle(100);
    EXPECT_TRUE(mgr.hasClient());  // no drop detected yet — correct

    // Eventually the stack reports disconnected → slot released.
    a->connected_ = false;
    mgr.cycle(200);
    EXPECT_FALSE(mgr.hasClient());
    EXPECT_EQ(host.disconnectedCalls_, 1);
}

// Concurrent arrival while a client is live: accept() wins, prior is evicted.
TEST(TcpRefusalTest, NewAcceptWhileLiveClientPresentEvictsPrior) {
    MockTcpServer srv; MockHost host;
    TcpServerManager mgr(srv, kToken, host);

    auto* a = makeConnected(srv, "10.0.0.1");
    a->readLines_ = {"AUTH " + std::string(kToken)};
    mgr.cycle(0);
    ASSERT_TRUE(mgr.hasClient());

    // Second client arrives while A is still connected and active.
    auto* b = makeConnected(srv, "10.0.0.2");
    b->readLines_ = {"AUTH " + std::string(kToken)};
    mgr.cycle(0);

    EXPECT_TRUE(a->stopped_);   // prior evicted
    EXPECT_TRUE(mgr.hasClient());
    EXPECT_EQ(mgr.getClientIp(), "10.0.0.2");
}

// ALWAYS-ALLOW-CONNECT (resilient-reconnect req-4): the firmware NEVER refuses a
// connect or holds the slot "taken". A brand-new client that arrives while an
// existing (even authenticated) client is live is immediately adopted — the ESP32
// and app are order-independent and sticky. There is no code path that drops or
// queues the new arrival because a slot is "occupied"; the prior client is simply
// evicted (stop()'d) and the new one wins. This pins the contract explicitly so a
// future "refuse when busy" optimization cannot regress road-test reliability.
TEST(TcpRefusalTest, AlwaysAllowConnectNeverRefusesNewClient) {
    MockTcpServer srv; MockHost host;
    TcpServerManager mgr(srv, kToken, host);

    auto* a = makeConnected(srv, "10.0.0.1");
    a->readLines_ = {"AUTH " + std::string(kToken)};
    mgr.cycle(0);
    ASSERT_TRUE(mgr.hasClient());
    ASSERT_EQ(mgr.getClientIp(), "10.0.0.1");

    // App reconnects (new TCP session) while the old one is still logically held.
    // The manager must accept it, never refuse, and evict the stale handle.
    auto* b = makeConnected(srv, "10.0.0.1");  // same IP, new socket — order-independent reconnect
    b->readLines_ = {"AUTH " + std::string(kToken)};
    mgr.cycle(0);

    EXPECT_TRUE(a->stopped_);                          // stale handle freed
    EXPECT_TRUE(mgr.hasClient());                      // new client adopted — not refused
    EXPECT_EQ(mgr.getClientIp(), "10.0.0.1");
    EXPECT_EQ(host.connectedCalls_, 2);                // both connects observed (no drop lost)
    EXPECT_EQ(host.disconnectedCalls_, 0);             // prior eviction is silent, not a protocol drop
}

// Command dispatch on an authenticated client. Both lines are queued BEFORE the
// first cycle so readIdx_ advances naturally (auth consumes index 0, the
// command sits at index 1 and is read on the next cycle).
TEST(TcpRefusalTest, AuthenticatedClientCommandsAreDispatched) {
    MockTcpServer srv; MockHost host;
    TcpServerManager mgr(srv, kToken, host);

    auto* a = makeConnected(srv, "10.0.0.1");
    a->readLines_ = {"AUTH " + std::string(kToken), "ATMA"};
    mgr.cycle(0);  // auth (consumes index 0)

    mgr.cycle(1);  // command dispatch tick (reads index 1 = "ATMA")
    ASSERT_EQ(host.commands_.size(), 1u);
    EXPECT_EQ(host.commands_[0], "ATMA");
}

// ═══════════════════════════════════════════════════════════════════════════════
// (2a) HARDEN: slot cleanup on disconnect (last-wins, never stuck)
// ═══════════════════════════════════════════════════════════════════════════════

// Repeated connect/disconnect cycles must never accumulate slots or get stuck.
TEST(TcpRecoveryTest, ManyConnectDisconnectCyclesNeverStuck) {
    MockTcpServer srv; MockHost host;
    TcpServerManager mgr(srv, kToken, host);

    for (int i = 0; i < 20; ++i) {
        auto* c = makeConnected(srv, "10.0.0." + std::to_string(i % 250 + 1));
        c->readLines_ = {"AUTH " + std::string(kToken)};
        mgr.cycle(static_cast<uint32_t>(i));
        ASSERT_TRUE(mgr.hasClient()) << "cycle " << i;

        // Alternate clean and dirty drops.
        if (i % 2 == 0) {
            c->connected_ = false;        // clean FIN
        } else {
            c->stopped_ = true;           // abrupt (already gone)
            c->connected_ = false;
        }
        mgr.cycle(static_cast<uint32_t>(i) + 1);
        EXPECT_FALSE(mgr.hasClient()) << "slot stuck after cycle " << i;
    }
}

// stop() must release the adopted client and free the slot for a new client.
// Asserts only observable manager state — the mock is destroyed by stop().
TEST(TcpRecoveryTest, StopReleasesAdoptedClient) {
    MockTcpServer srv; MockHost host;
    TcpServerManager mgr(srv, kToken, host);
    auto* c = makeConnected(srv, "10.0.0.7");
    c->readLines_ = {"AUTH " + std::string(kToken)};
    mgr.cycle(0);
    ASSERT_TRUE(mgr.hasClient());

    mgr.stop();
    EXPECT_FALSE(mgr.hasClient());  // slot released

    // A new client can immediately connect — the slot was truly freed.
    auto* d = makeConnected(srv, "10.0.0.8");
    d->readLines_ = {"AUTH " + std::string(kToken)};
    mgr.cycle(1);
    ASSERT_TRUE(mgr.hasClient());
    EXPECT_EQ(mgr.getClientIp(), "10.0.0.8");
}

// ═══════════════════════════════════════════════════════════════════════════════
// (2b) HARDEN: TCP server re-listens on WiFi reconnect / IP change
//       (guards against silent TCP refusal after a same-IP brief blip)
// ═══════════════════════════════════════════════════════════════════════════════

class FakeWiFiReconnect : public IWiFi {
public:
    int mode = 0;
    int beginCalls = 0;
    int statusVal = 3;  // start CONNECTED
    std::string lastSsid, lastPass;
    std::string localIpVal = "192.168.1.50";

    void setMode(int m) override { mode = m; }
    void begin(const char* ssid, const char* pass) override {
        lastSsid = ssid; lastPass = pass; ++beginCalls;
    }
    void disconnect(bool, bool) override {}
    int status() const override { return statusVal; }
    std::string localIP() const override { return localIpVal; }
    std::string softAPIP() const override { return "192.168.4.1"; }
    void softAP(const char*, const char*) override { mode = 2; }
    void setHostname(const char*) override {}
    int getMode() const override { return mode; }
    std::string SSID() const override { return lastSsid; }
    const char* disconnectReasonName(int) const override { return ""; }
    void onEvent(std::function<void(int, WifiEventInfo*)>, int) override {}
};

class FakePrefs : public IPreferences {
public:
    std::string ssid = "net", pass = "pw";
    void begin(const char*, bool) override {}
    void end() override {}
    size_t getBytesLength(const char*) override { return 1; }
    std::string getString(const char*, const std::string&) override { return "x"; }
    size_t putString(const char*, const std::string&) override { return 1; }
    void clear() override { ssid.clear(); pass.clear(); }
};

class FakeSerialNoop2 : public ISerial {
public:
    void println(const char*) override {}
    __attribute__((format(printf, 2, 3))) void printf(const char*, ...) override {}
};

TEST(TcpReconnectTest, ReconnectWithSameIpAlwaysFlagsTcpRestart) {
    FakeWiFiReconnect wifi; FakePrefs prefs; FakeSerialNoop2 serial;
    WiFiManager mgr(wifi, prefs, serial, "baked", "bakedpw");
    mgr.init();
    wifi.statusVal = 3;       // WL_CONNECTED
    mgr.update(1);            // -> CONNECTED_STA
    ASSERT_EQ(mgr.getState(), WiFiState::State::WIFI_CONNECTED);
    mgr.update(2);            // record IP into lastConnectedIp

    // Drop, then reconnect with the SAME IP (brief blip).
    mgr.onDisconnected(WIFI_REASON_UNSPECIFIED);
    ASSERT_EQ(mgr.getState(), WiFiState::State::WIFI_CONNECTING);
    wifi.statusVal = 3;       // re-associated, same IP
    mgr.update(3);

    // HARDENING: regardless of same-IP, the TCP server MUST be flagged for a
    // re-listen so it cannot end up silently not-listening (a TCP refusal).
    EXPECT_EQ(mgr.getState(), WiFiState::State::WIFI_CONNECTED);
    EXPECT_TRUE(mgr.shouldRestartTcpServer());
}

TEST(TcpReconnectTest, ReconnectWithNewIpFlagsTcpRestart) {
    FakeWiFiReconnect wifi; FakePrefs prefs; FakeSerialNoop2 serial;
    WiFiManager mgr(wifi, prefs, serial, "baked", "bakedpw");
    mgr.init();
    wifi.statusVal = 3;
    mgr.update(1);
    mgr.update(2);

    mgr.onDisconnected(WIFI_REASON_UNSPECIFIED);
    wifi.localIpVal = "192.168.1.99";  // DHCP handed a new lease
    wifi.statusVal = 3;
    mgr.update(3);

    EXPECT_EQ(mgr.getState(), WiFiState::State::WIFI_CONNECTED);
    EXPECT_TRUE(mgr.shouldRestartTcpServer());
}

TEST(TcpReconnectTest, ShouldRestartTcpServerForReconnectReturnsTrueAtFirstConnect) {
    // First-ever connect (no lastConnectedIp) must re-listen.
    EXPECT_TRUE(shouldRestartTcpServerForReconnect("1.2.3.4", "", 0));
}

TEST(TcpReconnectTest, ShouldRestartTcpServerForReconnectAlwaysTrueHardened) {
    // HARDENING: the function now ALWAYS returns true — even same-IP brief blip
    // (outage below LONG_OUTAGE_MS) — so the listening socket is always rebound
    // on reconnect. This eliminates the silent-refusal mode where a same-IP
    // blip left the socket un-rebound.
    EXPECT_TRUE(shouldRestartTcpServerForReconnect("10.0.0.5", "10.0.0.5", 1000));
    EXPECT_TRUE(shouldRestartTcpServerForReconnect("10.0.0.5", "10.0.0.5", 0));
}

// ═══════════════════════════════════════════════════════════════════════════════
// (2c) HARDEN: discovery (UDP 3335) restart after WiFi reconnect
//       (guards against a dead discovery socket after a radio reset)
// ═══════════════════════════════════════════════════════════════════════════════

class FakeUdpRebind : public IUdp {
public:
    int beginCalls = 0;
    uint16_t lastPort = 0;
    std::string lastIp; uint16_t lastPortPkt = 0;
    std::vector<uint8_t> lastWritten;
    int endPacketCalls = 0;
    void begin(uint16_t port) override { ++beginCalls; lastPort = port; }
    int beginPacket(const std::string& ip, uint16_t port) override { lastIp = ip; lastPortPkt = port; return 1; }
    size_t write(const uint8_t* d, size_t l) override { lastWritten.assign(d, d + l); return l; }
    int endPacket() override { ++endPacketCalls; return 1; }
};

class FakeWiFiDisc : public IWiFiDiscovery {
public:
    int mode = 1; int statusVal = 3; std::string broadcastIp = "192.168.1.255";
    int getMode() const override { return mode; }
    int status() const override { return statusVal; }
    std::string broadcastIP() const override { return broadcastIp; }
};

class FakeTimeRebind : public ITime {
public:
    uint64_t ts = 1700000000; uint32_t m = 0;
    uint64_t getCurrentTimestamp() const override { return ts; }
    uint32_t millis() const override { return m; }
};

class FakeSignerNoop2 : public IDiscoverySigner {
public:
    bool isEnabled() const override { return false; }
    void signPacket(uint8_t*) override {}
};

constexpr std::array<uint8_t, 16> kDevId = {
    0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
    0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10};

TEST(DiscoveryRebindTest, InitRebindsUdpSocketForDiscovery) {
    FakeUdpRebind udp; FakeWiFiDisc wifi; FakeTimeRebind time; FakeSignerNoop2 signer;
    DiscoveryManager dm(udp, wifi, time, kDevId, signer);
    dm.init();  // udp_.begin(DISCOVERY_PORT) binds the listening socket.
    EXPECT_EQ(udp.beginCalls, 1);
    EXPECT_EQ(udp.lastPort, DiscoveryConfig::DISCOVERY_PORT);
}

TEST(DiscoveryRebindTest, ForceBroadcastAfterRebindSendsPacket) {
    FakeUdpRebind udp; FakeWiFiDisc wifi; FakeTimeRebind time; FakeSignerNoop2 signer;
    DiscoveryManager dm(udp, wifi, time, kDevId, signer);
    dm.init();          // bind socket
    udp.endPacketCalls = 0;
    dm.forceBroadcast(); // after a reconnect the socket must still send
    EXPECT_EQ(udp.endPacketCalls, 1);
    ASSERT_EQ(udp.lastWritten.size(), DiscoveryConfig::DISCOVERY_PACKET_SIZE);
    EXPECT_EQ(udp.lastWritten[0], 0x56);  // magic 'V'
}

// ═══════════════════════════════════════════════════════════════════════════════
// (3) RESILIENT RECONNECT — req-4: ALWAYS ALLOW CONNECT (last-wins, never refuse)
//   The TCP accept path must NEVER hold a taken slot against a reconnecting app.
//   Every incoming accept — even while a (possibly stale) client is still reported
//   connected — evicts the prior handle and adopts the new client. This is what
//   keeps the ESP32 sticky/order-independent: the app can (re)connect any time.
// ═══════════════════════════════════════════════════════════════════════════════

TEST(AlwaysAllowConnectTest, NewAcceptWhileStaleSlotConnectedEvictsAndAdopts) {
    // A client is connected and the stack still reports it connected (abrupt drop
    // not yet detected). A brand-new accept MUST be accepted (last-wins) — the
    // stale handle is evicted, the new client is authenticated and adopted. No
    // refusal, no held slot.
    MockTcpServer srv; MockHost host;
    TcpServerManager mgr(srv, kToken, host);

    auto* a = makeConnected(srv, "10.0.0.1");
    a->readLines_ = {"AUTH " + std::string(kToken)};
    mgr.cycle(0);
    ASSERT_TRUE(mgr.hasClient());
    ASSERT_EQ(mgr.getClientIp(), "10.0.0.1");

    // New client arrives; A is still "connected" (no drop detected yet).
    auto* b = makeConnected(srv, "10.0.0.2");
    b->readLines_ = {"AUTH " + std::string(kToken)};
    mgr.cycle(0);

    EXPECT_TRUE(a->stopped_);                       // stale slot evicted
    ASSERT_TRUE(mgr.hasClient());                   // new client adopted
    EXPECT_EQ(mgr.getClientIp(), "10.0.0.2");       // last-wins
    EXPECT_EQ(host.connectedCalls_, 2);             // never refused
}

TEST(AlwaysAllowConnectTest, RepeatedReconnectsAlwaysAccepted) {
    // Simulate the app dropping and reconnecting repeatedly: each new accept is
    // accepted (never refused) regardless of prior state — sticky/order-independent.
    MockTcpServer srv; MockHost host;
    TcpServerManager mgr(srv, kToken, host);

    for (int i = 0; i < 3; ++i) {
        auto* c = makeConnected(srv, "10.0.0." + std::to_string(10 + i));
        c->readLines_ = {"AUTH " + std::string(kToken)};
        mgr.cycle(0);
        ASSERT_TRUE(mgr.hasClient())
            << "reconnect #" << i << " must be accepted (req-4)";
        // Simulate a clean drop before the next reconnect.
        c->connected_ = false;
        mgr.cycle(0);
    }
    EXPECT_EQ(host.connectedCalls_, 3);  // every reconnect accepted
    EXPECT_EQ(host.disconnectedCalls_, 3);
}

} // namespace
