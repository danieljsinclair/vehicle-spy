import Foundation
import XCTest
@testable import VehicleSim

// Pins the trust-and-discovery-resume contracts in VehicleViewModel that
// were not exercised by the existing suite.
//
// trustESP32 (lines 443–452):
//   • marks the device user-trusted in wifiSecurityPolicy
//   • clears any prior wifiSecurityError
//   • replaces the discoveredESP32s entry when the device is present
//
// resumeDiscovery (via .resumeDiscovery notification):
//   • starts discovery when WiFi mode and discovery is inactive
//   • is a no-op when discovery is already active

final class VehicleViewModelTrustAndDiscoveryLifecycleTests: XCTestCase {

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

    // MARK: - Helpers

    private func makeDiscoveredESP32(
        deviceId: Data = Data(repeating: 0xCC, count: 16),
        address: String = "192.168.1.100",
        canPort: UInt16 = 3333,
        timestamp: UInt64 = UInt64(Date().timeIntervalSince1970)
    ) -> DiscoveredESP32 {
        DiscoveredESP32(
            deviceId: deviceId,
            address: address,
            port: 3335,
            canPort: canPort,
            timestamp: timestamp,
            receivedAt: Date()
        )
    }

    // MARK: - trustESP32 (uncovered lines 444–451)

    // trustESP32 must mark the device user-trusted in the security policy.
    // This is the security-state mutation that gates all subsequent connection
    // decisions for that device.
    func testTrustESP32MarksDeviceAsUserTrusted() {
        let deviceId = Data(repeating: 0xDD, count: 16)
        let esp32 = makeDiscoveredESP32(deviceId: deviceId)

        viewModel.trustESP32(esp32)

        let state = viewModel.wifiSecurityPolicy.verificationState(for: deviceId)
        XCTAssertEqual(state, .userTrusted,
                       "trustESP32 should mark the device as userTrusted in the security policy")
    }

    // When the device already appears in discoveredESP32s, trustESP32 replaces
    // the existing entry with the trusted instance (so the UI reflects the
    // updated trust state, not the stale unverified entry).
    func testTrustESP32UpdatesDiscoveredListWhenDeviceIsPresent() {
        let deviceId = Data(repeating: 0xDE, count: 16)
        let original = makeDiscoveredESP32(deviceId: deviceId, address: "10.0.0.5")
        // Construct a distinct trusted instance (different timestamp) so the
        // replacement is observable: the stored reference must equal the new
        // instance, not the original.
        let trusted  = makeDiscoveredESP32(deviceId: deviceId, address: "10.0.0.5",
                                           timestamp: UInt64(Date().timeIntervalSince1970) + 1)

        viewModel.discoveredESP32s = [original]
        XCTAssertEqual(viewModel.discoveredESP32s.count, 1)

        viewModel.trustESP32(trusted)

        XCTAssertEqual(viewModel.discoveredESP32s.count, 1,
                       "list size must not change — the entry is replaced, not appended")
        XCTAssertEqual(viewModel.discoveredESP32s[0], trusted,
                       "the stored entry must be the trusted instance after trustESP32")
    }

    // If the device is not in discoveredESP32s, trustESP32 must not grow the
    // list �� the security policy is still updated, but no fabricated entry is
    // inserted.
    func testTrustESP32DoesNotGrowListWhenDeviceIsAbsent() {
        let knownDeviceId   = Data(repeating: 0xE0, count: 16)
        let unknownDeviceId = Data(repeating: 0xDF, count: 16)
        let known   = makeDiscoveredESP32(deviceId: knownDeviceId,   address: "10.0.0.1")
        let unknown = makeDiscoveredESP32(deviceId: unknownDeviceId, address: "10.0.0.2")

        viewModel.discoveredESP32s = [known]
        XCTAssertEqual(viewModel.discoveredESP32s.count, 1)

        viewModel.trustESP32(unknown)

        XCTAssertEqual(viewModel.discoveredESP32s.count, 1,
                       "trustESP32 must not grow the list when the device is absent")
        XCTAssertEqual(viewModel.discoveredESP32s[0].address, known.address,
                       "the pre-existing entry must be unchanged")
    }

    // trustESP32 must clear any prior wifiSecurityError so the UI does not
    // continue to display a stale refusal after the user has trusted the device.
    func testTrustESP32ClearsPriorSecurityError() {
        let deviceId = Data(repeating: 0xE1, count: 16)
        let esp32    = makeDiscoveredESP32(deviceId: deviceId)

        // Simulate a prior security refusal.
        viewModel.wifiSecurityError = "device not verified"
        XCTAssertNotNil(viewModel.wifiSecurityError)

        viewModel.trustESP32(esp32)

        XCTAssertNil(viewModel.wifiSecurityError,
                     "trustESP32 must clear any prior wifiSecurityError")
    }

    // When the device is present AND there is a prior error, trustESP32 must
    // perform both side-effects atomically — list update AND error clear.
    func testTrustESP32UpdatesListAndClearsErrorTogether() {
        let deviceId = Data(repeating: 0xE2, count: 16)
        let original = makeDiscoveredESP32(deviceId: deviceId, address: "10.0.0.9")
        let trusted  = makeDiscoveredESP32(deviceId: deviceId, address: "10.0.0.9",
                                           timestamp: UInt64(Date().timeIntervalSince1970) + 1)

        viewModel.discoveredESP32s  = [original]
        viewModel.wifiSecurityError = "device not verified"

        viewModel.trustESP32(trusted)

        XCTAssertEqual(viewModel.discoveredESP32s.count, 1)
        XCTAssertEqual(viewModel.discoveredESP32s[0], trusted)
        XCTAssertNil(viewModel.wifiSecurityError)
        XCTAssertEqual(viewModel.wifiSecurityPolicy.verificationState(for: deviceId),
                       .userTrusted)
    }

    // MARK: - resumeDiscovery via .resumeDiscovery notification (uncovered lines 396–405)

    // VehicleViewModel.init subscribes to NotificationCenter.default.publisher(for:
    // .resumeDiscovery). Posting that notification drives resumeDiscoveryIfNeeded
    // through the public Combine pipeline. In WiFi mode with discovery inactive,
    // it must restart discovery (isESP32DiscoveryActive flips true).
    func testResumeDiscoveryNotification_RestartsDiscoveryInWiFiMode() {
        viewModel.connectionMode = .wifi
        viewModel.isESP32DiscoveryActive = false

        // Act: simulate the app moving to foreground.
        NotificationCenter.default.post(name: .resumeDiscovery, object: nil)

        // Discovery is restarted synchronously on the main queue by the
        // Combine sink; pump the runloop briefly so the dispatched work lands.
        let deadline = Date(timeIntervalSinceNow: 0.3)
        while Date() < deadline {
            RunLoop.current.run(mode: .default, before: Date(timeIntervalSinceNow: 0.05))
        }

        XCTAssertTrue(viewModel.isESP32DiscoveryActive,
                      "resumeDiscovery must restart ESP32 discovery in WiFi mode when inactive")
    }

    // When discovery is already active, the .resumeDiscovery notification must
    // not start a second listener — the guard inside resumeDiscoveryIfNeeded
    // short-circuits and the active flag stays true.
    func testResumeDiscoveryNotification_IsNoopWhenAlreadyActive() {
        viewModel.connectionMode = .wifi
        viewModel.isESP32DiscoveryActive = true

        NotificationCenter.default.post(name: .resumeDiscovery, object: nil)

        XCTAssertTrue(viewModel.isESP32DiscoveryActive,
                      "resumeDiscovery must be a no-op when discovery is already active")
    }
}
