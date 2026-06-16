import Foundation
import CryptoKit
import XCTest

@testable import VehicleSim

final class DiscoveryVerifierTests: XCTestCase {

    private let deviceId = Data([
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
    ])

    private let nonce = Data([0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22])
    private let canPort: UInt16 = 3333

    private var keyPair: Curve25519.Signing.PrivateKey!
    private var publicKey: Curve25519.Signing.PublicKey!

    override func setUp() {
        super.setUp()
        keyPair = Curve25519.Signing.PrivateKey()
        publicKey = keyPair.publicKey
    }

    // MARK: - Untrusted Device

    func testVerifyRejectsUntrustedDevice() {
        let trustStore = StaticDiscoveryTrustStore(trustedDeviceIds: [])
        let verifier = DiscoveryVerifier(trustStore: trustStore, publicKey: publicKey)

        let packet = makePacket(deviceId: deviceId, timestamp: UInt64(Date().timeIntervalSince1970))

        XCTAssertThrowsError(try verifier.verify(packet)) { error in
            XCTAssertEqual(error as? DiscoveryVerificationError, .untrustedDevice)
        }
    }

    // MARK: - Invalid Signature

    func testVerifyRejectsWrongSignature() {
        let trustStore = StaticDiscoveryTrustStore(trustedDeviceIds: [deviceId])
        let verifier = DiscoveryVerifier(trustStore: trustStore, publicKey: publicKey)

        let otherKeyPair = Curve25519.Signing.PrivateKey()
        let packet = makePacket(deviceId: deviceId,
                                timestamp: UInt64(Date().timeIntervalSince1970),
                                signingKey: otherKeyPair)

        XCTAssertThrowsError(try verifier.verify(packet)) { error in
            XCTAssertEqual(error as? DiscoveryVerificationError, .invalidSignature)
        }
    }

    // MARK: - Valid Signature + Trusted

    func testVerifyAcceptsValidTrustedPacket() throws {
        let trustStore = StaticDiscoveryTrustStore(trustedDeviceIds: [deviceId])
        let verifier = DiscoveryVerifier(trustStore: trustStore, publicKey: publicKey)

        let timestamp = UInt64(Date().timeIntervalSince1970)
        let packet = makePacket(deviceId: deviceId, timestamp: timestamp, signingKey: keyPair)

        try verifier.verify(packet)
    }

    // MARK: - Stale Timestamp (Anti-replay)

    func testVerifyRejectsStaleTimestamp() {
        let trustStore = StaticDiscoveryTrustStore(trustedDeviceIds: [deviceId])
        let verifier = DiscoveryVerifier(trustStore: trustStore, publicKey: publicKey, maxClockSkew: 60)

        let staleTimestamp = UInt64(Date().timeIntervalSince1970) - 600
        let packet = makePacket(deviceId: deviceId, timestamp: staleTimestamp, signingKey: keyPair)

        XCTAssertThrowsError(try verifier.verify(packet)) { error in
            XCTAssertEqual(error as? DiscoveryVerificationError, .staleTimestamp)
        }
    }

    func testVerifyRejectsFutureTimestamp() {
        let trustStore = StaticDiscoveryTrustStore(trustedDeviceIds: [deviceId])
        let verifier = DiscoveryVerifier(trustStore: trustStore, publicKey: publicKey, maxClockSkew: 60)

        let futureTimestamp = UInt64(Date().timeIntervalSince1970) + 600
        let packet = makePacket(deviceId: deviceId, timestamp: futureTimestamp, signingKey: keyPair)

        XCTAssertThrowsError(try verifier.verify(packet)) { error in
            XCTAssertEqual(error as? DiscoveryVerificationError, .staleTimestamp)
        }
    }

    // MARK: - Tamper Detection

    func testVerifyRejectsPayloadTamper() throws {
        let trustStore = StaticDiscoveryTrustStore(trustedDeviceIds: [deviceId])
        let verifier = DiscoveryVerifier(trustStore: trustStore, publicKey: publicKey)

        let timestamp = UInt64(Date().timeIntervalSince1970)
        let original = makePacket(deviceId: deviceId, timestamp: timestamp, signingKey: keyPair, canPort: 3333)

        let tamperedPacket = DiscoveryPacket(
            deviceId: original.deviceId,
            nonce: original.nonce,
            timestamp: original.timestamp,
            canPort: 9999,
            signature: original.signature
        )

        XCTAssertThrowsError(try verifier.verify(tamperedPacket)) { error in
            XCTAssertEqual(error as? DiscoveryVerificationError, .invalidSignature)
        }
    }

    // MARK: - Trust Store Logic

    func testTrustStoreAcceptsKnownDeviceId() {
        let store = StaticDiscoveryTrustStore(trustedDeviceIds: [deviceId])
        XCTAssertTrue(store.isTrusted(deviceId: deviceId))
    }

    func testTrustStoreRejectsUnknownDeviceId() {
        let unknownId = Data(repeating: 0xFF, count: 16)
        let store = StaticDiscoveryTrustStore(trustedDeviceIds: [deviceId])
        XCTAssertFalse(store.isTrusted(deviceId: unknownId))
    }

    // MARK: - Helpers

    private func makePacket(
        deviceId: Data,
        timestamp: UInt64,
        signingKey: Curve25519.Signing.PrivateKey? = nil,
        canPort: UInt16 = 3333
    ) -> DiscoveryPacket {
        let key = signingKey ?? keyPair

        let unsigned = DiscoveryPacket(
            deviceId: deviceId,
            nonce: nonce,
            timestamp: timestamp,
            canPort: canPort,
            signature: Data()
        )

        let payload = unsigned.signedPayload
        let signature = try! key.signature(for: payload)

        return DiscoveryPacket(
            deviceId: deviceId,
            nonce: nonce,
            timestamp: timestamp,
            canPort: canPort,
            signature: Data(signature)
        )
    }
}
