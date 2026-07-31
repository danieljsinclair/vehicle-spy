import Foundation
import CryptoKit
import XCTest
@testable import VehicleSim

final class DiscoveryDecoderTests: XCTestCase {

    // MARK: - Fixtures

    private let deviceId = Data([
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
    ])

    private let nonce = Data([
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22
    ])

    private let timestamp: UInt64 = 1_700_000_000
    private let canPort: UInt16 = 3333
    private let otaPort: UInt16 = 80
    private let zeroSignature = Data(repeating: 0x00, count: 64)

    // MARK: - Helpers

    /// Build a raw unsigned discovery packet using the same wire format
    /// the ESP32 emits: magic "VSIM" + version 1 + type 1 + deviceId (16)
    /// + nonce (8) + timestamp (8 BE) + canPort (2 BE) + otaPort (2 BE)
    /// + 64-byte zero signature = 106 bytes total.
    private func makeUnsignedPacketData() -> Data {
        let packet = DiscoveryPacket(
            deviceId: deviceId,
            nonce: nonce,
            timestamp: timestamp,
            canPort: canPort,
            otaPort: otaPort,
            signature: zeroSignature
        )
        return packet.data
    }

    // MARK: - Unsigned decode contract

    func testUnsignedPacketDecodesSuccessfully() throws {
        let decoder = DiscoveryDecoder(verifier: DiscoveryVerifier(publicKey: nil))
        let packetData = makeUnsignedPacketData()
        let remoteAddress = "192.168.1.100"

        let discovered = try decoder.decode(packetData, remoteAddress: remoteAddress)

        XCTAssertEqual(discovered.address, remoteAddress)
        XCTAssertEqual(discovered.port, DiscoveryConstants.broadcastPort)
        XCTAssertEqual(discovered.timestamp, timestamp)
        XCTAssertEqual(discovered.deviceId, deviceId)
    }

    func testUnsignedPacketCanPortIs3333() throws {
        let decoder = DiscoveryDecoder(verifier: DiscoveryVerifier(publicKey: nil))
        let packetData = makeUnsignedPacketData()

        let discovered = try decoder.decode(packetData, remoteAddress: "10.0.0.1")

        XCTAssertEqual(discovered.canPort, 3333)
    }

    func testUnsignedPacketOtaPortIs80() throws {
        // OTA port is not carried on DiscoveredESP32, so verify it at the
        // parse layer (which is what the decoder calls internally).
        let packetData = makeUnsignedPacketData()
        let packet = try DiscoveryPacket.parse(packetData)

        XCTAssertEqual(packet.otaPort, 80)
    }

    func testZeroSignatureDoesNotDropUnsignedPacket() throws {
        // With a nil publicKey the verifier returns early (unsigned path),
        // so an all-zero signature must NOT cause the packet to be rejected.
        let decoder = DiscoveryDecoder(verifier: DiscoveryVerifier(publicKey: nil))
        let packetData = makeUnsignedPacketData()

        XCTAssertNoThrow(try decoder.decode(packetData, remoteAddress: "10.0.0.1"))
    }

    // MARK: - Malformed packets still fail through the decoder

    func testTruncatedDataThrows() {
        let decoder = DiscoveryDecoder(verifier: DiscoveryVerifier(publicKey: nil))
        let shortData = Data(repeating: 0, count: 50)

        XCTAssertThrowsError(try decoder.decode(shortData, remoteAddress: "10.0.0.1")) { error in
            XCTAssertEqual(error as? DiscoveryPacketError, .invalidLength)
        }
    }
}
