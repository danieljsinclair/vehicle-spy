import Foundation
import XCTest
@testable import VehicleSim

/// Pins the app-background pause contract for ESP32 discovery.
///
/// VehicleViewModel subscribes to `.pauseDiscovery` in its init (lines 113–117).
/// The Combine sink body (line 115–116) is the only notification handler that
/// is not exercised by the existing suite: no test posts `.pauseDiscovery`.
/// These tests drive that handler through the public NotificationCenter API
/// and assert the observable side-effect on `isESP32DiscoveryActive`.
///
/// The three cases cover the guard inside `pauseDiscoveryIfNeeded()`:
///   • WiFi + not connected  → discovery is stopped
///   • WiFi + connected      → discovery is preserved (skip while connected)
///   • non-WiFi              → discovery is preserved (handler is WiFi-only)

final class VehicleViewModelDiscoveryLifecycleTests: XCTestCase {

    private var viewModel: VehicleViewModel!
    private var mockWrapper: MockVehicleSimWrapper!

    override func setUp() {
        super.setUp()
        UserDefaults.standard.removeObject(forKey: "connectionMode")
        mockWrapper = MockVehicleSimWrapper()
        viewModel = VehicleViewModel(wrapper: mockWrapper)
    }

    override func tearDown() {
        viewModel = nil
        mockWrapper = nil
        super.tearDown()
    }

    // MARK: - pauseDiscovery notification (uncovered lines 115–116)

    func testPauseDiscoveryInWiFiModeStopsActiveDiscovery() {
        // Arrange: WiFi mode, not connected, discovery active.
        viewModel.connectionMode = .wifi
        viewModel.connectionState = .disconnected
        viewModel.isESP32DiscoveryActive = true

        // Act: simulate the app moving to background.
        NotificationCenter.default.post(name: .pauseDiscovery, object: nil)

        // Assert: the listener is torn down so discovery is no longer active.
        XCTAssertFalse(viewModel.isESP32DiscoveryActive,
                       "pauseDiscovery should stop ESP32 discovery when in WiFi mode and not connected")
    }

    func testPauseDiscoveryDoesNotInterruptActiveWiFiConnection() {
        // Arrange: WiFi mode, already connected, discovery active.
        // The guard in pauseDiscoveryIfNeeded() must bail out when connected
        // so tearing down the listener mid-session does not drop the link.
        viewModel.connectionMode = .wifi
        viewModel.connectionState = .connected
        viewModel.isESP32DiscoveryActive = true

        // Act
        NotificationCenter.default.post(name: .pauseDiscovery, object: nil)

        // Assert: discovery stays active because the connection is live.
        XCTAssertTrue(viewModel.isESP32DiscoveryActive,
                      "pauseDiscovery must not stop discovery while a WiFi connection is active")
    }

    func testPauseDiscoveryIsIgnoredOutsideWiFiMode() {
        // Arrange: BLE mode, discovery active (BLE has no ESP32 listener).
        viewModel.connectionMode = .ble
        viewModel.connectionState = .disconnected
        viewModel.isESP32DiscoveryActive = true

        // Act
        NotificationCenter.default.post(name: .pauseDiscovery, object: nil)

        // Assert: discovery untouched because the handler is WiFi-only.
        XCTAssertTrue(viewModel.isESP32DiscoveryActive,
                      "pauseDiscovery must be a no-op when the active connection mode is not WiFi")
    }
}
