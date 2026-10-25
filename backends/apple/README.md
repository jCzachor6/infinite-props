# Apple / Swift

A Swift package over C API 0.4 / ABI 0.5 for macOS 12+ and iOS 15+.
It provides typed inputs, metadata, owned RGBA/geometry/statistics results,
structured errors and CGImage, SpriteKit and Metal helpers. It has no game
dependencies. See the [shared binding contract](../../docs/native-bindings.md).

## Build from source

Use full Xcode with the macOS and iOS SDKs. From the repository root:

```sh
python3 backends/apple/build-xcframework.py
swift build --package-path backends/apple
```

The builder creates `Artifacts/CIprop.xcframework`: universal arm64/x86_64
macOS and iOS Simulator libraries, and an arm64 iOS device library. C API and
runtime code are combined into each static archive; SwiftPM links system
zlib and libc++. Existing output is never overwritten; move it or select
`--output` before rebuilding. Add `backends/apple` as a local Swift package
in Xcode and select the `InfiniteProps` product.

Without that artifact, the package uses its system-library target. Install
the native SDK and set `PKG_CONFIG_PATH=/path/to/sdk/lib/pkgconfig` before
building. The installed SDK includes the same Swift package sources. Building
an XCFramework requires the complete source checkout.
Pass `--source /path/to/source` when invoking the builder from an installed SDK.

## Use

```swift
import Foundation
import InfiniteProps

let program = try Program(contentsOf: URL(fileURLWithPath: "stone.iprop"))
defer { program.close() }
let image = try program.generate(seed: 7, inputs: [
    "tint": .color(RGBA8(138, 146, 153))
])
let origin = (program.metadata.originX, program.metadata.originY)
let collider = image.output(named: "collider", owner: 0)
let cgImage = try image.makeCGImage()
// Also: image.makeSKTexture(), image.makeMetalTexture(device: device).
```

Use `.float(Float)`, `.uint(UInt32)` or `.int(Int32)` for numeric inputs.
The program closes automatically on deinitialization; `close()` releases it
earlier and is idempotent. Results remain usable after close.
For distinct Unicode names that Swift dictionaries consider canonically
equivalent, use `generate(overrides: [InputOverride(name, value), ...])`.
This array API and output lookup compare the exact UTF-8 bytes.

Raw `rgba` is unchanged top-left RGBA8 with the runtime's legacy destination
alpha tag. CGImage/SpriteKit presentation interprets stored channels as
straight RGBA. Metal uploads raw bytes and leaves blending to the renderer.
The [shared guide](../../docs/native-bindings.md#pixels-and-presentation)
explains this distinction.

## Verify

Prepare fixtures using the shared guide, then:

```sh
IPROP_FIXTURES="$PWD/build/binding-fixtures" IPROP_PIXEL_CORPUS_DIR="$PWD/build/pixel-corpus" swift test --package-path backends/apple
```

Use a scratch directory outside iCloud when codesigning test bundles, e.g.
`--scratch-path /tmp/iprop-apple-tests`. Tests exercise the real C runtime,
CGImage ownership and Metal upload/readback when a GPU device is available.
Native archive cross-compilation covers the device/simulator slices; actual
app deployment remains an application-level check.

Generate the complete [shared pixel corpus](../../docs/native-bindings.md#required-pixel-equality-gate) before testing. The suite requires it and compares every image, CGImage provider and available Metal texture readback byte-for-byte.
