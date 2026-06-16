import Foundation
import CryptoKit

// MARK: - Trust Store

protocol DiscoveryTrustStore: Sendable {
    func isTrusted(deviceId: Data) -> Bool
}

struct StaticDiscoveryTrustStore: DiscoveryTrustStore {
    let trustedDeviceIds: Set<Data>

    func isTrusted(deviceId: Data) -> Bool {
        trustedDeviceIds.contains(deviceId)
    }
}

// MARK: - Verification Errors

enum DiscoveryVerificationError: Error, Equatable {
    case unsupportedVersion
    case invalidSignature
    case untrustedDevice
    case staleTimestamp
}

// MARK: - Verifier

struct DiscoveryVerifier {
    private let trustStore: any DiscoveryTrustStore
    private let publicKey: Curve25519.Signing.PublicKey
    private let maxClockSkew: UInt64 // seconds

    init(
        trustStore: any DiscoveryTrustStore,
        publicKey: Curve25519.Signing.PublicKey,
        maxClockSkew: UInt64 = 300
    ) {
        self.trustStore = trustStore
        self.publicKey = publicKey
        self.maxClockSkew = maxClockSkew
    }

    func verify(_ packet: DiscoveryPacket) throws {
        // 1. Verify Ed25519 signature over the signed payload
        let payload = packet.signedPayload
        guard publicKey.isValidSignature(packet.signature, for: payload) else {
            throw DiscoveryVerificationError.invalidSignature
        }

        // 2. Check device trust — must be in the trust store to be accepted
        guard trustStore.isTrusted(deviceId: packet.deviceId) else {
            throw DiscoveryVerificationError.untrustedDevice
        }

        // 3. Anti-replay: reject packets with timestamp too far from local clock
        let currentUnix = UInt64(Date().timeIntervalSince1970)
        if packet.timestamp > currentUnix + maxClockSkew ||
           packet.timestamp + maxClockSkew < currentUnix {
            throw DiscoveryVerificationError.staleTimestamp
        }
    }
}
