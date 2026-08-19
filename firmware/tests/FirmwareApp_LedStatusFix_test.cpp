// FirmwareApp_LedStatusFix_test.cpp - RED→GREEN tests for the LED-status fixes
// on branch fix/led-status:
//   DEFECT 1: loop() ran update() BEFORE cycle(), so a just-adopted TCP client
//             was invisible to selectLedPattern() that tick (LED fell to
//             WIFI_SEARCHING / mostly dark instead of SOLID BLUE).
//   DEFECT 2: update() overwrote the LED pattern every tick, so onAuthFailed's
//             ERROR_AUTH_FAILURE was clobbered before the engine rendered it.
//
// These tests target the DRIVING layer (FirmwareApp + .ino ordering). They do
// NOT touch the StatusLED engine, StatusLEDRenderer, or the pattern table.
//
// PUBLIC CONTRACT only — uses the shared fixture + mocks from
// FirmwareApp_test_fixture.h.

#include "FirmwareApp_test_fixture.h"
#include "vanilla/StatusLED.h"            // firmware::StatusLED::Pattern
#include "vanilla/TcpServerManager.h"     // real TcpServerManager
#include "vanilla/TcpManagerConnectionSource.h"  // real IClientConnectionSource impl
#include "vanilla/ITcpServer.h"           // ITcpServer / ITcpServerClient / ITcpHostCallbacks

#include <memory>
#include <string>

using namespace esp32_firmware;
using namespace esp32_firmware::firmwareapp_test;

// ── Minimal fake ITcpServer/ITcpServerClient for the integration seam test ──
// Mirrors the gmock seams used in TcpServerManager_test.cpp, but here we want a
// CONCRETE fake (not NiceMock) so the real TcpServerManager drives a real
// TcpManagerConnectionSource -> FirmwareApp path (the seam that was previously
// only referenced in comments).
namespace {

class FakeTcpServerClient : public ITcpServerClient {
public:
    bool connected() const override { return connected_; }
    void stop() override { stopped_ = true; }
    void setTimeout(uint32_t) override {}
    int available() const override { return 0; }
    std::string readLine(char) override { return "AUTH vehicle-sim-2026"; }
    std::string readAvailableLine(char) override { return {}; }
    void println(const std::string&) override {}
    void flush() override {}
    void setNoDelay(bool) override {}
    std::string remoteIP() const override { return "10.0.0.99"; }

    bool connected_ = true;
    bool stopped_ = false;
};

class FakeTcpServer : public ITcpServer {
public:
    void begin() override {}
    void end() override {}
    std::unique_ptr<ITcpServerClient> accept() override {
        if (!pending_) return nullptr;
        return std::move(pending_);
    }
    void queue(std::unique_ptr<FakeTcpServerClient> c) { pending_ = std::move(c); }
    std::unique_ptr<FakeTcpServerClient> pending_;
};

class FakeTcpHostCallbacks : public ITcpHostCallbacks {
public:
    void handleTcpAtCommand(const std::string&) override {}
    void setMonitorActive(bool) override {}
    void resetDiscoveryBackoff() override {}
    int getWiFiState() const override { return 0; }
    void onClientConnected(const std::string&) override {}
    void onAuthFailed(const std::string&) override {}
    void onClientDisconnected(const std::string&, int) override {}
};

} // namespace

// ============================================================================
// TCP AUTH-token rejection — a client problem, NOT an ESP32 error.
// onAuthFailed must NOT drive the LED: the state model is LED-agnostic and
// update()'s selectLedPattern (state, clientConnected) is the only writer.
// ============================================================================

TEST_F(FirmwareAppTest, OnAuthFailed_DoesNotChangeLedPattern) {
    firmwareApp->init();
    ON_CALL(clientConnSourceMock, isClientConnected()).WillByDefault(Return(false));

    firmwareApp->update(1000);
    const int patternBefore = firmwareApp->getCurrentLedPattern();
    ASSERT_EQ(patternBefore, static_cast<int>(firmware::StatusLED::Pattern::WIFI_SEARCHING));

    // No setPattern() may be issued by the TCP auth rejection path.
    EXPECT_CALL(statusLedMock, setPattern(_)).Times(0);
    firmwareApp->onAuthFailed("1.2.3.4");

    EXPECT_EQ(firmwareApp->getCurrentLedPattern(), patternBefore);
}

// ============================================================================
// DEFECT 1 + ordering contract — a client adopted by cycle() BEFORE update()
// must be visible to selectLedPattern() via TcpManagerConnectionSource, yielding
// CLIENT_CONNECTED (SOLID BLUE), not WIFI_SEARCHING. This exercises the seam
// (TcpManagerConnectionSource -> TcpServerManager::hasClient()) that was
// previously only referenced in comments.
// ============================================================================

TEST_F(FirmwareAppTest, CycleBeforeUpdate_AdoptedClientYieldsClientConnected) {
    // Real manager + real connection source (the production wiring seam).
    FakeTcpServer server;
    FakeTcpHostCallbacks host;
    const std::string kToken = "vehicle-sim-2026";
    TcpServerManager manager(server, [&kToken]() -> const std::string& { return kToken; }, host);
    TcpManagerConnectionSource connSource(manager);

    // Build a FirmwareApp over the REAL connection source (not the mock).
    CanBridgeDeps canDeps{canDriverStub, tcpClientStub, serialStub};
    auto app = std::make_unique<FirmwareApp>(
        wifiMock, prefsMock, statusLedMock, serialTraceMock,
        wifiMock, udpMock, timeMock, sntpMock, timeNtpMock,
        testDeviceId, canDeps, &connSource, "baked-ssid", "baked-pass");
    app->init();

    // Before any client, the source reports disconnected → WIFI_SEARCHING.
    app->update(1000);
    ASSERT_EQ(connSource.isClientConnected(), false);
    ASSERT_EQ(app->getCurrentLedPattern(),
              static_cast<int>(firmware::StatusLED::Pattern::WIFI_SEARCHING));

    // A client arrives and authenticates: cycle() adopts it.
    server.queue(std::make_unique<FakeTcpServerClient>());
    manager.cycle(1010);
    ASSERT_EQ(connSource.isClientConnected(), true);

    // The NEXT update() (called AFTER cycle, mirroring the fixed loop order)
    // must now see the adopted client and show CLIENT_CONNECTED (SOLID BLUE).
    app->update(1020);
    EXPECT_EQ(connSource.isClientConnected(), true);
    EXPECT_EQ(app->getCurrentLedPattern(),
              static_cast<int>(firmware::StatusLED::Pattern::CLIENT_CONNECTED));
}
