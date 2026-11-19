// swift-tools-version: 5.9
import PackageDescription
import Foundation

// The source checkout uses the installed C SDK. build-xcframework.py prepares
// the self-contained binary dependency for Apple application distributions.
let packageRoot = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
let binary = FileManager.default.fileExists(atPath: packageRoot.appendingPathComponent("Artifacts/CIprop.xcframework").path)
let artifact = packageRoot.appendingPathComponent("Artifacts/CIprop.xcframework")
let slices = (try? FileManager.default.contentsOfDirectory(at: artifact, includingPropertiesForKeys: nil)) ?? []
let modelSettings: [SwiftSetting] = slices.contains {
    FileManager.default.fileExists(atPath: $0.appendingPathComponent("Headers/iprop3d.h").path)
} ? [.define("IPROP_MODELS")] : []
let native: Target = binary
    ? .binaryTarget(name: "CIprop", path: "Artifacts/CIprop.xcframework")
    : .systemLibrary(name: "CIprop", pkgConfig: "ipropc")

let package = Package(
    name: "InfiniteProps",
    platforms: [.macOS(.v12), .iOS(.v15)],
    products: [.library(name: "InfiniteProps", targets: ["InfiniteProps"])],
    targets: [native,
        .target(name: "InfiniteProps", dependencies: ["CIprop"],
                swiftSettings: modelSettings, linkerSettings: [.linkedLibrary("z"), .linkedLibrary("c++")]),
        .testTarget(name: "InfinitePropsTests", dependencies: ["InfiniteProps"], swiftSettings: modelSettings)
    ])
