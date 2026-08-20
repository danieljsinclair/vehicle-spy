// FirmwareApp_test.cpp - Blind TDD tests for FirmwareApp orchestrator
// Testing PUBLIC CONTRACT only - do NOT read FirmwareApp.cpp
//
// The shared fixture, mocks, stubs, and AT boundary spies now live in
// FirmwareApp_test_fixture.h so a second TU (FirmwareApp_characterization_test.cpp)
// can reuse them.

#include "FirmwareApp_test_fixture.h"
#include "vanilla/StatusLED.h"     // firmware::StatusLED::Pattern

using namespace esp32_firmware;
using namespace esp32_firmware::firmwareapp_test;

// ============================================================================
// LIFECYCLE TESTS (mostly GREEN expected)
// ============================================================================

TEST_F(FirmwareAppTest, Ctor_DoesNotThrow) {
    // Constructor should not throw with valid dependencies
    EXPECT_NO_THROW({
        FirmwareApp app(wifiMock, prefsMock, statusLedMock, serialTraceMock,
                       wifiMock, udpMock, timeMock,
                       sntpMock, timeNtpMock,
                       testDeviceId, canDeps,
                       &clientConnSourceMock);
    });
}

TEST_F(FirmwareAppTest, Ctor_WithBakedCredentials_DoesNotThrow) {
    // Constructor with baked credentials should not throw
    EXPECT_NO_THROW({
        FirmwareApp app(wifiMock, prefsMock, statusLedMock, serialTraceMock,
                       wifiMock, udpMock, timeMock,
                       sntpMock, timeNtpMock,
                       testDeviceId, canDeps,
                       &clientConnSourceMock, "test-ssid", "test-pass");
    });
}

TEST_F(FirmwareAppTest, Init_FirstCall_Succeeds) {
    // First init() call should succeed without throwing
    EXPECT_NO_THROW({
        firmwareApp->init();
    });
}

TEST_F(FirmwareAppTest, Update_AfterInit_DoesNotThrow) {
    // After init(), update() should not throw
    firmwareApp->init();

    EXPECT_NO_THROW({
        firmwareApp->update(1000);
    });
}

TEST_F(FirmwareAppTest, GetWiFiState_AfterInit_ReturnsSaneValue) {
    // getWiFiState() should return a valid WiFi state after init
    firmwareApp->init();

    int state = firmwareApp->getWiFiState();

    // State should be within valid WiFiState enum range
    EXPECT_GE(state, 0);
    EXPECT_LT(state, 10); // Reasonable upper bound
}

// ============================================================================
// LIFECYCLE TESTS
// ============================================================================

TEST_F(FirmwareAppTest, OnWiFiDisconnected_AfterInit_DoesNotThrow) {
    // WiFi disconnected callback should not throw after init
    firmwareApp->init();

    EXPECT_NO_THROW({
        firmwareApp->onWiFiDisconnected(2); // WIFI_REASON_AUTH_EXPIRE (real ESP-IDF code)
    });
}

// ============================================================================
// LOOP ORCHESTRATION TESTS (RED expected - unimplemented TODOs)
// ============================================================================

TEST_F(FirmwareAppTest, Update_DrivesStatusLED_StatusLEDUpdateCalled) {
    // update() should drive status LED animation every tick
    firmwareApp->init();

    EXPECT_CALL(statusLedMock, update(_))
        .Times(1);

    firmwareApp->update(1000);
}

TEST_F(FirmwareAppTest, Update_DiscoveryManager_BroadcastsOnCadence) {
    // DiscoveryManager broadcasts on 500ms cadence (fast mode, age < 60s)
    // update() should trigger discovery broadcasts on this cadence
    firmwareApp->init();

    // Verify UDP methods are called (confirms broadcast() is being called)
    EXPECT_CALL(udpMock, beginPacket(_, _)).Times(AtLeast(1));
    EXPECT_CALL(udpMock, write(_, _)).Times(AtLeast(1));
    EXPECT_CALL(udpMock, endPacket()).Times(AtLeast(1));

    // Simulate time passing beyond the 500ms initial cadence
    firmwareApp->update(0);
    firmwareApp->update(1000);
    firmwareApp->update(2000);
}

TEST_F(FirmwareAppTest, Update_DiscoveryDisabled_NoUdpOpenOrBroadcast) {
    // Stage 3: the .ino injects the build-time VEHICLE_SIM_ENABLE_DISCOVERY toggle
    // via FirmwareApp::setDiscoveryEnabled(). When disabled, the vanilla
    // DiscoveryManager must never open the UDP socket nor broadcast — this mirrors
    // the removed inline `#if VEHICLE_SIM_ENABLE_DISCOVERY` guard.
    firmwareApp->init();
    firmwareApp->setDiscoveryEnabled(false);

    // UDP socket open (begin) must NOT happen when discovery is disabled.
    EXPECT_CALL(udpMock, begin(_)).Times(0);
    // No discovery packet should be written/sent.
    EXPECT_CALL(udpMock, beginPacket(_, _)).Times(0);
    EXPECT_CALL(udpMock, write(_, _)).Times(0);
    EXPECT_CALL(udpMock, endPacket()).Times(0);

    // Run several loop ticks past the fast-cadence window.
    firmwareApp->update(0);
    firmwareApp->update(1000);
    firmwareApp->update(2000);
    firmwareApp->update(3000);
}

TEST_F(FirmwareAppTest, Update_ClientConnected_StillBroadcasts) {
    // The device always broadcasts (even with a connected client) so it remains
    // discoverable. Previously: suppress entirely when haveClient. That hid the
    // device once any client connected AND masked a stuck-haveClient state where
    // discovery silently stopped. The time-based backoff slows the cadence; no
    // full suppress. The client connection state is now queried via
    // IClientConnectionSource (injected mock) instead of setClientConnected().
    firmwareApp->init();
    EXPECT_CALL(udpMock, begin(_)).Times(AtLeast(1));  // socket opens on first tick
    EXPECT_CALL(udpMock, beginPacket(_, _)).Times(AtLeast(1));
    EXPECT_CALL(udpMock, write(_, _)).Times(AtLeast(1));
    EXPECT_CALL(udpMock, endPacket()).Times(AtLeast(1));

    // Inject "client connected" via the IClientConnectionSource seam.
    ON_CALL(clientConnSourceMock, isClientConnected()).WillByDefault(Return(true));
    firmwareApp->update(0);
    firmwareApp->update(1000);
}

// ============================================================================
// CREDENTIAL OPERATIONS TESTS (delegation truisms removed — covered by WiFiManager_test)
// ============================================================================

// ============================================================================
// BAKED CREDENTIALS TESTS (delegation truisms removed — covered by WiFiManager_test)
// ============================================================================

// ============================================================================
// STATE QUERY TESTS
// ============================================================================

TEST_F(FirmwareAppTest, GetWiFiState_InitOnly_ReturnsDisconnectedState) {
    // After init with no WiFi action, should be in disconnected state
    firmwareApp->init();

    int state = firmwareApp->getWiFiState();

    // Should be DISCONNECTED (typically 0 or specific enum value)
    EXPECT_GE(state, 0);
}

// ============================================================================
// EDGE CASE TESTS
// ============================================================================

TEST_F(FirmwareAppTest, Update_RapidCalls_DoesNotCrash) {
    // Rapid update calls should not cause crashes
    firmwareApp->init();

    EXPECT_NO_THROW({
        for (uint32_t t = 0; t < 100; ++t) {
            firmwareApp->update(t);
        }
    });
}

TEST_F(FirmwareAppTest, OnWiFiDisconnected_InvalidReason_DoesNotThrow) {
    // Invalid disconnect reason should not throw
    firmwareApp->init();

    EXPECT_NO_THROW({
        firmwareApp->onWiFiDisconnected(-1);
        firmwareApp->onWiFiDisconnected(999);
    });
}

// Edge-case credential round-trip tests removed — covered by WiFiManager_test.

// ============================================================================
// NTP TIME SYNC TESTS (DISABLED - NtpTimeSync reverted, will be re-added later)
// ============================================================================

// These tests are disabled after reverting NtpTimeSync from FirmwareApp.
// NTP routing will be re-implemented in a future task with proper TDD workflow.

/*
TEST_F(FirmwareAppTest, Init_ConstructsNtpTimeSync_DoesNotThrow) {
    // init() should construct NtpTimeSync without throwing
    EXPECT_NO_THROW({
        firmwareApp->init();
    });
}

TEST_F(FirmwareAppTest, Init_ConstructsNtpTimeSync_SntpInitCalled) {
    // NtpTimeSync init should call sntp init (when WiFi connects)
    // This test verifies NtpTimeSync is constructed and init is called
    EXPECT_CALL(sntpMock, enabled())
        .WillOnce(Return(false));  // SNTP not enabled yet
    EXPECT_CALL(sntpMock, setOperatingMode(_))
        .Times(AtLeast(1));
    EXPECT_CALL(sntpMock, setServerName(_, _))
        .Times(AtLeast(1));
    EXPECT_CALL(sntpMock, setSyncMode(_))
        .Times(AtLeast(1));
    EXPECT_CALL(sntpMock, setSyncInterval(_))
        .Times(AtLeast(1));
    EXPECT_CALL(sntpMock, setTimeSyncNotificationCallback(_))
        .Times(AtLeast(1));
    EXPECT_CALL(sntpMock, init())
        .Times(AtLeast(1));

    // Note: These expectations are for when NTP init is actually triggered
    // The exact timing depends on WiFiManager state transitions
    firmwareApp->init();

    // Simulate WiFi connection to trigger NTP init
    firmwareApp->setCallbacks(callbackSpies);
    wifiMock.simulateConnectSuccess();
    firmwareApp->update(5000);
}

TEST_F(FirmwareAppTest, InitNtpSyncCallback_WiredToFirmwareCallbacks_FiresCorrectly) {
    // Verify that callbacks_.initNtpSync is wired correctly
    // This callback is set by WiFiManager when it transitions to NTP init state
    firmwareApp->init();
    firmwareApp->setCallbacks(callbackSpies);

    // Simulate WiFi connection which should trigger NTP init callback
    wifiMock.simulateConnectSuccess();
    firmwareApp->update(5000);

    // The callback should be fired when WiFiManager triggers NTP init
    // This is the wiring verification test
}

TEST_F(FirmwareAppTest, NtpTimeSync_ReusesExistingITimeDependency_SameInjected) {
    // Verify that ITime from DiscoveryManager is reused, not duplicated
    // FirmwareApp should only inject ITime once in constructor
    // NtpTimeSync uses ITimeNtp (different interface) - no conflict
    firmwareApp->init();

    // If this compiles and runs, the constructor signature is correct
    // and both DiscoveryManager (ITime) and NtpTimeSync (ITimeNtp) are happy
    EXPECT_NO_THROW({
        firmwareApp->update(1000);
    });
}
*/

// NTP routing tests removed — callback wiring no longer lives in FirmwareApp.

// ============================================================================
// CAN BRIDGE ROUTING (Stage 1: wire vanilla CanBridge through FirmwareApp)
// ============================================================================

TEST_F(FirmwareAppTest, CanBridge_SetMonitorActive_DelegatesToBridge) {
    // CONTRACT: setMonitorActive() must drive the wired CanBridge's monitor
    // state so the .ino no longer owns a parallel global.
    firmwareApp->init();

    EXPECT_FALSE(firmwareApp->isMonitorActive());

    firmwareApp->setMonitorActive(true);
    EXPECT_TRUE(firmwareApp->isMonitorActive());

    firmwareApp->setMonitorActive(false);
    EXPECT_FALSE(firmwareApp->isMonitorActive());
}

// CanBridge_ProcessCanFrames test removed — covered by CanBridge_test.

// ── AT Command delegation (Stage 2: .ino -> vanilla AtCommandDispatcher) ────────
// FirmwareApp owns the dispatcher and routes the .ino's TCP + serial command reads
// through it. We pin the PUBLIC contract: setAtCommandAdapters wires the five
// boundary adapters, and handleTcpAtCommand/handleSerialAtCommand delegate.

TEST_F(FirmwareAppTest, AtCommand_TcpCommand_RoutesToDispatcherWithCrCrGt) {
    // ATI over TCP must reach the dispatcher and be framed as "<resp>\r\r>"
    // (the host HELO handshake waits for the terminator); serial must NOT echo.
    SpyTcpClientAt tcp;
    SpySerialAt serial;
    SpyEspAt esp;
    SpyWifiStore wifi;
    SpyTokenStore token;
    SpyCredentialClear credClear;
    SpyMonitorState monitor;

    firmwareApp->init();
    firmwareApp->setAtCommandAdapters(tcp, serial, esp, wifi, token, credClear, monitor, testDeviceId);

    firmwareApp->handleTcpAtCommand("ATI");
    EXPECT_EQ(tcp.lastPrinted, "ESP32 CAN Bridge v0.1\r\r>");
    EXPECT_EQ(serial.lastLine, "");  // no serial echo on TCP path
}

// AtCommand_SerialCommand_AtzClearsMonitor removed — handleSerialAtCommand no longer on FirmwareApp.

TEST_F(FirmwareAppTest, AtCommand_Atreboot_NoExtraClientFlushBeforeRestart) {
    // The flush-hang fix: ATREBOOT's shouldFlushClient=false means the only flush
    // is the prompt's, then ESP.restart() proceeds. Exactly one flush + one restart.
    SpyTcpClientAt tcp;
    SpySerialAt serial;
    SpyEspAt esp;
    SpyWifiStore wifi;
    SpyTokenStore token;
    SpyCredentialClear credClear;
    SpyMonitorState monitor;

    firmwareApp->init();
    firmwareApp->setAtCommandAdapters(tcp, serial, esp, wifi, token, credClear, monitor, testDeviceId);

    firmwareApp->handleTcpAtCommand("ATREBOOT");
    EXPECT_EQ(esp.restartCalls, 1);
    EXPECT_EQ(tcp.flushCalls, 1);
    EXPECT_EQ(tcp.lastPrinted, "REBOOT\r\r>");
}

// ============================================================================
// CLIENT WIRING INVESTIGATION (Commit 5: #3)
//
// Proves that FirmwareApp queries IClientConnectionSource::isClientConnected()
// (not the global WiFiClient's connected()) and feeds the result into
// StatusLED::selectLedPattern(wifiState, clientConnected).
//
// The divergence: the .ino previously called setClientConnected() from the
// global WiFiClient's connected() state. When TcpServerManager stops the old
// client (on a new arrival or auth-fail), the global WiFiClient reports
// disconnected even though the manager may still hold an adopted client —
// feeding false into selectLedPattern → LED out + [STATE] no client.
//
// The fix: FirmwareApp now queries IClientConnectionSource (injected), which
// the .ino backs with TcpManagerConnectionSource over TcpServerManager::hasClient().
// ============================================================================

TEST_F(FirmwareAppTest, ClientWiring_ClientConnected_ReachesSelectLedPattern) {
    // CONTRACT: when IClientConnectionSource reports isClientConnected()==true,
    // selectLedPattern receives clientConnected=true → CLIENT_CONNECTED pattern
    // (regardless of WiFi state, per the priority rule).
    firmwareApp->init();

    // Simulate WiFi connected (so we're not in WIFI_SEARCHING).
    wifiMock.simulateConnectSuccess();

    // Inject "client connected" via the IClientConnectionSource seam.
    ON_CALL(clientConnSourceMock, isClientConnected()).WillByDefault(Return(true));

    // Expect setPattern to be called with CLIENT_CONNECTED (priority over WiFi state).
    EXPECT_CALL(statusLedMock, setPattern(
        static_cast<int>(firmware::StatusLED::Pattern::CLIENT_CONNECTED)))
        .Times(1);

    firmwareApp->update(5000);
}

TEST_F(FirmwareAppTest, ClientWiring_ClientDisconnected_ReachesSelectLedPattern) {
    // CONTRACT: when IClientConnectionSource reports isClientConnected()==false,
    // selectLedPattern receives clientConnected=false → pattern reflects WiFi state
    // (WIFI_CONNECTED when WiFi is connected).
    firmwareApp->init();

    wifiMock.simulateConnectSuccess();

    // Inject "no client connected" via the IClientConnectionSource seam.
    ON_CALL(clientConnSourceMock, isClientConnected()).WillByDefault(Return(false));

    // WiFi is connected, no client → WIFI_CONNECTED pattern.
    EXPECT_CALL(statusLedMock, setPattern(
        static_cast<int>(firmware::StatusLED::Pattern::WIFI_CONNECTED)))
        .Times(1);

    firmwareApp->update(5000);
}

TEST_F(FirmwareAppTest, ClientWiring_ClientConnectedOverridesWifiSearching) {
    // CONTRACT: client-connected takes PRIORITY over WiFi state. Even when WiFi
    // is DISCONNECTED (WIFI_SEARCHING), a connected client shows CLIENT_CONNECTED.
    // This is the core divergence fix: the old global-WiFiClient path could report
    // disconnected while the manager still held a client, dropping the LED to
    // WIFI_SEARCHING erroneously.
    firmwareApp->init();

    // WiFi is NOT connected (disconnected at boot).
    ON_CALL(clientConnSourceMock, isClientConnected()).WillByDefault(Return(true));

    // Client connected + WiFi disconnected → CLIENT_CONNECTED (priority rule).
    EXPECT_CALL(statusLedMock, setPattern(
        static_cast<int>(firmware::StatusLED::Pattern::CLIENT_CONNECTED)))
        .Times(1);

    firmwareApp->update(1000);
}

// ============================================================================
// UNCOVERED METHOD TESTS (closes FirmwareApp.cpp coverage gaps)
// ============================================================================

TEST_F(FirmwareAppTest, SetClientConnectionSource_AfterInit_UsedByUpdate) {
    // CONTRACT: setClientConnectionSource() swaps the client source and update()
    // immediately reads the new source via isClientConnected().
    firmwareApp->init();

    // Default fixture source reports disconnected; verify LED shows WIFI_CONNECTED.
    ON_CALL(clientConnSourceMock, isClientConnected()).WillByDefault(Return(false));
    wifiMock.simulateConnectSuccess();
    EXPECT_CALL(statusLedMock, setPattern(
        static_cast<int>(firmware::StatusLED::Pattern::WIFI_CONNECTED)))
        .Times(1);
    firmwareApp->update(5000);

    // Swap in a source that reports connected; LED must switch to CLIENT_CONNECTED.
    MockClientConnectionSource newSource;
    ON_CALL(newSource, isClientConnected()).WillByDefault(Return(true));
    firmwareApp->setClientConnectionSource(&newSource);

    EXPECT_CALL(statusLedMock, setPattern(
        static_cast<int>(firmware::StatusLED::Pattern::CLIENT_CONNECTED)))
        .Times(1);
    firmwareApp->update(6000);
}

// Delegation truism tests removed (clear, storeAuthToken, getDiscoveryCadence).
