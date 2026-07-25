import Foundation
import XCTest
@testable import VehicleSim

/// Pins the user-facing error strings for `ESP32DiscoveryListenerError`.
/// These are pure computed-property values (no networking, no UI, no device
/// dependencies), so they are deterministic and fast to assert.
final class ESP32DiscoveryListenerErrorTests: XCTestCase {

    // MARK: - invalidPublicKey

    func testInvalidPublicKeyDescriptionIsStable() {
        let error = ESP32DiscoveryListenerError.invalidPublicKey

        XCTAssertEqual(error.errorDescription,
                       "The configured ESP32 discovery public key is invalid.",
                       "invalidPublicKey must expose a stable, user-facing description")
    }

    // MARK: - listenerFailed

    func testListenerFailedWrapsUnderlyingErrorDescription() {
        let underlying = NSError(domain: "TestDomain",
                                 code: 42,
                                 userInfo: [NSLocalizedDescriptionKey: "socket closed"])
        let error = ESP32DiscoveryListenerError.listenerFailed(underlying)

        XCTAssertEqual(error.errorDescription,
                       "Discovery listener failed: socket closed",
                       "listenerFailed should surface the wrapped error's localizedDescription")
    }

    func testListenerFailedWithEmptyDescriptionStillWraps() {
        // Edge case: underlying error has an explicitly empty localizedDescription.
        let underlying = NSError(domain: "TestDomain",
                                 code: 1,
                                 userInfo: [NSLocalizedDescriptionKey: ""])
        let error = ESP32DiscoveryListenerError.listenerFailed(underlying)

        XCTAssertEqual(error.errorDescription,
                       "Discovery listener failed: ",
                       "listenerFailed should still wrap a blank underlying description")
    }
}
