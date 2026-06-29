import SwiftUI
import Combine

@main
struct VehicleSimApp: App {
    @Environment(\.scenePhase) private var scenePhase

    var body: some Scene {
        WindowGroup {
            ContentView()
                .onAppear {
                    // Start discovery on app launch if WiFi mode is pre-selected
                    if let savedMode = UserDefaults.standard.string(forKey: "connectionMode"),
                       savedMode == ConnectionMode.wifi.rawValue {
                        NotificationCenter.default.post(name: .startDiscovery, object: nil)
                    }
                }
                .onReceive(NotificationCenter.default.publisher(for: .startDiscovery)) { _ in
                    // Handled by ContentView's ViewModel
                }
        }
        .onChange(of: scenePhase) { _, newPhase in
            switch newPhase {
            case .active:
                // Resume discovery when app becomes active
                NotificationCenter.default.post(name: .resumeDiscovery, object: nil)
            case .background, .inactive:
                // Pause discovery when app goes to background
                NotificationCenter.default.post(name: .pauseDiscovery, object: nil)
            @unknown default:
                break
            }
        }
    }
}

// MARK: - Notification Names

extension Notification.Name {
    static let startDiscovery = Notification.Name("startDiscovery")
    static let resumeDiscovery = Notification.Name("resumeDiscovery")
    static let pauseDiscovery = Notification.Name("pauseDiscovery")
}
