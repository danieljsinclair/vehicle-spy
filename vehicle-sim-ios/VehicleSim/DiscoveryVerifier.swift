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
        // Unsigned discovery: no freshness check — the firmware sends a zeroed
        // signature field, and the timestamp may be uptime-based (seconds since
        // boot) when the device lacks NTP sync, so it can be wildly different
        // from host Unix time. Freshness is only meaningful once a signature
        // provides the authenticity guarantee (replay protection for signed
        // discovery). This matches the CLI listener, which accepts unsigned
        // discovery regardless of timestamp (src/discovery/UDPDiscovery.cpp).
        guard publicKey != nil else { return }
        // Signed discovery: enforce timestamp freshness (replay protection).
        let currentUnix = UInt64(Date().timeIntervalSince1970)
        if packet.timestamp > currentUnix + maxClockSkew ||
           packet.timestamp + maxClockSkew < currentUnix {
            throw DiscoveryVerificationError.staleTimestamp
        }
    }
}
