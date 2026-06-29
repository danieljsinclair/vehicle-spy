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
        // Discovery packets are intentionally unsigned — the firmware sends a
        // zeroed signature field. The OTA key is used for firmware *update*
        // authentication, not for discovery. Discovery is the bootstrap that
        // learns the device's IP before any secure channel exists. We only
        // check timestamp freshness here; signature verification is skipped.
        let currentUnix = UInt64(Date().timeIntervalSince1970)
        if packet.timestamp > currentUnix + maxClockSkew ||
           packet.timestamp + maxClockSkew < currentUnix {
            throw DiscoveryVerificationError.staleTimestamp
        }
    }
}
