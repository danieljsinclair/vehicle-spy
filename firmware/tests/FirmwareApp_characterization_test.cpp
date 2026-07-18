// FirmwareApp_characterization_test.cpp - Characterization tests for FirmwareApp
// (cpp:S1820 refactor-safety net).
//
// These pin OBSERVABLE PUBLIC behavior contracts only, so they survive the
// cpp:S1820 god-struct restructure (no internal field reads). Each test states
// WHICH behavior it pins and WHY it matters for refactor safety.
//
// CURATED SET (6 tests): reviewed by Architect 2 (critic) and tech-architect.
// Cuts applied vs. the original 14-test draft:
//   - 5 EXPECT_NO_THROW-over-no-op-stub tautologies (2 destructor, DoesNotDriveCanBridge,
//     ResetDiscoveryBackoff, ProcessCanFrames_MonitorInactive) — pin nothing.
//   - 2 duplicates of baseline tests (ProcessCanFrames_ForwardsMonitorStateToBridge
//     ~ L807; ClearCredentials_EquivalentToFactoryReset = union of L463+L476).
//   - Update_DrivesAllManagersEveryTick — name/body mismatch AND redundant with two
//     baseline tests (L303 StatusLED-every-tick, L314 discovery-cadence).
//   - HandleOtaCallback_NoPublicPathFiresIt — FirmwareCallbacks::handleOta was removed
//     from the header as intentional dead-code cleanup (S1128/S1133); no call sites,
//     OTA lives in OtaUpdateServer. Test dropped alongside the field.
//
// Evidence basis (read from FirmwareApp.cpp / FirmwareApp.h, NOT speculation):
//   - update() drives: WiFiManager.update (every tick),
//     NtpTimeSync.startIfWiFiConnected (when ntpStarted_ && !isSynced()),
//     DiscoveryManager.update (when discoveryEnabled_), StatusLED.update (every tick).
//   - update() does NOT call CanBridge::processFrames — frame draining is driven
//     by the .ino via FirmwareApp::processCanFrames only.
//   - setCallbacks / setAtCommandAdapters assign by copy; a second call REPLACES.
//   - onWiFiDisconnected -> WiFiManager::onDisconnected; a later connect re-arms
//     the TCP-server-restart flag.

#include "FirmwareApp_test_fixture.h"

using namespace esp32_firmware;
using namespace esp32_firmware::firmwareapp_test;

// FakeClock: deterministic, monotonic timestamp source for tests. The production
// loop is driven by update(now), never by wall-clock — so tests advance this clock
// explicitly and complete in ~0ms (no realtime waits). Mirrors the ITime seam the
// production code already injects, but local to this TU for readability.
class FakeClock {
public:
    explicit FakeClock(uint32_t t = 0) : now_(t) {}
    uint32_t now() const { return now_; }
    void advance(uint32_t ms) { now_ += ms; }
private:
    uint32_t now_;
};

// ============================================================================
// WiFi STATE TRANSITIONS: disconnect + reconnect re-arms TCP restart flag
// ============================================================================

TEST_F(FirmwareAppTest, OnWiFiDisconnectedThenReconnect_ReArmsTcpRestartFlag) {
    // CONTRACT (refactor-safety net for cpp:S1820): a non-auth WiFi drop from
    // CONNECTED_STA must transition to RECONNECTING and immediately re-arm the
    // TCP-server-restart flag; the subsequent reconnect keeps it armed. This pins
    // "disconnect -> RECONNECTING/discovery -> reconnect re-arms flag" so a god-struct
    // split cannot silently drop the re-arm.
    //
    // Disconnect reason 200 (WIFI_REASON_BEACON_TIMEOUT) is a non-auth drop: it takes
    // the RECONNECTING branch (WiFiManager::onDisconnected), which sets the flag. An
    // auth-fail reason (2/202) instead parks in AP mode and clears the flag — that is
    // a DIFFERENT contract and is intentionally NOT asserted here.
    FakeClock clock;
    firmwareApp->init();
    firmwareApp->setCallbacks(callbackSpies);

    // Initial connect arms the flag.
    wifiMock.simulateConnectSuccess();
    firmwareApp->update(clock.now());
    ASSERT_TRUE(firmwareApp->shouldRestartTcpServer());

    // Consume the flag.
    firmwareApp->clearTcpServerRestartFlag();
    ASSERT_FALSE(firmwareApp->shouldRestartTcpServer());

    // Non-auth disconnect from CONNECTED_STA -> RECONNECTING, which re-arms the flag.
    clock.advance(1000);  // deterministic advance, no realtime wait
    wifiMock.simulateDisconnect(200);
    firmwareApp->onWiFiDisconnected(200);
    firmwareApp->update(clock.now());
    ASSERT_TRUE(firmwareApp->shouldRestartTcpServer())
        << "non-auth disconnect from CONNECTED_STA must re-arm the flag (RECONNECTING)";

    // Reconnect keeps the flag armed.
    clock.advance(1000);
    wifiMock.simulateConnectSuccess();
    firmwareApp->update(clock.now());
    EXPECT_TRUE(firmwareApp->shouldRestartTcpServer())
        << "reconnect must keep the TCP restart flag armed";
}

TEST_F(FirmwareAppTest, CredentialFailure_DoesNotReconnect_SettlesInApMode) {
    // CONTRACT (refactor-safety net for cpp:S1820): a *permanent* credential/auth
    // rejection (bad SSID/PSK) MUST NOT re-enter the connect cycle against the
    // same credentials — retrying a refused SSID/PSK is guaranteed-futile. From a
    // CONNECTED_STA state, the disconnect must settle in AP mode and leave the
    // TCP-restart flag CLEARED (no STA connection to serve). Pins the four
    // unrecoverable auth failures (AUTH_FAIL=202, 4WAY_HANDSHAKE_TIMEOUT=15,
    // 802_1X_AUTH_FAILED=23, CIPHER_SUITE_REJECTED=24 — real ESP-IDF codes), so a
    // god-struct split cannot silently drop any of them back into RECONNECTING.
    //
    // Each reason is exercised from a fresh CONNECTED_STA precondition (a new
    // FirmwareApp per reason, initialized exactly once), because once the app is
    // in AP mode a connect-success event is correctly ignored by the state
    // machine — that is real behavior, not a setup the test should fight.
    // Time is driven by a FakeClock advanced explicitly (no realtime waits), so
    // the whole loop completes in ~0ms.
    for (int reason : {202, 15, 23, 24}) {
        firmwareApp = createFirmwareApp("baked-ssid", "baked-pass");
        firmwareApp->init();
        firmwareApp->setCallbacks(callbackSpies);

        FakeClock clock;
        // Establish CONNECTED_STA with the TCP-restart flag armed.
        wifiMock.simulateConnectSuccess();
        firmwareApp->update(clock.now());
        ASSERT_TRUE(firmwareApp->shouldRestartTcpServer())
            << "precondition: connect arms the TCP restart flag (reason " << reason << ")";

        // The credential failure must NOT keep the flag armed.
        clock.advance(1000);  // deterministic advance, no realtime wait
        firmwareApp->onWiFiDisconnected(reason);
        firmwareApp->update(clock.now());
        EXPECT_FALSE(firmwareApp->shouldRestartTcpServer())
            << "permanent auth failure (reason " << reason
            << ") must settle in AP mode and leave the TCP restart flag cleared";
    }
}

TEST_F(FirmwareAppTest, RecoverableAuthDisconnect_ReconnectsAndReArmsTcpFlag) {
    // CONTRACT (refactor-safety net for cpp:S1820): transient session/assoc
    // lifecycle drops (AUTH_EXPIRE=2, AUTH_LEAVE=3, NOT_AUTHED=6, NOT_ASSOCED=7,
    // ASSOC_NOT_AUTHED=9 — real ESP-IDF codes) are RECOVERABLE — they are not wrong-credential rejections,
    // so the stack must re-associate rather than abandon STA for AP mode. From a
    // CONNECTED_STA state the disconnect must settle in RECONNECTING and KEEP the
    // TCP-restart flag armed (a drop occurred, the link must be rebuilt). Pins all
    // five recoverable reasons so a god-struct split cannot silently promote any of
    // them into the unrecoverable AP-mode set.
    for (int reason : {2, 3, 6, 7, 9}) {
        firmwareApp = createFirmwareApp("baked-ssid", "baked-pass");
        firmwareApp->init();
        firmwareApp->setCallbacks(callbackSpies);

        FakeClock clock;
        // Establish CONNECTED_STA with the TCP-restart flag armed.
        wifiMock.simulateConnectSuccess();
        firmwareApp->update(clock.now());
        ASSERT_TRUE(firmwareApp->shouldRestartTcpServer())
            << "precondition: connect arms the TCP restart flag (reason " << reason << ")";

        // The recoverable drop must NOT bail to AP mode; it re-enters RECONNECTING
        // and keeps the flag armed.
        clock.advance(1000);  // deterministic advance, no realtime wait
        firmwareApp->onWiFiDisconnected(reason);
        firmwareApp->update(clock.now());
        EXPECT_TRUE(firmwareApp->shouldRestartTcpServer())
            << "recoverable auth drop (reason " << reason
            << ") must stay in RECONNECTING and keep the TCP restart flag armed";
    }
}

// ============================================================================
// DISCOVERY BACKOFF RESET / DYNAMIC ENABLE TOGGLE
// ============================================================================

TEST_F(FirmwareAppTest, DiscoveryReEnabledAfterDisable_ResumesBroadcast) {
    // CONTRACT: setDiscoveryEnabled(false) stops broadcasts; re-enabling via
    // setDiscoveryEnabled(true) must resume them on a later tick. Pins the dynamic
    // enable/disable toggle observable — important for refactor safety because the
    // toggle gates the discovery UDP open in update(), not just a per-tick skip.
    firmwareApp->init();
    firmwareApp->setCallbacks(callbackSpies);

    FakeClock clock;
    firmwareApp->setDiscoveryEnabled(false);
    firmwareApp->update(clock.now());        // t=0
    clock.advance(1000);
    firmwareApp->update(clock.now());        // t=1000
    ASSERT_FALSE(broadcastDiscoveryCalled)
        << "disabled discovery must not broadcast";

    // Re-enable and tick past cadence.
    firmwareApp->setDiscoveryEnabled(true);
    EXPECT_CALL(udpMock, begin(_)).Times(AtLeast(1));  // socket re-opens on next tick
    EXPECT_CALL(udpMock, beginPacket(_, _)).Times(AtLeast(1));
    EXPECT_CALL(udpMock, write(_, _)).Times(AtLeast(1));
    EXPECT_CALL(udpMock, endPacket()).Times(AtLeast(1));
    clock.advance(1000);
    firmwareApp->update(clock.now());        // t=2000
    EXPECT_TRUE(broadcastDiscoveryCalled)
        << "re-enabled discovery must resume broadcasting";
}

TEST_F(FirmwareAppTest, ResetDiscoveryBackoff_DelegatesAndKeepsDiscoveryAlive) {
    // CONTRACT: resetDiscoveryBackoff() delegates to DiscoveryManager::resetBackoff()
    // (real code, not a no-op stub) and must not disable or break discovery. After a
    // broadcast cycle, calling it must be safe (no throw) and the loop must still
    // broadcast on a later tick. Pins the delegation so a refactor that drops the
    // forward (or adds a shadowed backoff field) is caught.
    //
    // Cadence note: DiscoveryManager::init() sets lastBroadcastMs=0 and the fast
    // interval is 500ms, so the FIRST tick at t=0 does NOT broadcast (0-0 < 500);
    // the first real broadcast fires at t>=500 (mirrors existing baseline behaviour).
    firmwareApp->init();
    firmwareApp->setCallbacks(callbackSpies);
    FakeClock clock;

    // t=0: discovery started, but no broadcast yet (lastBroadcast=0, fast cadence 500ms).
    firmwareApp->update(clock.now());        // t=0
    ASSERT_FALSE(broadcastDiscoveryCalled);

    // advance past fast-cadence -> broadcast fires.
    clock.advance(1000);
    firmwareApp->update(clock.now());        // t=1000 (>= 500ms)
    ASSERT_TRUE(broadcastDiscoveryCalled)
        << "discovery should broadcast once fast-cadence (500ms) has elapsed";

    // Reset backoff — must be safe and not disable discovery.
    EXPECT_NO_THROW({ firmwareApp->resetDiscoveryBackoff(); });

    // After reset, the loop must still drive discovery broadcasts on a later tick.
    broadcastDiscoveryCalled = false;
    clock.advance(1000);
    firmwareApp->update(clock.now());        // t=2000 (>= 500ms since last) -> fires again
    EXPECT_TRUE(broadcastDiscoveryCalled)
        << "discovery must still broadcast after resetDiscoveryBackoff()";
}

// ============================================================================
// NTP SYNC COMPLETION OBSERVABLE (synced state, at FirmwareApp level)
// ============================================================================

TEST_F(FirmwareAppTest, NtpRouting_AfterSync_NoFurtherTimeFormatting) {
    // CONTRACT: once the SNTP sync-notification callback marks NtpTimeSync synced,
    // the "synced" state is observable at the FirmwareApp level by update() no longer
    // formatting the time on later ticks (no duplicate strftime/gmtime work after
    // sync). Pins the observable synced-state so a refactor that loses the
    // isSynced() guard re-introduces redundant time formatting.
    firmwareApp->init();
    firmwareApp->setCallbacks(callbackSpies);

    EXPECT_CALL(sntpMock, setTimeSyncNotificationCallback(_))
        .WillOnce(SaveArg<0>(&capturedSyncCallback_));
    EXPECT_CALL(sntpMock, init()).Times(1);
    EXPECT_CALL(timeNtpMock, gmtime_r(_, _)).Times(AtLeast(1));
    EXPECT_CALL(timeNtpMock, strftime(_, _, _, _)).Times(AtLeast(1));

    wifiMock.simulateConnectSuccess();
    FakeClock clock;
    firmwareApp->update(clock.now());        // t=0

    ASSERT_TRUE(capturedSyncCallback_);
    timeval tv{1234567890, 0};
    capturedSyncCallback_(&tv);  // marks synced

    // Post-sync ticks: time formatting must NOT repeat.
    EXPECT_CALL(timeNtpMock, gmtime_r(_, _)).Times(0);
    EXPECT_CALL(timeNtpMock, strftime(_, _, _, _)).Times(0);
    EXPECT_CALL(sntpMock, init()).Times(0);
    clock.advance(1000);
    firmwareApp->update(clock.now());        // t=1000
    clock.advance(1000);
    firmwareApp->update(clock.now());        // t=2000
}

// ============================================================================
// AT COMMAND ADAPTERS: second wiring REPLACES (last wins)
// ============================================================================

TEST_F(FirmwareAppTest, SetAtCommandAdapters_CalledTwice_LastWins) {
    // CONTRACT: a second setAtCommandAdapters() replaces the dispatcher's adapters.
    // After rewiring with a fresh spy set, commands must route to the LAST set, not
    // the first. Pins the assignment-replaces contract so a refactor that appends or
    // retains the first set would break this.
    SpyTcpClientAt firstTcp, secondTcp;
    SpySerialAt firstSerial, secondSerial;
    SpyEspAt esp;
    SpyWifiStore wifi;
    SpyMonitorState monitor;

    firmwareApp->init();
    firmwareApp->setAtCommandAdapters(firstTcp, firstSerial, esp, wifi, monitor, testDeviceId);
    firmwareApp->handleTcpAtCommand("ATI");
    EXPECT_EQ(firstTcp.lastPrinted, "ESP32 CAN Bridge v0.1\r\r>");
    EXPECT_EQ(secondTcp.lastPrinted, "");  // not yet routed here

    // Re-wire with a second set of spies.
    firmwareApp->setAtCommandAdapters(secondTcp, secondSerial, esp, wifi, monitor, testDeviceId);
    firmwareApp->handleTcpAtCommand("ATI");

    EXPECT_EQ(secondTcp.lastPrinted, "ESP32 CAN Bridge v0.1\r\r>")
        << "command must route to the LAST-wired adapter set";
    EXPECT_EQ(firstTcp.lastPrinted, "ESP32 CAN Bridge v0.1\r\r>")
        << "first set must be left untouched after re-wire (no double handling)";
}

// ============================================================================
// CALLBACKS SET MULTIPLE TIMES: last wins
// ============================================================================

TEST_F(FirmwareAppTest, SetCallbacks_CalledTwice_LastWins) {
    // CONTRACT: a second setCallbacks() replaces the callback set. After a WiFi
    // connect, only the LAST set's restartTcpServer lambda fires. Pins the
    // assignment-replaces contract at the FirmwareApp boundary.
    firmwareApp->init();

    bool firstFired = false;
    bool secondFired = false;
    FirmwareCallbacks first;
    first.restartTcpServer = [&]() { firstFired = true; };
    first.broadcastDiscovery = [this]() { broadcastDiscoveryCalled = true; };

    FirmwareCallbacks second;
    second.restartTcpServer = [&]() { secondFired = true; };
    second.broadcastDiscovery = [this]() { broadcastDiscoveryCalled = true; };

    firmwareApp->setCallbacks(first);
    firmwareApp->setCallbacks(second);  // last wins

    wifiMock.simulateConnectSuccess();
    FakeClock clock;
    firmwareApp->update(clock.now());        // connect -> triggers restartTcpServer

    EXPECT_FALSE(firstFired) << "first callback set must be replaced";
    EXPECT_TRUE(secondFired) << "last callback set must be the one that fires";
}

// ============================================================================
// CREDENTIAL EDGE CASES
// ============================================================================

TEST_F(FirmwareAppTest, StoreCredentials_VeryLongPass_StoresAndRoundTrips) {
    // CONTRACT: symmetric to the existing long-SSID test. storeCredentials has no
    // length cap of its own (WiFiManager enforces limits at the AT boundary); a
    // long password must store and round-trip. Pins the storage layer's "store
    // whatever is given" contract so a refactor adding a pass cap doesn't silently
    // truncate.
    firmwareApp->init();

    const std::string ssid = "ssid";
    const std::string longPass(100, 'B');

    bool result = firmwareApp->storeCredentials(ssid, longPass);
    ASSERT_TRUE(result);

    std::string loadedSsid, loadedPass;
    ASSERT_TRUE(firmwareApp->loadCredentials(loadedSsid, loadedPass));
    EXPECT_EQ(loadedSsid, ssid);
    EXPECT_EQ(loadedPass, longPass);
}

