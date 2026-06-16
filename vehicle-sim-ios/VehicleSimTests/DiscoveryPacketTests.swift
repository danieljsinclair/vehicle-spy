import Foundation
import CryptoKit
import XCTest

@testable import VehicleSim

final class DiscoveryPacketTests: XCTestCase {

    // MARK: - Test Data

    private let validDeviceId = Data([
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
    ])

    private let validNonce = Data([
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22
    ])

    private let validTimestamp: UInt64 = 1_700_000_000
    private let validCanPort: UInt16 = 3333
    private let validSignature = Data(repeating: 0xAB, count: 64)

    // MARK: - Packet Construction

    func testSignedPayloadContainsHeaderFields() {
        let packet = DiscoveryPacket(
            deviceId: validDeviceId,
            nonce: validNonce,
            timestamp: validTimestamp,
            canPort: validCanPort,
            signature: validSignature
        )

        let payload = packet.signedPayload

        XCTAssertEqual(payload.count, DiscoveryConstants.headerLength)
        XCTAssertEqual(Array(payload[0..<4]), DiscoveryConstants.magic)
        XCTAssertEqual(payload[4], 1)
        XCTAssertEqual(payload[5], 1)
        XCTAssertEqual(Array(payload[6..<22]), Array(validDeviceId))
    }

    func testPacketDataIncludesSignature() {
        let packet = DiscoveryPacket(
            deviceId: validDeviceId,
            nonce: validNonce,
            timestamp: validTimestamp,
            canPort: validCanPort,
            signature: validSignature
        )

        let data = packet.data
        XCTAssertEqual(data.count, DiscoveryConstants.minimumLength)

        let sigBytes = data.subdata(in: DiscoveryConstants.headerLength..<DiscoveryConstants.minimumLength)
        XCTAssertEqual(sigBytes, validSignature)
    }

    // MARK: - Packet Parsing - Valid

    func testParseValidPacket() throws {
        let original = DiscoveryPacket(
            deviceId: validDeviceId,
            nonce: validNonce,
            timestamp: validTimestamp,
            canPort: validCanPort,
            signature: validSignature
        )

        let parsed = try DiscoveryPacket.parse(original.data)

        XCTAssertEqual(parsed.deviceId, validDeviceId)
        XCTAssertEqual(parsed.nonce, validNonce)
        XCTAssertEqual(parsed.timestamp, validTimestamp)
        XCTAssertEqual(parsed.canPort, validCanPort)
        XCTAssertEqual(parsed.signature, validSignature)
    }

    // MARK: - Packet Parsing - Errors

    func testParseTooShort() {
        let shortData = Data(repeating: 0, count: 50)
        XCTAssertThrowsError(try DiscoveryPacket.parse(shortData)) { error in
            XCTAssertEqual(error as? DiscoveryPacketError, .invalidLength)
        }
    }

    func testParseWrongMagic() {
        var data = buildValidPacketData()
        data[0] = 0xFF
        XCTAssertThrowsError(try DiscoveryPacket.parse(data)) { error in
            XCTAssertEqual(error as? DiscoveryPacketError, .invalidHeader)
        }
    }

    func testParseUnsupportedVersion() {
        var data = buildValidPacketData()
        data[4] = 99
        XCTAssertThrowsError(try DiscoveryPacket.parse(data)) { error in
            XCTAssertEqual(error as? DiscoveryPacketError, .unsupportedVersion)
        }
    }

    func testParseWrongPacketType() {
        var data = buildValidPacketData()
        data[5] = 2
        XCTAssertThrowsError(try DiscoveryPacket.parse(data)) { error in
            XCTAssertEqual(error as? DiscoveryPacketError, .unsupportedPacketType)
        }
    }

    func testParseZeroTimestamp() {
        var data = buildValidPacketData()
        for i in 22..<30 {
            data[i] = 0
        }
        XCTAssertThrowsError(try DiscoveryPacket.parse(data)) { error in
            XCTAssertEqual(error as? DiscoveryPacketError, .invalidTimestamp)
        }
    }

    func testParseZeroCanPort() {
        var data = buildValidPacketData()
        for i in 30..<32 {
            data[i] = 0
        }
        XCTAssertThrowsError(try DiscoveryPacket.parse(data)) { error in
            XCTAssertEqual(error as? DiscoveryPacketError, .invalidPort)
        }
    }

    // MARK: - Round-trip

    func testRoundTrip() throws {
        let original = DiscoveryPacket(
            deviceId: validDeviceId,
            nonce: validNonce,
            timestamp: validTimestamp,
            canPort: validCanPort,
            signature: validSignature
        )

        let parsed = try DiscoveryPacket.parse(original.data)
        let rebuilt = try DiscoveryPacket.parse(parsed.data)

        XCTAssertEqual(parsed, rebuilt)
    }

    // MARK: - Big-endian helpers

    func testUInt16BigEndianRoundTrip() {
        let values: [UInt16] = [0, 1, 255, 256, 3333, 58421, 65535]
        for value in values {
            let data = value.bigEndianBytes
            let parsed = UInt16(bigEndianBytes: data)
            XCTAssertEqual(parsed, value, "UInt16 round-trip failed for \(value)")
        }
    }

    func testUInt64BigEndianRoundTrip() {
        let values: [UInt64] = [0, 1, 255, 65535, 1_700_000_000, UInt64.max]
        for value in values {
            let data = value.bigEndianBytes
            let parsed = UInt64(bigEndianBytes: data)
            XCTAssertEqual(parsed, value, "UInt64 round-trip failed for \(value)")
        }
    }

    // MARK: - Constants

    func testConstants() {
        XCTAssertEqual(DiscoveryConstants.magic, [0x56, 0x53, 0x49, 0x4D])
        XCTAssertEqual(DiscoveryConstants.broadcastPort, 58421)
        XCTAssertEqual(DiscoveryConstants.defaultCANPort, 3333)
        XCTAssertEqual(DiscoveryConstants.otaPort, 3334)
        XCTAssertEqual(DiscoveryConstants.signatureLength, 64)
        XCTAssertEqual(DiscoveryConstants.deviceIdLength, 16)
        XCTAssertEqual(DiscoveryConstants.nonceLength, 8)
        XCTAssertEqual(DiscoveryConstants.headerLength, 40)
        XCTAssertEqual(DiscoveryConstants.minimumLength, 104)
    }

    // MARK: - Helpers

    private func buildValidPacketData() -> Data {
        var data = Data(capacity: DiscoveryConstants.minimumLength)
        data.append(contentsOf: DiscoveryConstants.magic)
        data.append(1)
        data.append(1)
        data.append(validDeviceId)
        data.append(validNonce)

        var ts = validTimestamp.bigEndian
        data.append(contentsOf: withUnsafeBytes(of: &ts) { Array($0) })

        var port = validCanPort.bigEndian
        data.append(contentsOf: withUnsafeBytes(of: &port) { Array($0) })

        data.append(validSignature)

        return data
    }
}
