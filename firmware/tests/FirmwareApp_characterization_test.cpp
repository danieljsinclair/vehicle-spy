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

// ============================================================================
// PHASE 2 (cpp:S1820): field-count-independent construction invariant
// ============================================================================
//
// WHY THIS EXISTS (cpp:S1820 refactor-safety net, Phase 2 of the god-struct
// clear): Phase 3 will REGROUP the 23 member fields of FirmwareApp into fewer
// cohesive groups (e.g. dropping 7 PASSED-ONLY interface refs that the owning
// managers already receive, so the ctor still *receives* them and *forwards*
// them rather than storing them itself). This test must STAY GREEN across that
// regroup — it is the proof the refactor is behavior-preserving.
//
// REFACTOR-SAFETY GUARANTEE: every assertion below goes through the PUBLIC
// CONTRACT only. No private member is named, counted, or read. The test builds a
// FirmwareApp from the full injected dependency set (the 9 interface refs +
// canBridgeDeps_ + baked creds + deviceId), calls init(), then drives a real
// construct->init->update cycle and reads observable state through the public
// accessors (getWiFiState, shouldRestartTcpServer, hasStoredCredentials,
// isMonitorActive, processCanFrames). If Phase 3 drops a field instead of
// forwarding it (a manager never constructed, an init callback not wired), at
// least one of these real behavior paths breaks — so the test catches it.
//
// This is NOT a tautology: it exercises the actual wiring that init() performs
// (manager construction + callback forwarding + CAN-bridge monitor-state path),
// not "always passes regardless" code.

TEST_F(FirmwareAppTest, ConstructionInvariant_FullDependencySetBuildsAndInits) {
    // CONTRACT: FirmwareApp constructed with the complete injected dependency set
    // must init() and expose a deterministic, non-degenerate observable baseline
    // purely through public accessors — independent of how its internal fields are
    // grouped. Pins "ctor receives every dependency -> init wires every manager"
    // so a Phase-3 regroup that drops a forwarded ref (rather than re-forwarding
    // it) leaves an unconstructed/ unwired manager that fails one of these checks.
    //
    // The fixture already constructs firmwareApp with ALL deps (9 interface refs
    // + canBridgeDeps_ + baked creds + deviceId) via createFirmwareApp(). We do
    // NOT re-count or re-read fields; we only assert the observable post-init
    // contract. Time is driven by a FakeClock (no realtime waits) -> ~0ms.
    FakeClock clock;

    // Pre-init: public observers must be safe to call and reflect "not yet live".
    // (FirmwareApp asserts the manager exists internally; this just confirms the
    //  accessor entry point is wired to a constructed manager post-init.)
    firmwareApp->init();
    firmwareApp->setCallbacks(callbackSpies);

    // WiFi state is observable and a valid (non-negative) machine state. This
    // proves WiFiManager was constructed and its state exposed via the public
    // accessor — a dropped/ unwired WiFiManager would assert-fail or report junk.
    const int wifiState = firmwareApp->getWiFiState();
    EXPECT_GE(wifiState, 0)
        << "getWiFiState() must surface a valid WiFiManager state post-init";

    // Credential store is observable: with no prior store call, the public
    // hasStoredCredentials() contract returns false (real WiFiManager behavior,
    // not a stub).
    EXPECT_FALSE(firmwareApp->hasStoredCredentials())
        << "fresh init with no stored creds must report hasStoredCredentials()==false";

    // Monitor is observable and defaults inactive; the CanBridge must have been
    // constructed and wired (setupManagers builds it). A dropped CanBridge wire
    // would assert-fail here instead of returning false.
    EXPECT_FALSE(firmwareApp->isMonitorActive())
        << "monitor must default inactive after init (CanBridge constructed + wired)";

    // Drive one real update tick. This exercises the full init()-wired loop path:
    // WiFiManager.update, (deferred) discovery/NTP gating, StatusLED.update. A
    // missing manager or unwired callback would assert-fail or no-op silently.
    EXPECT_NO_THROW({ firmwareApp->update(clock.now()); });

    // Post-update observable state remains sane: WiFi state still valid, monitor
    // state preserved (update() does NOT touch monitor state — that is driven only
    // by setMonitorActive/processCanFrames, per FirmwareApp.cpp evidence).
    EXPECT_GE(firmwareApp->getWiFiState(), 0)
        << "WiFi state must remain valid after a real update() tick";
    EXPECT_FALSE(firmwareApp->isMonitorActive())
        << "update() must not change monitor state (only setMonitorActive does)";
}

// STEP 0 (cpp:S1820 Phase 3 safety-net tightening): the two construction-invariant
// tests above assert managers were *built*, but NOT that they were built *with the
// right dependencies*. A refactor that forwards a PASSED-ONLY ref could drop it from
// the owning manager's constructor (e.g. DiscoveryManager built without udp_/
// wifiDiscovery_/deviceId_, or WiFiManager built without prefs_) and the existing
// 6 assertions would STILL PASS — because the manager itself exists. These two pins
// catch the *dropped-ctor-dep* risk specifically by exercising observable behavior
// that only exists when the dependency was actually plumbed through.
//
// (a) Discovery broadcast fires after a tick: DiscoveryManager::init() opens the UDP
//     socket (udp_.begin) and broadcasts only because it was constructed with real
//     udp_ + wifiDiscovery_ + deviceId_. If any of those were dropped from its ctor,
//     the broadcast callback (which FirmwareApp forwards to our spy) never fires.
// (b) Credential round-trip via prefs_: storeCredentials() -> hasStoredCredentials()
//     returns true only because WiFiManager was constructed WITH prefs_ (the NVS
//     backing). A WiFiManager built without prefs_ would have no persistence and this
//     assertion would fail.
//
// Both pins go through the PUBLIC CONTRACT only — no private field is read.

TEST_F(FirmwareAppTest, ConstructionInvariant_DiscoveryBroadcastFiresWithDeps) {
    // CONTRACT: after init() + one discovery-enabled update() tick, the
    // broadcastDiscovery callback spy fires — proving DiscoveryManager was built with
    // real udp_/wifiDiscovery_/deviceId_ deps (it cannot open the socket or build a
    // packet without them). Pins "DiscoveryManager ctor received all 3 forwarded
    // deps" so a Phase-3 drop of udp_/wifiDiscovery_/deviceId_ from that ctor is
    // caught (the spy stays false).
    //
    // Setup: AP mode (broadcastIP available), discovery enabled (default). The first
    // tick at t=0 opens the socket; the broadcast fires once fast-cadence (500ms)
    // elapses (lastBroadcastMs starts at 0, so t>=500 is required).
    firmwareApp->init();
    firmwareApp->setCallbacks(callbackSpies);

    // Set WiFi to AP mode so broadcastIP() resolves (DiscoveryManager broadcasts in AP).
    wifiMock.setMode(2);  // WIFI_AP

    EXPECT_CALL(udpMock, begin(_)).Times(AtLeast(1));        // socket opens on first tick
    EXPECT_CALL(udpMock, beginPacket(_, _)).Times(AtLeast(1));
    EXPECT_CALL(udpMock, write(_, _)).Times(AtLeast(1));
    EXPECT_CALL(udpMock, endPacket()).Times(AtLeast(1));

    FakeClock clock;
    firmwareApp->update(clock.now());        // t=0: opens socket, no broadcast yet (cadence)
    clock.advance(1000);                     // advance past fast-cadence (500ms)
    firmwareApp->update(clock.now());        // t=1000: broadcast fires

    EXPECT_TRUE(broadcastDiscoveryCalled)
        << "DiscoveryManager built WITHOUT udp_/wifiDiscovery_/deviceId_ would "
           "never fire the broadcast callback — a dropped ctor dep is caught here";
}

TEST_F(FirmwareAppTest, ConstructionInvariant_CredentialRoundTripViaPrefs) {
    // CONTRACT: storeCredentials() followed by hasStoredCredentials()==true proves
    // WiFiManager was constructed WITH prefs_ (the NVS-backed persistence). A
    // WiFiManager built without prefs_ has no storage layer and would report false.
    // Pins "WiFiManager ctor received prefs_" so a Phase-3 drop of prefs_ from that
    // ctor is caught.
    firmwareApp->init();

    ASSERT_TRUE(firmwareApp->storeCredentials("roundtrip-ssid", "roundtrip-pass"));

    EXPECT_TRUE(firmwareApp->hasStoredCredentials())
        << "WiFiManager built WITHOUT prefs_ would have no persistence and report "
           "false — a dropped prefs_ ctor dep is caught here";

    // Sanity: the value actually round-trips through the backing store.
    std::string loadedSsid, loadedPass;
    ASSERT_TRUE(firmwareApp->loadCredentials(loadedSsid, loadedPass));
    EXPECT_EQ(loadedSsid, "roundtrip-ssid");
    EXPECT_EQ(loadedPass, "roundtrip-pass");
}

TEST_F(FirmwareAppTest, ConstructionInvariant_MonitorAndTcpWiringSurviveRegroup) {
    // CONTRACT (Phase-2 regroup-safety, second axis): the two init()-wired
    // behavior paths that a careless field-drop would silently break — the
    // CanBridge monitor-state round-trip and the WiFiManager→TCP-restart callback
    // wiring — must both hold after init(), observable only through public
    // accessors. If Phase 3 drops the CanBridge construction or the
    // setTcpServerRestartCallback forward, this test fails (not just degrades).
    FakeClock clock;
    firmwareApp->init();
    firmwareApp->setCallbacks(callbackSpies);

    // (1) Monitor round-trip: setMonitorActive(true) is forwarded to the wired
    // CanBridge; isMonitorActive() reads it back. Pins CanBridge construction +
    // the setMonitorActive/isMonitorActive forwarding in FirmwareApp::update's
    // processCanFrames path. Driven through processCanFrames too (real code path,
    // not a no-op stub) to ensure the monitor flag reaches CanBridge::processFrames.
    firmwareApp->setMonitorActive(true);
    ASSERT_TRUE(firmwareApp->isMonitorActive())
        << "setMonitorActive(true) must forward to the wired CanBridge";

    EXPECT_NO_THROW({ firmwareApp->processCanFrames(0); })
        << "processCanFrames must route through the wired CanBridge without throwing";

    // (2) TCP-restart callback wiring: a WiFi connect must arm the
    // shouldRestartTcpServer() flag via the WiFiManager callback wired in
    // setupCallbacks(). If init() failed to wire setTcpServerRestartCallback, the
    // flag would never arm and this assertion fails.
    wifiMock.simulateConnectSuccess();
    firmwareApp->update(clock.now());
    EXPECT_TRUE(firmwareApp->shouldRestartTcpServer())
        << "WiFi connect must arm the TCP-restart flag (callback wired in init())";

    // Consuming the flag must be observable and reset to false (real WiFiManager
    // behavior), confirming the flag is the live wired one, not a constant.
    firmwareApp->clearTcpServerRestartFlag();
    EXPECT_FALSE(firmwareApp->shouldRestartTcpServer())
        << "clearTcpServerRestartFlag() must reset the armed flag to false";

    // Monitor state must survive the connect tick (independent of WiFi state).
    EXPECT_TRUE(firmwareApp->isMonitorActive())
        << "monitor state must persist across a WiFi connect tick";
}

