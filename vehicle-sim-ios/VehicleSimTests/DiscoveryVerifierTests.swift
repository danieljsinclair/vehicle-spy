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
    private let otaPort: UInt16 = 3334

    private var keyPair: Curve25519.Signing.PrivateKey!
    private var publicKey: Curve25519.Signing.PublicKey!

    override func setUp() {
        super.setUp()
        keyPair = Curve25519.Signing.PrivateKey()
        publicKey = keyPair.publicKey
    }

    // MARK: - No Public Key (unsigned acceptance)

    func testVerifyAcceptsUnsignedPacketWithNoPublicKey() throws {
        let verifier = DiscoveryVerifier(publicKey: nil)
        let timestamp = UInt64(Date().timeIntervalSince1970)
        let packet = makePacket(deviceId: deviceId, timestamp: timestamp, sign: false)

        // Should pass -- no public key configured means signature is not checked
        try verifier.verify(packet)
    }

    // MARK: - Invalid Signature

    func testVerifyRejectsWrongSignature() {
        let otherKeyPair = Curve25519.Signing.PrivateKey()
        let verifier = DiscoveryVerifier(publicKey: publicKey)

        let timestamp = UInt64(Date().timeIntervalSince1970)
        let packet = makePacket(deviceId: deviceId, timestamp: timestamp,
                                signingKey: otherKeyPair, sign: true)

        XCTAssertThrowsError(try verifier.verify(packet)) { error in
            XCTAssertEqual(error as? DiscoveryVerificationError, .invalidSignature)
        }
    }

    // MARK: - Valid Signature

    func testVerifyAcceptsValidSignedPacket() throws {
        let verifier = DiscoveryVerifier(publicKey: publicKey)
        let timestamp = UInt64(Date().timeIntervalSince1970)
        let packet = makePacket(deviceId: deviceId, timestamp: timestamp,
                                signingKey: keyPair, sign: true)

        try verifier.verify(packet)
    }

    // MARK: - Stale Timestamp (Anti-replay)

    func testVerifyRejectsStaleTimestamp() {
        let verifier = DiscoveryVerifier(publicKey: nil, maxClockSkew: 60)
        let staleTimestamp = UInt64(Date().timeIntervalSince1970) - 600
        let packet = makePacket(deviceId: deviceId, timestamp: staleTimestamp, sign: false)

        XCTAssertThrowsError(try verifier.verify(packet)) { error in
            XCTAssertEqual(error as? DiscoveryVerificationError, .staleTimestamp)
        }
    }

    func testVerifyRejectsFutureTimestamp() {
        let verifier = DiscoveryVerifier(publicKey: nil, maxClockSkew: 60)
        let futureTimestamp = UInt64(Date().timeIntervalSince1970) + 600
        let packet = makePacket(deviceId: deviceId, timestamp: futureTimestamp, sign: false)

        XCTAssertThrowsError(try verifier.verify(packet)) { error in
            XCTAssertEqual(error as? DiscoveryVerificationError, .staleTimestamp)
        }
    }

    // MARK: - Tamper Detection

    func testVerifyRejectsPayloadTamper() throws {
        let verifier = DiscoveryVerifier(publicKey: publicKey)
        let timestamp = UInt64(Date().timeIntervalSince1970)
        let original = makePacket(deviceId: deviceId, timestamp: timestamp,
                                  signingKey: keyPair, canPort: 3333, sign: true)

        let tamperedPacket = DiscoveryPacket(
            deviceId: original.deviceId,
            nonce: original.nonce,
            timestamp: original.timestamp,
            canPort: 9999,
            otaPort: original.otaPort,
            signature: original.signature
        )

        XCTAssertThrowsError(try verifier.verify(tamperedPacket)) { error in
            XCTAssertEqual(error as? DiscoveryVerificationError, .invalidSignature)
        }
    }

    // MARK: - Helpers

    private func makePacket(
        deviceId: Data,
        timestamp: UInt64,
        signingKey: Curve25519.Signing.PrivateKey? = nil,
        canPort: UInt16 = 3333,
        sign: Bool
    ) -> DiscoveryPacket {
        let unsigned = DiscoveryPacket(
            deviceId: deviceId,
            nonce: nonce,
            timestamp: timestamp,
            canPort: canPort,
            otaPort: otaPort,
            signature: Data()
        )

        if sign, let key = signingKey {
            let payload = unsigned.signedPayload
            let signature = try! key.signature(for: payload)
            return DiscoveryPacket(
                deviceId: deviceId,
                nonce: nonce,
                timestamp: timestamp,
                canPort: canPort,
                otaPort: otaPort,
                signature: Data(signature)
            )
        }

        return DiscoveryPacket(
            deviceId: deviceId,
            nonce: nonce,
            timestamp: timestamp,
            canPort: canPort,
            otaPort: otaPort,
            signature: Data(repeating: 0, count: 64)
        )
    }
}
