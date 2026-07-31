import Foundation
import Network
import XCTest
@testable import VehicleSim

// Pins the NWEndpoint.hostAddressString resolution contract — the pure logic
// that turns a discovered peer's NWEndpoint into the `address` string stamped
// onto every DiscoveredESP32 (and subsequently used to connect). A wrong branch
// here yields a wrong host → connection failure, so each endpoint family is
// asserted independently. The helper was lifted from `private` to internal
// (still module-internal, not public) so @testable can reach it without a
// behaviour-changing refactor.

final class ESP32DiscoveryHostAddressTests: XCTestCase {

    // MARK: - .hostPort with each host family

    // ipv4 host → its debug description (the canonical IPv4 string form).
    // Constructs a real NWEndpoint.Host.ipv4 from a literal address so the
    // assertion exercises the genuine SDK value, not a hand-rolled string.
    func testHostPortIPv4ResolvesToAddressDebugDescription() throws {
        let ipv4 = try XCTUnwrap(IPv4Address("192.168.1.42"))
        let endpoint = NWEndpoint.hostPort(host: .ipv4(ipv4), port: 3335)

        // Intent: the resolved host is the IPv4 address (not "unknown", not a
        // port-suffixed form). Assert the address substring is present rather
        // than the exact debugDescription format, which the SDK owns.
        XCTAssertTrue(endpoint.hostAddressString.contains("192.168.1.42"),
                      "ipv4 host should resolve to its address; got \(endpoint.hostAddressString)")
        XCTAssertNotEqual(endpoint.hostAddressString, "unknown")
    }

    // ipv6 host → its debug description. A distinct family from ipv4 so a
    // misroute (e.g. ipv6 falling into the ipv4 branch) is caught.
    func testHostPortIPv6ResolvesToAddressDebugDescription() throws {
        let ipv6 = try XCTUnwrap(IPv6Address("fd00::1"))
        let endpoint = NWEndpoint.hostPort(host: .ipv6(ipv6), port: 3335)

        XCTAssertTrue(endpoint.hostAddressString.lowercased().contains("fd00"),
                      "ipv6 host should resolve to its address; got \(endpoint.hostAddressString)")
        XCTAssertNotEqual(endpoint.hostAddressString, "unknown")
    }

    // .name host → the name string verbatim. This is the hostname (DNS) branch,
    // distinct from both numeric families.
    func testHostPortNameResolvesToTheName() {
        let endpoint = NWEndpoint.hostPort(host: .name("esp32-bridge.local", nil), port: 3335)

        XCTAssertEqual(endpoint.hostAddressString, "esp32-bridge.local")
    }

    // MARK: - "unknown" fallbacks

    // A non-.hostPort endpoint shape (e.g. service) cannot yield a host → the
    // outer guard returns "unknown". Pins the outer-else fallback.
    func testNonHostPortEndpointResolvesUnknown() {
        // .service is an endpoint shape with no directly-resolvable host.
        let endpoint = NWEndpoint.service(name: "esp32", type: "_tcp", domain: "local", interface: nil)

        XCTAssertEqual(endpoint.hostAddressString, "unknown")
    }

    // MARK: - family-independence (intent: each family resolves distinctly)

    // The three resolvable families produce DISTINCT outputs for distinct
    // inputs — guards against a "everything returns the same value" regression
    // that per-family asserts above wouldn't catch on their own.
    func testDistinctHostsProduceDistinctResolutions() throws {
        let v4 = try XCTUnwrap(IPv4Address("10.0.0.7"))
        let v6 = try XCTUnwrap(IPv6Address("fd00::2"))

        let v4Endpoint = NWEndpoint.hostPort(host: .ipv4(v4), port: 3335)
        let v6Endpoint = NWEndpoint.hostPort(host: .ipv6(v6), port: 3335)
        let nameEndpoint = NWEndpoint.hostPort(host: .name("host-a", nil), port: 3335)

        let resolved: Set<String> = [v4Endpoint.hostAddressString,
                                     v6Endpoint.hostAddressString,
                                     nameEndpoint.hostAddressString]
        // Three distinct inputs → three distinct resolutions (none collapsed).
        XCTAssertEqual(resolved.count, 3,
                       "distinct host families should resolve distinctly; got \(resolved)")
    }
}
