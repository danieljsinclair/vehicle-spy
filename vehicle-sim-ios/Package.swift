// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "VehicleSimApp",
    platforms: [
        .iOS(.v15)
    ],
    products: [
        .executable(
            name: "VehicleSimApp",
            targets: ["VehicleSimApp"]
        )
    ],
    dependencies: [
        // No external Swift dependencies
        .binaryTarget(
            name: "vehicle-sim-core",
            path: "./vehicle-sim-core.xcframework"
        )
    ],
    targets: [
        .executableTarget(
            name: "VehicleSimApp",
            dependencies: [
                .product(name: "vehicle-sim-core", package: "vehicle-sim-core")
            ],
            path: "Sources",
            resources: [
                .copy("Info.plist")
            ],
            swiftSettings: [
                .enableUpcomingFeature("BareSlashRegex")
            ]
        )
    ]
)
