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

    // Two policies built from the SAME non-nil public key are equal — pins the
    // Curve25519 key-comparison branch (rawRepresentation equality), which the
    // default nil-key equality test above does not reach.
    func testPolicyEqualityWithSamePublicKey() {
        let keyPair = Curve25519.Signing.PrivateKey()
        let policy1 = WiFiSecurityPolicy(publicKey: keyPair.publicKey)
        let policy2 = WiFiSecurityPolicy(publicKey: keyPair.publicKey)

        XCTAssertEqual(policy1, policy2, "policies with the same public key should be equal")
    }

    // Two policies with DIFFERENT public keys are NOT equal — pins the
    // inequality fallback for two present-but-distinct keys.
    func testPolicyInequalityWithDifferentPublicKeys() {
        let keyA = Curve25519.Signing.PrivateKey()
        let keyB = Curve25519.Signing.PrivateKey()
        let policy1 = WiFiSecurityPolicy(publicKey: keyA.publicKey)
        let policy2 = WiFiSecurityPolicy(publicKey: keyB.publicKey)

        XCTAssertNotEqual(policy1, policy2, "policies with different public keys should not be equal")
    }

    // A policy with a key and a policy without one are NOT equal — pins the
    // one-nil / one-present (mixed) default branch of the key comparison.
    func testPolicyInequalityWhenOneHasKeyAndOtherDoesNot() {
        let keyPair = Curve25519.Signing.PrivateKey()
        let withKey = WiFiSecurityPolicy(publicKey: keyPair.publicKey)
        let withoutKey = WiFiSecurityPolicy(publicKey: nil)

        XCTAssertNotEqual(withKey, withoutKey,
                          "a policy with a key must not equal one without")
    }

    // MARK: - Error Descriptions

    func testWiFiSecurityPolicyErrorDescriptions() {
        XCTAssertEqual(WiFiSecurityPolicyError.deviceNotVerified.errorDescription, "ESP32 device identity could not be verified. Connection refused for security.")
        XCTAssertEqual(WiFiSecurityPolicyError.noPublicKeyConfigured.errorDescription, "No verification public key is configured. Cannot verify ESP32 identity.")
    }

    // MARK: - Verify and Update Method

    func testVerifyAndUpdateWithNoPublicKeyReturnsFalse() {
        var policy = WiFiSecurityPolicy(publicKey: nil)
        let esp32 = makeDiscoveredESP32(deviceId: deviceIdA)
        let timestamp = UInt64(Date().timeIntervalSince1970)
        let packet = DiscoveryPacket(
            deviceId: deviceIdA,
            nonce: Data(repeating: 0xAA, count: 8),
            timestamp: timestamp,
            canPort: 3333,
            otaPort: 3334,
            signature: Data(repeating: 0xAB, count: 64)
        )

        let result = policy.verifyAndUpdate(discovered: esp32, packet: packet)

        XCTAssertFalse(result, "Should return false when no public key is configured")
        XCTAssertEqual(policy.verificationState(for: deviceIdA), .unverified)
    }

    func testVerifyAndUpdateWithValidTimestampMarksVerified() {
        let keyPair = Curve25519.Signing.PrivateKey()
        var policy = WiFiSecurityPolicy(publicKey: keyPair.publicKey)
        let esp32 = makeDiscoveredESP32(deviceId: deviceIdA)
        let timestamp = UInt64(Date().timeIntervalSince1970)
        let packet = DiscoveryPacket(
            deviceId: deviceIdA,
            nonce: Data(repeating: 0xAA, count: 8),
            timestamp: timestamp,
            canPort: 3333,
            otaPort: 3334,
            signature: Data(repeating: 0, count: 64)
        )

        let result = policy.verifyAndUpdate(discovered: esp32, packet: packet)

        XCTAssertTrue(result, "Should verify successfully with valid timestamp")
        XCTAssertEqual(policy.verificationState(for: deviceIdA), .verified)
    }

    func testVerifyAndUpdateWithStaleTimestampMarksRejected() {
        let keyPair = Curve25519.Signing.PrivateKey()
        var policy = WiFiSecurityPolicy(publicKey: keyPair.publicKey)
        let esp32 = makeDiscoveredESP32(deviceId: deviceIdA)
        let staleTimestamp = UInt64(Date().timeIntervalSince1970) - 1000 // Over 5 mins ago
        let packet = DiscoveryPacket(
            deviceId: deviceIdA,
            nonce: Data(repeating: 0xAA, count: 8),
            timestamp: staleTimestamp,
            canPort: 3333,
            otaPort: 3334,
            signature: Data(repeating: 0, count: 64)
        )

        let result = policy.verifyAndUpdate(discovered: esp32, packet: packet)

        XCTAssertFalse(result, "Should reject stale timestamp")
        XCTAssertEqual(policy.verificationState(for: deviceIdA), .rejected)
    }

    func testVerifyAndUpdateWithFutureTimestampMarksRejected() {
        let keyPair = Curve25519.Signing.PrivateKey()
        var policy = WiFiSecurityPolicy(publicKey: keyPair.publicKey)
        let esp32 = makeDiscoveredESP32(deviceId: deviceIdA)
        let futureTimestamp = UInt64(Date().timeIntervalSince1970) + 1000 // Over 5 mins ahead
        let packet = DiscoveryPacket(
            deviceId: deviceIdA,
            nonce: Data(repeating: 0xAA, count: 8),
            timestamp: futureTimestamp,
            canPort: 3333,
            otaPort: 3334,
            signature: Data(repeating: 0, count: 64)
        )

        let result = policy.verifyAndUpdate(discovered: esp32, packet: packet)

        XCTAssertFalse(result, "Should reject future timestamp")
        XCTAssertEqual(policy.verificationState(for: deviceIdA), .rejected)
    }

    func testVerifyAndUpdateUpdatesExistingState() {
        let keyPair = Curve25519.Signing.PrivateKey()
        var policy = WiFiSecurityPolicy(publicKey: keyPair.publicKey)
        let esp32 = makeDiscoveredESP32(deviceId: deviceIdA)

        // First mark as rejected
        policy.markRejected(deviceId: deviceIdA)
        XCTAssertEqual(policy.verificationState(for: deviceIdA), .rejected)

        // Then verify and update with valid timestamp
        let timestamp = UInt64(Date().timeIntervalSince1970)
        let packet = DiscoveryPacket(
            deviceId: deviceIdA,
            nonce: Data(repeating: 0xAA, count: 8),
            timestamp: timestamp,
            canPort: 3333,
            otaPort: 3334,
            signature: Data(repeating: 0, count: 64)
        )

        let result = policy.verifyAndUpdate(discovered: esp32, packet: packet)

        XCTAssertTrue(result)
        XCTAssertEqual(policy.verificationState(for: deviceIdA), .verified, "State should be updated to verified")
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
