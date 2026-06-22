import Foundation
import CryptoKit
import XCTest

@testable import VehicleSim

// MARK: - Helpers

private func makeDiscoveredESP32(
    deviceId: Data = Data(repeating: 0x01, count: 16),
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

// MARK: - WiFi Security Policy Tests

final class WiFiSecurityPolicyTests: XCTestCase {

    private let deviceIdA = Data(repeating: 0xAA, count: 16)
    private let deviceIdB = Data(repeating: 0xBB, count: 16)

    // MARK: - Default Policy: Refuse Unverified

    func testDefaultPolicyRefusesUnverifiedDevice() {
        let policy = WiFiSecurityPolicy()
        let esp32 = makeDiscoveredESP32(deviceId: deviceIdA)

        XCTAssertThrowsError(try policy.allowConnection(discovered: esp32)) { error in
            XCTAssertEqual(error as? WiFiSecurityPolicyError, .deviceNotVerified)
        }
    }

    func testDefaultPolicyAllowsVerifiedDevice() {
        var policy = WiFiSecurityPolicy()
        let esp32 = makeDiscoveredESP32(deviceId: deviceIdA)
        policy.markVerified(deviceId: deviceIdA)

        XCTAssertNoThrow(try policy.allowConnection(discovered: esp32))
    }

    func testDefaultPolicyRefusesRejectedDevice() {
        var policy = WiFiSecurityPolicy()
        let esp32 = makeDiscoveredESP32(deviceId: deviceIdA)
        policy.markRejected(deviceId: deviceIdA)

        XCTAssertThrowsError(try policy.allowConnection(discovered: esp32)) { error in
            XCTAssertEqual(error as? WiFiSecurityPolicyError, .deviceNotVerified)
        }
    }

    func testDefaultPolicyAllowsUserTrustedDevice() {
        var policy = WiFiSecurityPolicy()
        let esp32 = makeDiscoveredESP32(deviceId: deviceIdA)
        policy.markUserTrusted(deviceId: deviceIdA)

        XCTAssertNoThrow(try policy.allowConnection(discovered: esp32))
    }

    // MARK: - Per-Device State Isolation

    func testPolicyTracksDevicesIndependently() {
        var policy = WiFiSecurityPolicy()
        let esp32A = makeDiscoveredESP32(deviceId: deviceIdA)
        let esp32B = makeDiscoveredESP32(deviceId: deviceIdB)

        policy.markVerified(deviceId: deviceIdA)

        // A should be allowed
        XCTAssertNoThrow(try policy.allowConnection(discovered: esp32A))
        // B should still be refused
        XCTAssertThrowsError(try policy.allowConnection(discovered: esp32B))
    }

    // MARK: - Permissive Mode (Debug)

    func testPermissiveModeAllowsUnverified() {
        let policy = WiFiSecurityPolicy(
            publicKey: nil,
            deviceStates: [:],
            refuseUnverifiedByDefault: false
        )
        let esp32 = makeDiscoveredESP32(deviceId: deviceIdA)

        XCTAssertNoThrow(try policy.allowConnection(discovered: esp32))
    }

    // MARK: - Verification State Lookup

    func testVerificationStateReturnsUnverifiedForUnknownDevice() {
        let policy = WiFiSecurityPolicy()
        let state = policy.verificationState(for: deviceIdA)
        XCTAssertEqual(state, .unverified)
    }

    func testVerificationStateReturnsCorrectState() {
        var policy = WiFiSecurityPolicy()
        policy.markVerified(deviceId: deviceIdA)
        policy.markRejected(deviceId: deviceIdB)

        XCTAssertEqual(policy.verificationState(for: deviceIdA), .verified)
        XCTAssertEqual(policy.verificationState(for: deviceIdB), .rejected)
    }

    // MARK: - User Trust Override

    func testUserTrustOverridesRejection() {
        var policy = WiFiSecurityPolicy()
        let esp32 = makeDiscoveredESP32(deviceId: deviceIdA)

        policy.markRejected(deviceId: deviceIdA)
        XCTAssertThrowsError(try policy.allowConnection(discovered: esp32))

        // User explicitly trusts the device
        policy.markUserTrusted(deviceId: deviceIdA)
        XCTAssertNoThrow(try policy.allowConnection(discovered: esp32))
    }

    // MARK: - Equatable

    func testPolicyEquality() {
        let policy1 = WiFiSecurityPolicy()
        let policy2 = WiFiSecurityPolicy()
        XCTAssertEqual(policy1, policy2)
    }

    func testPolicyInequalityDifferentStates() {
        var policy1 = WiFiSecurityPolicy()
        let policy2 = WiFiSecurityPolicy()
        policy1.markVerified(deviceId: deviceIdA)
        XCTAssertNotEqual(policy1, policy2)
    }

    // MARK: - Data Hex Helper

    func testHexStringEncoding() {
        let data = Data([0x00, 0x0F, 0x10, 0xFF])
        XCTAssertEqual(data.hexString, "000f10ff")
    }

    func testHexStringEncodingEmpty() {
        let data = Data()
        XCTAssertEqual(data.hexString, "")
    }

    // MARK: - Verification State Enum

    func testAllVerificationStatesDistinct() {
        let states: [DeviceVerificationState] = [.unverified, .verified, .rejected, .userTrusted]
        let unique = Set(states)
        XCTAssertEqual(unique.count, 4, "All four states should be distinct")
    }
}
