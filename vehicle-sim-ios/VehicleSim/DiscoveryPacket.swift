import Foundation
import CryptoKit

// MARK: - Constants

enum DiscoveryConstants {
    /// Magic bytes: "VSIM" (0x56 0x53 0x49 0x4D)
    static let magic: [UInt8] = [0x56, 0x53, 0x49, 0x4D]

    /// Current protocol version
    static let currentVersion: UInt8 = 1

    /// Packet type: discovery broadcast
    static let packetTypeDiscovery: UInt8 = 1

    /// Size of the device ID field in bytes
    static let deviceIdLength = 16

    /// Size of the nonce field in bytes
    static let nonceLength = 8

    /// Size of Ed25519 signature in bytes
    static let signatureLength = 64

    /// Header: magic(4) + version(1) + type(1) + deviceId(16) + nonce(8) + timestamp(8) + canPort(2) + otaPort(2) = 42
    static let headerLength = 42

    /// Minimum total packet length: header + signature
    static let minimumLength = headerLength + signatureLength  // 106

    /// UDP port for discovery broadcasts
    static let broadcastPort: UInt16 = 3335

    /// Default CAN TCP port on ESP32
    static let defaultCANPort: UInt16 = 3333

    /// OTA update port on ESP32
    static let otaPort: UInt16 = 3334
}

// MARK: - Errors

enum DiscoveryPacketError: Error, Equatable {
    case invalidLength
    case unsupportedVersion
    case unsupportedPacketType
    case invalidHeader
    case invalidTimestamp
    case invalidPort
    case invalidSignature
}

// MARK: - Packet

struct DiscoveryPacket: Equatable {
    let deviceId: Data
    let nonce: Data
    let timestamp: UInt64
    let canPort: UInt16
    let otaPort: UInt16
    let signature: Data

    /// The data that is signed (everything except the signature itself).
    var signedPayload: Data {
        var payload = Data(capacity: DiscoveryConstants.headerLength)
        payload.append(contentsOf: DiscoveryConstants.magic)
        payload.append(DiscoveryConstants.currentVersion)
        payload.append(DiscoveryConstants.packetTypeDiscovery)
        payload.append(deviceId)
        payload.append(nonce)
        payload.append(timestamp.bigEndianBytes)
        payload.append(canPort.bigEndianBytes)
        payload.append(otaPort.bigEndianBytes)
        return payload
    }

    /// Full wire-format packet: signedPayload + signature.
    var data: Data {
        var packet = signedPayload
        packet.append(signature)
        return packet
    }

    /// Parse a discovery packet from raw UDP data.
    static func parse(_ data: Data) throws -> DiscoveryPacket {
        guard data.count >= DiscoveryConstants.minimumLength else {
            throw DiscoveryPacketError.invalidLength
        }

        let expectedMagic = DiscoveryConstants.magic
        for i in 0..<expectedMagic.count {
            guard data[i] == expectedMagic[i] else {
                throw DiscoveryPacketError.invalidHeader
            }
        }

        let version = data[4]
        guard version == DiscoveryConstants.currentVersion else {
            throw DiscoveryPacketError.unsupportedVersion
        }

        let packetType = data[5]
        guard packetType == DiscoveryConstants.packetTypeDiscovery else {
            throw DiscoveryPacketError.unsupportedPacketType
        }

        let deviceId = data.subdata(in: 6..<(6 + DiscoveryConstants.deviceIdLength))
        let nonceStart = 6 + DiscoveryConstants.deviceIdLength
        let nonce = data.subdata(in: nonceStart..<(nonceStart + DiscoveryConstants.nonceLength))

        let timestampStart = nonceStart + DiscoveryConstants.nonceLength
        let timestamp = UInt64(bigEndianBytes: data[timestampStart..<(timestampStart + 8)])

        guard timestamp > 0 else {
            throw DiscoveryPacketError.invalidTimestamp
        }

        let canPortStart = timestampStart + 8
        let canPort = UInt16(bigEndianBytes: data[canPortStart..<(canPortStart + 2)])

        guard canPort > 0 else {
            throw DiscoveryPacketError.invalidPort
        }

        let otaPortStart = canPortStart + 2
        let otaPort = UInt16(bigEndianBytes: data[otaPortStart..<(otaPortStart + 2)])

        let sigStart = DiscoveryConstants.headerLength
        let signature = data.subdata(in: sigStart..<(sigStart + DiscoveryConstants.signatureLength))

        return DiscoveryPacket(
            deviceId: deviceId,
            nonce: nonce,
            timestamp: timestamp,
            canPort: canPort,
            otaPort: otaPort,
            signature: signature
        )
    }
}

// MARK: - Big-endian helpers

extension UInt16 {
    init(bigEndianBytes data: Data) {
        self = data.withUnsafeBytes { ptr in
            UInt16(bigEndian: ptr.load(as: UInt16.self))
        }
    }

    init(bigEndianBytes first: UInt8, _ second: UInt8) {
        self = (UInt16(first) << 8) | UInt16(second)
    }

    var bigEndianBytes: Data {
        var value = self.bigEndian
        return Data(bytes: &value, count: MemoryLayout<UInt16>.size)
    }
}

extension UInt64 {
    init(bigEndianBytes data: Data) {
        self = data.withUnsafeBytes { ptr in
            UInt64(bigEndian: ptr.load(as: UInt64.self))
        }
    }

    init(bigEndianBytes b0: UInt8, _ b1: UInt8, _ b2: UInt8, _ b3: UInt8,
         _ b4: UInt8, _ b5: UInt8, _ b6: UInt8, _ b7: UInt8) {
        self = (UInt64(b0) << 56)
            | (UInt64(b1) << 48)
            | (UInt64(b2) << 40)
            | (UInt64(b3) << 32)
            | (UInt64(b4) << 24)
            | (UInt64(b5) << 16)
            | (UInt64(b6) << 8)
            | UInt64(b7)
    }

    var bigEndianBytes: Data {
        var value = self.bigEndian
        return Data(bytes: &value, count: MemoryLayout<UInt64>.size)
    }
}
