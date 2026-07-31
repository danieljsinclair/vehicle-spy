import Foundation

// MARK: - Discovery Decoder

struct DiscoveryDecoder {
    private let verifier: DiscoveryVerifier

    init(verifier: DiscoveryVerifier) {
        self.verifier = verifier
    }

    /// Decode a discovery broadcast into a `DiscoveredESP32`.
    ///
    /// This is the pure, network-free decode path: it parses the wire
    /// packet, verifies it, and produces a value object. The caller
    /// supplies the sender's address as a plain string so the decoder
    /// has zero Network.framework dependency.
    ///
    /// - Parameters:
    ///   - data: Raw UDP payload bytes.
    ///   - remoteAddress: Resolved host address of the sender.
    /// - Returns: A fully-populated `DiscoveredESP32`.
    /// - Throws: `DiscoveryPacketError` or `DiscoveryVerificationError`
    ///     on malformed or unverified packets.
    func decode(_ data: Data, remoteAddress: String) throws -> DiscoveredESP32 {
        let packet = try DiscoveryPacket.parse(data)
        try verifier.verify(packet)

        return DiscoveredESP32(
            deviceId: packet.deviceId,
            address: remoteAddress,
            port: DiscoveryConstants.broadcastPort,
            canPort: packet.canPort,
            timestamp: packet.timestamp,
            receivedAt: Date()
        )
    }
}
