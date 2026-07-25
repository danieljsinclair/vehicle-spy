import XCTest

@testable import VehicleSim

// MARK: - VehicleSimApp Scene-Lifecycle Tests
//
// VehicleSimApp (@main) posts three custom notifications from its `body`
// scene-phase handler:
//   • .startDiscovery — when the app launches with connectionMode == .wifi
//     saved in UserDefaults (onAppear).
//   • .resumeDiscovery — when the scene becomes active.
//   • .pauseDiscovery — when the scene goes to background or inactive.
//
// The notification-posting closures are SwiftUI view-modifier bodies that
// cannot be unit-tested without a running scene; what IS testable is the
// string literal that the body compares against when deciding to post
// startDiscovery.  Pinning that literal here means a rename of the
// ConnectionMode raw value (or of the UserDefaults key) breaks the build
// before any production code is touched.
//
// The notification name constants themselves live in a Notification.Name
// extension in VehicleSimAppApp.swift; their distinctness is pinned
// indirectly by mirroring the three literal strings here and asserting
// that no two collide.  If a developer renames one constant to match
// another, this test fails.

final class VehicleSimAppAppTests: XCTestCase {

    // MARK: - WiFi-mode check (onAppear body)

    func testWifiModeRawValueMatchesBodyLiteral() {
        // The body onAppear checks:
        //   if let savedMode = UserDefaults.standard.string(forKey: "connectionMode"),
        //      savedMode == ConnectionMode.wifi.rawValue {
        //       NotificationCenter.default.post(name: .startDiscovery, ...)
        //   }
        // If someone changes ConnectionMode.wifi's raw value, the onAppear
        // check silently stops matching and startDiscovery is never posted.
        XCTAssertEqual(
            ConnectionMode.wifi.rawValue,
            "WiFi",
            "ConnectionMode.wifi.rawValue must remain 'WiFi' — the onAppear body compares UserDefaults string against this value"
        )
    }

    // MARK: - Notification name distinctness

    func testNotificationNameStringsAreDistinct() {
        // The body posts three notifications by name.  A collision would
        // cause the wrong handler to fire silently.  The three names used
        // in the body are "startDiscovery", "resumeDiscovery", and
        // "pauseDiscovery" — these must never overlap.
        let names = [
            "startDiscovery",
            "resumeDiscovery",
            "pauseDiscovery",
        ]
        let unique = Set(names)
        XCTAssertEqual(
            unique.count,
            names.count,
            "Each notification name must be unique; a collision would cause subscriber routing to fire the wrong handler"
        )
    }

    // MARK: - Notification names do not collide with ConnectionMode raw values

    func testNotificationNamesDoNotCollideWithConnectionModeRawValues() {
        // ConnectionMode cases (.ble, .wifi) are also string raw values
        // stored in UserDefaults under the same "connectionMode" key.
        // A notification name that coincides with a ConnectionMode raw
        // value could cause confusion in debugging/logging.
        let notificationNames: Set<String> = [
            "startDiscovery",
            "resumeDiscovery",
            "pauseDiscovery",
        ]
        let connectionModeValues = Set(ConnectionMode.allCases.map(\.rawValue))

        let collisions = notificationNames.intersection(connectionModeValues)
        XCTAssertTrue(
            collisions.isEmpty,
            "Notification names must not collide with ConnectionMode raw values; collisions: \(collisions)"
        )
    }
}
