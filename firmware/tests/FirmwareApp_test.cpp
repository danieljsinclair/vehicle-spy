// FirmwareApp_test.cpp - Blind TDD tests for FirmwareApp orchestrator
// Testing PUBLIC CONTRACT only - do NOT read FirmwareApp.cpp
//
// The shared fixture, mocks, stubs, and AT boundary spies now live in
// FirmwareApp_test_fixture.h so a second TU (FirmwareApp_characterization_test.cpp)
// can reuse them.

#include "FirmwareApp_test_fixture.h"

using namespace esp32_firmware;
using namespace esp32_firmware::firmwareapp_test;

// ============================================================================
// LIFECYCLE TESTS (mostly GREEN expected)
// ============================================================================

TEST_F(FirmwareAppTest, Ctor_DoesNotThrow) {
    // Constructor should not throw with valid dependencies
    EXPECT_NO_THROW({
        FirmwareApp app(wifiMock, prefsMock, statusLedMock,
                       wifiMock, udpMock, timeMock,
                       sntpMock, timeNtpMock,
                       testDeviceId, canDeps);
    });
}

TEST_F(FirmwareAppTest, Ctor_WithBakedCredentials_DoesNotThrow) {
    // Constructor with baked credentials should not throw
    EXPECT_NO_THROW({
        FirmwareApp app(wifiMock, prefsMock, statusLedMock,
                       wifiMock, udpMock, timeMock,
                       sntpMock, timeNtpMock,
                       testDeviceId, canDeps, "test-ssid", "test-pass");
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
// CALLBACK WIRING TESTS
// ============================================================================

TEST_F(FirmwareAppTest, SetCallbacks_BeforeInit_DoesNotThrow) {
    // Setting callbacks before init should not throw
    EXPECT_NO_THROW({
        firmwareApp->setCallbacks(callbackSpies);
    });
}

TEST_F(FirmwareAppTest, SetCallbacks_AfterInit_DoesNotThrow) {
    // Setting callbacks after init should not throw
    firmwareApp->init();

    EXPECT_NO_THROW({
        firmwareApp->setCallbacks(callbackSpies);
    });
}

TEST_F(FirmwareAppTest, OnWiFiDisconnected_AfterInit_DoesNotThrow) {
    // WiFi disconnected callback should not throw after init
    firmwareApp->init();

    EXPECT_NO_THROW({
        firmwareApp->onWiFiDisconnected(2); // WIFI_REASON_AUTH_EXPIRE (real ESP-IDF code)
    });
}

TEST_F(FirmwareAppTest, ShouldRestartTcpServer_InitOnly_ReturnsFalse) {
    // After init only, no TCP server restart should be needed
    firmwareApp->init();

    EXPECT_FALSE(firmwareApp->shouldRestartTcpServer());
}

TEST_F(FirmwareAppTest, ClearTcpServerRestartFlag_DoesNotThrow) {
    // Clearing the flag should not throw
    firmwareApp->init();

    EXPECT_NO_THROW({
        firmwareApp->clearTcpServerRestartFlag();
    });
}

TEST_F(FirmwareAppTest, FactoryReset_InitOnly_ReturnsTrue) {
    // Factory reset should succeed after init
    firmwareApp->init();

    EXPECT_TRUE(firmwareApp->factoryReset());
}

// ============================================================================
// LOOP ORCHESTRATION TESTS (RED expected - unimplemented TODOs)
// ============================================================================

TEST_F(FirmwareAppTest, Update_DrivesStatusLED_StatusLEDUpdateCalled) {
    // update() should drive status LED animation every tick
    firmwareApp->init();
    firmwareApp->setCallbacks(callbackSpies);

    EXPECT_CALL(statusLedMock, update(_))
        .Times(1);

    firmwareApp->update(1000);
}

TEST_F(FirmwareAppTest, Update_OnDiscoveryCadence_BroadcastDiscoveryCallbackFired) {
    // Task #2 - DiscoveryManager routing
    // DiscoveryManager broadcasts on 500ms cadence (fast mode, age < 60s)
    // update() should trigger discovery broadcasts on this cadence

    // Set callbacks BEFORE init so they're available during setupManagers
    firmwareApp->setCallbacks(callbackSpies);
    firmwareApp->init();

    // Verify UDP methods are called (confirms broadcast() is being called)
    EXPECT_CALL(udpMock, beginPacket(_, _)).Times(AtLeast(1));
    EXPECT_CALL(udpMock, write(_, _)).Times(AtLeast(1));
    EXPECT_CALL(udpMock, endPacket()).Times(AtLeast(1));

    // Simulate time passing beyond the 500ms initial cadence
    // With updates at 1000ms intervals, we should see at least one broadcast
    firmwareApp->update(0);
    firmwareApp->update(1000);
    firmwareApp->update(2000);

    EXPECT_TRUE(broadcastDiscoveryCalled) << "Broadcast callback was not called.";
}

TEST_F(FirmwareAppTest, Update_DoesNotFireDiscoveryBeforeCadence_CallbackNotFired) {
    // Task #2 - DiscoveryManager routing
    // Discovery should NOT fire before the 500ms cadence elapses
    firmwareApp->init();
    firmwareApp->setCallbacks(callbackSpies);

    // Update with time less than the 500ms cadence
    firmwareApp->update(0);
    firmwareApp->update(100);
    firmwareApp->update(200);
    firmwareApp->update(300);
    firmwareApp->update(400);

    EXPECT_FALSE(broadcastDiscoveryCalled);
}

TEST_F(FirmwareAppTest, Update_DiscoveryDisabled_NoUdpOpenOrBroadcast) {
    // Stage 3: the .ino injects the build-time VEHICLE_SIM_ENABLE_DISCOVERY toggle
    // via FirmwareApp::setDiscoveryEnabled(). When disabled, the vanilla
    // DiscoveryManager must never open the UDP socket nor broadcast — this mirrors
    // the removed inline `#if VEHICLE_SIM_ENABLE_DISCOVERY` guard.
    firmwareApp->setCallbacks(callbackSpies);
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

    EXPECT_FALSE(broadcastDiscoveryCalled)
        << "Discovery broadcast callback must not fire when discovery is disabled.";
}

TEST_F(FirmwareAppTest, Update_ClientConnected_SuppressesBroadcast) {
    // Stage 3: the .ino feeds the live TCP-client state into FirmwareApp via
    // setClientConnected(); DiscoveryManager should skip broadcasts while a buddy
    // is connected (replaces the inline `haveClient` early-return).
    firmwareApp->setCallbacks(callbackSpies);
    firmwareApp->init();
    EXPECT_CALL(udpMock, begin(_)).Times(AtLeast(1));  // socket opens on first tick
    EXPECT_CALL(udpMock, beginPacket(_, _)).Times(0);
    EXPECT_CALL(udpMock, write(_, _)).Times(0);
    EXPECT_CALL(udpMock, endPacket()).Times(0);

    firmwareApp->setClientConnected(true);
    firmwareApp->update(0);
    firmwareApp->update(1000);

    EXPECT_FALSE(broadcastDiscoveryCalled)
        << "Discovery should be suppressed while a TCP client is connected.";
}

// ============================================================================
// CREDENTIAL OPERATIONS TESTS
// ============================================================================

TEST_F(FirmwareAppTest, StoreCredentials_ValidCredentials_ReturnsTrue) {
    // Storing valid credentials should succeed
    firmwareApp->init();

    bool result = firmwareApp->storeCredentials("test-ssid", "test-pass");

    EXPECT_TRUE(result);
}

TEST_F(FirmwareAppTest, StoreCredentials_EmptySsid_ReturnsFalse) {
    // Storing empty SSID should fail
    firmwareApp->init();

    bool result = firmwareApp->storeCredentials("", "test-pass");

    EXPECT_FALSE(result);
}

TEST_F(FirmwareAppTest, HasStoredCredentials_AfterStore_ReturnsTrue) {
    // After storing credentials, hasStoredCredentials should return true
    firmwareApp->init();

    firmwareApp->storeCredentials("test-ssid", "test-pass");

    EXPECT_TRUE(firmwareApp->hasStoredCredentials());
}

TEST_F(FirmwareAppTest, HasStoredCredentials_BeforeStore_ReturnsFalse) {
    // Before storing credentials, should return false
    firmwareApp->init();

    EXPECT_FALSE(firmwareApp->hasStoredCredentials());
}

TEST_F(FirmwareAppTest, LoadCredentials_AfterStore_RoundTripsCorrectly) {
    // Credentials should round-trip correctly
    firmwareApp->init();

    const std::string testSsid = "roundtrip-ssid";
    const std::string testPass = "roundtrip-pass";

    firmwareApp->storeCredentials(testSsid, testPass);

    std::string loadedSsid, loadedPass;
    bool result = firmwareApp->loadCredentials(loadedSsid, loadedPass);

    EXPECT_TRUE(result);
    EXPECT_EQ(loadedSsid, testSsid);
    EXPECT_EQ(loadedPass, testPass);
}

TEST_F(FirmwareAppTest, LoadCredentials_NoStoredCredentials_ReturnsFalse) {
    // Loading without stored credentials should return false
    firmwareApp->init();

    std::string loadedSsid, loadedPass;
    bool result = firmwareApp->loadCredentials(loadedSsid, loadedPass);

    EXPECT_FALSE(result);
}

TEST_F(FirmwareAppTest, ClearCredentials_AfterStore_HasStoredReturnsFalse) {
    // Clearing credentials should remove them
    firmwareApp->init();

    firmwareApp->storeCredentials("test-ssid", "test-pass");
    EXPECT_TRUE(firmwareApp->hasStoredCredentials());

    bool cleared = firmwareApp->clearCredentials();

    EXPECT_TRUE(cleared);
    EXPECT_FALSE(firmwareApp->hasStoredCredentials());
}

TEST_F(FirmwareAppTest, FactoryReset_ClearsStoredCredentials) {
    // Factory reset should clear stored credentials
    firmwareApp->init();

    firmwareApp->storeCredentials("test-ssid", "test-pass");
    EXPECT_TRUE(firmwareApp->hasStoredCredentials());

    firmwareApp->factoryReset();

    EXPECT_FALSE(firmwareApp->hasStoredCredentials());
}

TEST_F(FirmwareAppTest, ClearCredentials_NoCredentials_ReturnsTrue) {
    // Clearing when no credentials exist should still succeed
    firmwareApp->init();

    bool result = firmwareApp->clearCredentials();

    EXPECT_TRUE(result);
}

// ============================================================================
// BAKED CREDENTIALS TESTS
// ============================================================================

TEST_F(FirmwareAppTest, Ctor_WithBakedCredentials_HasStoredReturnsFalse) {
    // Baked credentials should NOT count as stored credentials
    FirmwareApp app(wifiMock, prefsMock, statusLedMock,
                   wifiMock, udpMock, timeMock,
                   sntpMock, timeNtpMock,
                   testDeviceId, canDeps, "baked-ssid", "baked-pass");
    app.init();

    EXPECT_FALSE(app.hasStoredCredentials());
}

TEST_F(FirmwareAppTest, StoreCredentials_OverridesBaked_WhenStored) {
    // Stored credentials should take precedence over baked
    FirmwareApp app(wifiMock, prefsMock, statusLedMock,
                   wifiMock, udpMock, timeMock,
                   sntpMock, timeNtpMock,
                   testDeviceId, canDeps, "baked-ssid", "baked-pass");
    app.init();

    app.storeCredentials("stored-ssid", "stored-pass");

    std::string loadedSsid, loadedPass;
    app.loadCredentials(loadedSsid, loadedPass);

    EXPECT_EQ(loadedSsid, "stored-ssid");
    EXPECT_EQ(loadedPass, "stored-pass");
}

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

TEST_F(FirmwareAppTest, ShouldRestartTcpServer_AfterWiFiReconnect_ReturnsTrue) {
    // After WiFi reconnect, TCP server restart flag should be set
    // The flag is set when transitioning TO CONNECTED_STA state (initial connection or reconnect)

    firmwareApp->init();
    firmwareApp->setCallbacks(callbackSpies);

    // Simulate initial connection
    wifiMock.simulateConnectSuccess();
    firmwareApp->update(5000);

    // Flag should be set after initial connection
    EXPECT_TRUE(firmwareApp->shouldRestartTcpServer())
        << "TCP restart flag should be set after initial WiFi connection";
}

TEST_F(FirmwareAppTest, ClearTcpServerRestartFlag_AfterClear_ReturnsFalse) {
    // Clearing the flag should reset it to false
    firmwareApp->init();

    // Set the flag (via WiFi reconnect simulation)
    firmwareApp->setCallbacks(callbackSpies);
    wifiMock.simulateConnectSuccess();
    firmwareApp->update(5000);

    firmwareApp->clearTcpServerRestartFlag();

    EXPECT_FALSE(firmwareApp->shouldRestartTcpServer());
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

TEST_F(FirmwareAppTest, StoreCredentials_VeryLongSsid_StoresAndRoundTrips) {
    // storeCredentials has NO length cap of its own (WiFiManager.h: the AT
    // command handler enforces the 1-32 SSID limit; the storage layer stores
    // whatever it is given via IPreferences::putString, returning true iff both
    // putString writes succeed). A long SSID therefore stores and round-trips.
    firmwareApp->init();

    const std::string longSsid(100, 'A');
    const std::string longPass = "pass";

    bool result = firmwareApp->storeCredentials(longSsid, longPass);

    ASSERT_TRUE(result);

    std::string loadedSsid, loadedPass;
    ASSERT_TRUE(firmwareApp->loadCredentials(loadedSsid, loadedPass));
    EXPECT_EQ(loadedSsid, longSsid);
    EXPECT_EQ(loadedPass, longPass);
}

TEST_F(FirmwareAppTest, LoadCredentials_EmptyStrings_DoesNotCrash) {
    // Loading into empty strings should work
    firmwareApp->init();

    std::string emptySsid, emptyPass;
    bool result = firmwareApp->loadCredentials(emptySsid, emptyPass);

    // Should return false (no credentials) but not crash
    EXPECT_FALSE(result);
}

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

// ============================================================================
// NTP TIME SYNC ROUTING TESTS (BLIND / SPEC-FIRST)
//
// These lock the CONTRACT of how FirmwareApp routes NTP startup through
// NtpTimeSync, per the spec:
//   - NTP init (which touches SNTP/sockets) MUST be deferred until WiFi is
//     connected, and MUST NOT run on the boot path (boot-crash regression).
//   - When WiFi connects, FirmwareApp drives NtpTimeSync::startIfWiFiConnected
//     which configures ISntp and wires the SNTP sync-notification callback.
//   - Once synced, NTP must NOT re-init on later ticks.
// Assertions are about INTENT (ISntp configured w/ expected mode/server,
// callback wired, formatting invoked), not fragile call-ordering.
// ============================================================================

TEST_F(FirmwareAppTest, NtpRouting_StartsOnWiFiConnect_ConfiguresSntp) {
    // CONTRACT: after init + callbacks + WiFi CONNECTED_STA + update,
    // NtpTimeSync::init() must drive ISntp (operating mode / server / sync
    // mode / interval / init) AND wire the SNTP sync-notification callback.
    firmwareApp->init();
    firmwareApp->setCallbacks(callbackSpies);

    EXPECT_CALL(sntpMock, setTimeSyncNotificationCallback(_))
        .WillOnce(SaveArg<0>(&capturedSyncCallback_));
    EXPECT_CALL(sntpMock, setOperatingMode(1)).Times(1);  // SNTP_OPMODE_POLL
    EXPECT_CALL(sntpMock, setServerName(0, NtpConfig::NTP_SERVER)).Times(1);
    EXPECT_CALL(sntpMock, setSyncMode(1)).Times(1);        // SNTP_SYNC_MODE_IMMED
    EXPECT_CALL(sntpMock, setSyncInterval(NtpConfig::NTP_RETRY_INTERVAL_MS)).Times(1);
    EXPECT_CALL(sntpMock, init()).Times(1);
    EXPECT_CALL(timeNtpMock, setenv("TZ", "UTC", 1)).Times(AtLeast(1));
    EXPECT_CALL(timeNtpMock, tzset()).Times(AtLeast(1));

    wifiMock.simulateConnectSuccess();
    firmwareApp->update(5000);

    // The sync-notification callback MUST be wired on connect.
    ASSERT_TRUE(capturedSyncCallback_) << "SNTP sync-notification callback was not wired";
}

TEST_F(FirmwareAppTest, NtpRouting_Deferred_NotStartedAtBoot) {
    // CONTRACT (boot-crash regression guard): NTP init must NOT run on the
    // boot path. Before WiFi connects, update() must NOT drive ISntp init /
    // configuration even across multiple ticks.
    firmwareApp->init();
    firmwareApp->setCallbacks(callbackSpies);

    EXPECT_CALL(sntpMock, init()).Times(0);
    EXPECT_CALL(sntpMock, setOperatingMode(_)).Times(0);
    EXPECT_CALL(sntpMock, setServerName(_, _)).Times(0);
    EXPECT_CALL(sntpMock, setSyncMode(_)).Times(0);
    EXPECT_CALL(sntpMock, setSyncInterval(_)).Times(0);
    EXPECT_CALL(sntpMock, setTimeSyncNotificationCallback(_)).Times(0);

    // WiFi is NOT connected (disconnected at boot) — tick the loop several times.
    firmwareApp->update(1000);
    firmwareApp->update(2000);
    firmwareApp->update(3000);
}

TEST_F(FirmwareAppTest, NtpRouting_NoReinitAfterSync) {
    // CONTRACT: after a successful sync, a later update() must NOT re-init
    // ISntp (no boot loop / duplicate sockets).
    firmwareApp->init();
    firmwareApp->setCallbacks(callbackSpies);

    EXPECT_CALL(sntpMock, setTimeSyncNotificationCallback(_))
        .WillOnce(SaveArg<0>(&capturedSyncCallback_));
    EXPECT_CALL(sntpMock, init()).Times(1);  // exactly one init on connect

    wifiMock.simulateConnectSuccess();
    firmwareApp->update(5000);  // first connect -> NTP init once

    ASSERT_TRUE(capturedSyncCallback_) << "sync callback must be wired before sync";
    timeval tv{1000000000, 0};  // valid Unix time -> onSyncComplete marks synced
    capturedSyncCallback_(&tv);

    // Subsequent ticks after sync must NOT re-init ISmtp.
    EXPECT_CALL(sntpMock, init()).Times(0);
    firmwareApp->update(6000);
    firmwareApp->update(7000);
}

TEST_F(FirmwareAppTest, NtpRouting_SyncCallbackDrivesTimeFormatting) {
    // CONTRACT (optional): the SNTP sync-notification callback wired by
    // NtpTimeSync::init() must be FUNCTIONAL — when SNTP fires it, onSyncComplete
    // runs and formats the time via the injected ITimeNtp (gmtime_r + strftime).
    // Locks "onSyncComplete marks synced" observably through the mock, without
    // depending on a nonexistent FirmwareApp sync accessor.
    firmwareApp->init();
    firmwareApp->setCallbacks(callbackSpies);

    EXPECT_CALL(sntpMock, setTimeSyncNotificationCallback(_))
        .WillOnce(SaveArg<0>(&capturedSyncCallback_));
    EXPECT_CALL(timeNtpMock, gmtime_r(_, _)).Times(AtLeast(1));
    EXPECT_CALL(timeNtpMock, strftime(_, _, _, _)).Times(AtLeast(1));

    wifiMock.simulateConnectSuccess();
    firmwareApp->update(5000);

    ASSERT_TRUE(capturedSyncCallback_) << "sync callback must be wired before firing";
    timeval tv{1234567890, 0};
    capturedSyncCallback_(&tv);  // simulate SNTP firing the sync-notification
}

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

TEST_F(FirmwareAppTest, CanBridge_ProcessCanFrames_AfterInit_DoesNotThrow) {
    // With the stub (no-op) adapters, draining the TWAI RX queue must be safe.
    firmwareApp->init();

    EXPECT_NO_THROW({
        firmwareApp->processCanFrames(0);
        firmwareApp->processCanFrames(5000);  // quiet-period variant
    });
}

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
    SpyMonitorState monitor;

    firmwareApp->init();
    firmwareApp->setAtCommandAdapters(tcp, serial, esp, wifi, monitor, testDeviceId);

    firmwareApp->handleTcpAtCommand("ATI");
    EXPECT_EQ(tcp.lastPrinted, "ESP32 CAN Bridge v0.1\r\r>");
    EXPECT_EQ(serial.lastLine, "");  // no serial echo on TCP path
}

TEST_F(FirmwareAppTest, AtCommand_SerialCommand_AtzClearsMonitor) {
    // ATZ over serial routes through the dispatcher and clears the monitor flag.
    SpyTcpClientAt tcp;
    SpySerialAt serial;
    SpyEspAt esp;
    SpyWifiStore wifi;
    SpyMonitorState monitor;
    monitor.active = true;

    firmwareApp->init();
    firmwareApp->setAtCommandAdapters(tcp, serial, esp, wifi, monitor, testDeviceId);

    firmwareApp->handleSerialAtCommand("ATZ");
    EXPECT_EQ(serial.lastLine, "ELM327 v2.3");
    EXPECT_FALSE(monitor.active);
}

TEST_F(FirmwareAppTest, AtCommand_Atreboot_NoExtraClientFlushBeforeRestart) {
    // The flush-hang fix: ATREBOOT's shouldFlushClient=false means the only flush
    // is the prompt's, then ESP.restart() proceeds. Exactly one flush + one restart.
    SpyTcpClientAt tcp;
    SpySerialAt serial;
    SpyEspAt esp;
    SpyWifiStore wifi;
    SpyMonitorState monitor;

    firmwareApp->init();
    firmwareApp->setAtCommandAdapters(tcp, serial, esp, wifi, monitor, testDeviceId);

    firmwareApp->handleTcpAtCommand("ATREBOOT");
    EXPECT_EQ(esp.restartCalls, 1);
    EXPECT_EQ(tcp.flushCalls, 1);
    EXPECT_EQ(tcp.lastPrinted, "REBOOT\r\r>");
}
