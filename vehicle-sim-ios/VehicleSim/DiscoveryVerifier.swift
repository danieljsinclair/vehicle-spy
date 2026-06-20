import Foundation
import CryptoKit

// MARK: - Verification Errors

enum DiscoveryVerificationError: Error, Equatable {
    case unsupportedVersion
    case invalidSignature
    case untrustedDevice
    case staleTimestamp
}

// MARK: - Verifier

struct DiscoveryVerifier {
    private let publicKey: Curve25519.Signing.PublicKey?
    private let maxClockSkew: UInt64 // seconds

    init(publicKey: Curve25519.Signing.PublicKey? = nil, maxClockSkew: UInt64 = 300) {
        self.publicKey = publicKey
        self.maxClockSkew = maxClockSkew
    }

    func verify(_ packet: DiscoveryPacket) throws {
        if let publicKey {
            let payload = packet.signedPayload
            guard publicKey.isValidSignature(packet.signature, for: payload) else {
                throw DiscoveryVerificationError.invalidSignature
            }
        }

        let currentUnix = UInt64(Date().timeIntervalSince1970)
        if packet.timestamp > currentUnix + maxClockSkew ||
           packet.timestamp + maxClockSkew < currentUnix {
            throw DiscoveryVerificationError.staleTimestamp
        }
    }
}
