import Foundation
import XCTest
import Combine
@testable import VehicleSim

/// TDD tests for app-level heartbeat liveness — detecting a silent ESP32 drop.
///
/// When the ESP32 vanishes silently (TCP transport stays alive but no signal
/// data flows), the transport-exhaustion check (`!wrapper.isConnectionAlive`)
/// never fires. The heartbeat liveness probe fills this gap: every polling
/// cycle that sees fresh data records a heartbeat; if no heartbeat arrives
/// within 1s, the connection is considered stale and a reconnect is triggered.
///
/// All tests are deterministic via FakeVMClock — zero real-time waits for the
/// liveness logic itself. Runloop pumps are only for letting the polling timer
/// fire (0.1s real-time cadence), not for time-based assertions.
final class VehicleViewModelHeartbeatTests: XCTestCase {

    private var viewModel: VehicleViewModel!
    private var mockWrapper: MockVehicleSimWrapper!
    private var fakeClock: FakeVMClock!
    private var cancellables: Set<AnyCancellable> = []

    private let testDeviceId = Data((0..<16).map { UInt8($0) })

    override func setUp() {
        super.setUp()
        UserDefaults.standard.removeObject(forKey: "connectionMode")
        UserDefaults.standard.removeObject(forKey: "lastConnectedDeviceId")
        mockWrapper = MockVehicleSimWrapper()
        mockWrapper.getVehicleOptionsResult = [
            ["id": "tesla_model3", "displayName": "Tesla Model 3"]
        ]
        fakeClock = FakeVMClock()
        viewModel = VehicleViewModel(wrapper: mockWrapper, vmClock: fakeClock)
        _ = viewModel.vehicleOptions
        cancellables = []
    }

    override func tearDown() {
        viewModel?.stop()
        viewModel = nil
        mockWrapper = nil
        cancellables = []
        super.tearDown()
    }

    // MARK: - Helpers

    /// Pump the runloop briefly so the polling timer (0.1s real-time) fires.
    /// Does NOT advance the fake clock — use `fakeClock.advance(_)` for that.
    private func pumpRunLoop(_ timeout: TimeInterval = 0.15) {
        let deadline = Date(timeIntervalSinceNow: timeout)
        while Date() < deadline {
            RunLoop.current.run(mode: .default, before: Date(timeIntervalSinceNow: 0.01))
        }
    }

    private func makeDiscoveredESP32(
        deviceId: Data = Data((0..<16).map { UInt8($0) }),
        address: String = "192.168.1.100",
        canPort: UInt16 = 3333
    ) -> DiscoveredESP32 {
        DiscoveredESP32(
            deviceId: deviceId,
            address: address,
            port: 3335,
            canPort: canPort,
            timestamp: UInt64(Date().timeIntervalSince1970),
            receivedAt: Date()
        )
    }

    /// Establishes a WiFi connection by calling autoConnect with a mock
    /// that returns success, then settles the runloop so the polling timer
    /// fires at least once (recording the initial heartbeat).
    private func establishWiFiConnection(
        address: String = "192.168.1.100",
        canPort: UInt16 = 3333
    ) {
        viewModel.connectionMode = .wifi
        pumpRunLoop(0.1)
        mockWrapper.connectToDeviceResult = true
        let esp32 = makeDiscoveredESP32(address: address, canPort: canPort)
        viewModel.autoConnect(to: esp32)
        let deadline = Date(timeIntervalSinceNow: 1.0)
        while viewModel.connectionState != .connected && Date() < deadline {
            RunLoop.current.run(mode: .default, before: Date(timeIntervalSinceNow: 0.01))
        }
        XCTAssertEqual(viewModel.connectionState, .connected,
                       "Precondition: WiFi connection should be established")
        // Let the polling timer fire at least once so an initial heartbeat
        // is recorded via updateTelemetry().
        pumpRunLoop(0.15)
    }

    private func waitForConnectCall(timeout: TimeInterval = 0.5) -> Bool {
        let deadline = Date(timeIntervalSinceNow: timeout)
        while !mockWrapper.connectToDeviceCalled && Date() < deadline {
            RunLoop.current.run(mode: .default, before: Date(timeIntervalSinceNow: 0.01))
        }
        return mockWrapper.connectToDeviceCalled
    }

    // MARK: - 1. Heartbeat resets staleness

    /// After a heartbeat is received, the connection must NOT be considered
    /// stale at 999ms (just under the 1s timeout).
    func testHeartbeatReceivedResetsStale() {
        // Establish connection with data flowing — the polling cycle records
        // a heartbeat at fake-clock time 0.
        mockWrapper.isReceivingDataValue = true
        establishWiFiConnection()
        XCTAssertEqual(viewModel.connectionState, .connected)

        // Advance 999ms — just under the 1s timeout. The connection must
        // still be alive because the heartbeat was recorded at t=0.
        fakeClock.advance(999)
        pumpRunLoop()

        XCTAssertEqual(viewModel.connectionState, .connected,
                       "Connection should NOT be stale at 999ms after a heartbeat")
    }

    // MARK: - 2. Heartbeat missed for one second is stale

    /// When no heartbeat arrives for 1000ms (the timeout), the connection
    /// must be considered stale.
    func testHeartbeatMissedForOneSecondIsStale() {
        // Establish connection with data flowing — heartbeat recorded at t=0.
        mockWrapper.isReceivingDataValue = true
        establishWiFiConnection()
        XCTAssertEqual(viewModel.connectionState, .connected)

        // ESP32 goes silent: no more fresh data, so no more heartbeats.
        mockWrapper.isReceivingDataValue = false

        // Advance 1000ms — exactly the timeout. The connection must be stale.
        fakeClock.advance(1000)
        pumpRunLoop()

        XCTAssertEqual(viewModel.connectionState, .disconnected,
                       "Connection should be stale (disconnected) at 1000ms without heartbeat")
    }

    // MARK: - 3. Silent ESP triggers reconnect

    /// When the ESP32 vanishes silently (TCP transport stays alive but no
    /// signal data flows), the heartbeat liveness must detect the stale
    /// connection and trigger a reconnect — even though `isConnectionAlive`
    /// remains true.
    func testSilentESPTriggersReconnect() {
        // Establish connection with data flowing.
        mockWrapper.isReceivingDataValue = true
        establishWiFiConnection()
        XCTAssertEqual(viewModel.connectionState, .connected)

        // ESP32 goes silent: TCP transport is fine (isConnectionAlive = true)
        // but no signal data arrives (isReceivingData = false).
        mockWrapper.isReceivingDataValue = false
        mockWrapper.isConnectionAliveValue = true

        // Advance 1000ms — heartbeat is stale.
        fakeClock.advance(1000)
        pumpRunLoop()

        XCTAssertEqual(viewModel.connectionState, .disconnected,
                       "Silent ESP (no data for 1s) should trigger disconnect")
        XCTAssertTrue(viewModel.connectionStatus.lowercased().contains("reconnect"),
                      "Status should indicate reconnect intent; got \(viewModel.connectionStatus)")

        // The reconnect loop should attempt to reconnect.
        // Advance past the first backoff (1s) to trigger the attempt.
        fakeClock.advance(1000)
        pumpRunLoop()
        XCTAssertTrue(waitForConnectCall(timeout: 0.5),
                      "Reconnect loop should attempt to reconnect after silent drop")
    }

    // MARK: - 4. Active data prevents false disconnect

    /// When fresh data arrives every 200ms over 5 seconds, the connection
    /// must stay connected — the heartbeat is refreshed often enough to
    /// never be considered stale.
    func testActiveDataPreventsFalseDisconnect() {
        // Establish connection with data flowing.
        mockWrapper.isReceivingDataValue = true
        establishWiFiConnection()
        XCTAssertEqual(viewModel.connectionState, .connected)

        // Simulate fresh data every 200ms over 5 seconds (25 cycles).
        // Each polling tick records a heartbeat at the current fake-clock time.
        for _ in 0..<25 {
            fakeClock.advance(200)
            pumpRunLoop()
            XCTAssertEqual(viewModel.connectionState, .connected,
                           "Connection should stay connected with active data at t=\(fakeClock.now)")
        }

        XCTAssertEqual(viewModel.connectionState, .connected,
                       "Connection should remain connected after 5s of active data")
    }

    // MARK: - 5. Reconnect backoff advances with fake clock

    /// After a connection drop, the reconnect loop must use exponential
    /// backoff (1s, 2s, 4s) — verified by advancing the fake clock past
    /// each backoff window and asserting the reconnect attempt fires.
    func testReconnectBackoffAdvancesWithFakeClock() {
        // Establish connection.
        mockWrapper.isReceivingDataValue = true
        establishWiFiConnection()
        XCTAssertEqual(viewModel.connectionState, .connected)

        // Simulate a hard drop: transport is dead, and reconnects fail
        // (so the backoff loop continues without cancelling).
        mockWrapper.isConnectionAliveValue = false
        mockWrapper.isReceivingDataValue = false
        mockWrapper.connectToDeviceResult = false

        // Wait for the drop to be detected.
        pumpRunLoop(0.2)
        XCTAssertEqual(viewModel.connectionState, .disconnected,
                       "Drop should be detected before reconnect")

        // Advance 1000ms → first reconnect attempt (1s backoff).
        fakeClock.advance(1000)
        pumpRunLoop()
        XCTAssertTrue(mockWrapper.connectToDeviceCalled,
                      "First reconnect attempt should fire after 1s backoff")

        // Reset and advance 2000ms → second reconnect attempt (2s backoff).
        mockWrapper.connectToDeviceCalled = false
        fakeClock.advance(2000)
        pumpRunLoop()
        XCTAssertTrue(mockWrapper.connectToDeviceCalled,
                      "Second reconnect attempt should fire after 2s backoff")

        // Reset and advance 4000ms → third reconnect attempt (4s backoff).
        mockWrapper.connectToDeviceCalled = false
        fakeClock.advance(4000)
        pumpRunLoop()
        XCTAssertTrue(mockWrapper.connectToDeviceCalled,
                      "Third reconnect attempt should fire after 4s backoff")
    }
}
