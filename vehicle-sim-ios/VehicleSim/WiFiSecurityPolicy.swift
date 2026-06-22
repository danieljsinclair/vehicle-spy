import Foundation
import CryptoKit

// MARK: - WiFi Security Errors

enum WiFiSecurityPolicyError: Error, LocalizedError {
    case deviceNotVerified
    case noPublicKeyConfigured

    var errorDescription: String? {
        switch self {
        case .deviceNotVerified:
            return "ESP32 device identity could not be verified. Connection refused for security."
        case .noPublicKeyConfigured:
            return "No verification public key is configured. Cannot verify ESP32 identity."
        }
    }
}

// MARK: - Verification State

/// The verification state of a discovered ESP32 device.
enum DeviceVerificationState: Equatable {
    /// Device has not been verified yet.
    case unverified
    /// Device signature was checked and passed.
    case verified
    /// Device signature was checked and failed.
    case rejected
    /// Device was explicitly trusted by the user (one-time bypass).
    case userTrusted
}

// MARK: - WiFi Security Policy

/// Security policy for ESP32 WiFi connections.
///
/// By default, unverified devices are refused. A device must either:
/// 1. Pass signature verification during discovery (`.verified`), or
/// 2. Be explicitly trusted by the user (`.userTrusted`)
///
/// This is a VALUE TYPE (struct) so it is safe to pass across threads.
/// The trusted device set is VALUE-TYPE based: callers materialise a new
/// copy when they mutate, or the ViewModel holds it as @Published.
struct WiFiSecurityPolicy {
    /// The Ed25519 public key used to verify ESP32 discovery packet signatures.
    /// When nil, NO devices can be cryptographically verified (all are unverified).
    let publicKey: Curve25519.Signing.PublicKey?

    /// Per-device-id verification state.  The key is the hex-encoded device ID.
    var deviceStates: [String: DeviceVerificationState]

    /// When true (the default), unverified devices are refused.
    /// When false, the policy allows unverified devices (insecure, for debugging only).
    let refuseUnverifiedByDefault: Bool

    init(
        publicKey: Curve25519.Signing.PublicKey? = nil,
        deviceStates: [String: DeviceVerificationState] = [:],
        refuseUnverifiedByDefault: Bool = true
    ) {
        self.publicKey = publicKey
        self.deviceStates = deviceStates
        self.refuseUnverifiedByDefault = refuseUnverifiedByDefault
    }

    /// Return the verification state for a discovered device.
    func verificationState(for deviceId: Data) -> DeviceVerificationState {
        let hex = deviceId.hexString
        return deviceStates[hex] ?? .unverified
    }

    /// Record that a device passed signature verification.
    mutating func markVerified(deviceId: Data) {
        deviceStates[deviceId.hexString] = .verified
    }

    /// Record that a device failed signature verification.
    mutating func markRejected(deviceId: Data) {
        deviceStates[deviceId.hexString] = .rejected
    }

    /// Record that a device was explicitly trusted by the user.
    mutating func markUserTrusted(deviceId: Data) {
        deviceStates[deviceId.hexString] = .userTrusted
    }

    /// Check whether a discovered ESP32 is allowed to connect.
    /// Throws WiFiSecurityPolicyError if the device should be refused.
    func allowConnection(discovered: DiscoveredESP32) throws {
        let state = verificationState(for: discovered.deviceId)
        switch state {
        case .verified, .userTrusted:
            return // explicitly allowed
        case .rejected:
            throw WiFiSecurityPolicyError.deviceNotVerified
        case .unverified:
            if refuseUnverifiedByDefault {
                throw WiFiSecurityPolicyError.deviceNotVerified
            }
            // allowed through because policy is in permissive mode (debugging)
            return
        }
    }

    /// Attempt to verify a discovered device's packet signature.
    /// If the signature is valid, the device is marked as .verified.
    /// Returns true if verification succeeded.
    mutating func verifyAndUpdate(discovered: DiscoveredESP32, packet: DiscoveryPacket) -> Bool {
        guard let publicKey = publicKey else {
            // Cannot verify without a key; device remains unverified
            return false
        }

        do {
            try DiscoveryVerifier(publicKey: publicKey).verify(packet)
            markVerified(deviceId: discovered.deviceId)
            return true
        } catch {
            markRejected(deviceId: discovered.deviceId)
            return false
        }
    }
}

// MARK: - Equatable
/// Manual Equatable: Curve25519.Signing.PublicKey does not conform to Equatable,
/// so we compare the raw data representation.
extension WiFiSecurityPolicy: Equatable {
    static func == (lhs: WiFiSecurityPolicy, rhs: WiFiSecurityPolicy) -> Bool {
        let keysEqual: Bool
        switch (lhs.publicKey, rhs.publicKey) {
        case (nil, nil):
            keysEqual = true
        case (let a?, let b?):
            keysEqual = a.rawRepresentation == b.rawRepresentation
        default:
            keysEqual = false
        }
        return keysEqual
            && lhs.deviceStates == rhs.deviceStates
            && lhs.refuseUnverifiedByDefault == rhs.refuseUnverifiedByDefault
    }
}

// MARK: - Data hex encoding helper

extension Data {
    var hexString: String {
        map { String(format: "%02x", $0) }.joined()
    }
}
