# Native language and engine bindings

The [Apple](../backends/apple/README.md), [Unity](../backends/unity/README.md),
[Android](../backends/android/README.md) and [Python](../backends/python/README.md)
packages call the same [C API](../backends/capi/README.md). They require C API
0.4 / ABI 0.5 and load compiled `.iprop` files. Compile `.ips` with `ipc` before
shipping assets. No compiler, server or network connection is needed at runtime.

## Shared contract

- Every generation supplies a uint32 seed, optional uint32 dimensions (zero
  selects the asset's declared dimension), and typed named root inputs.
- Inputs start at their declared defaults on every call. Overrides require an
  exact declared binary type. Source `i32` and `u32` both reflect as uint32;
  signed helpers preserve the integer's 32-bit representation.
- Metadata includes declared dimensions, geometric origin, fuel budget,
  emission metadata and physical container flags. Actual generated dimensions
  belong to the result.
- Results own copied RGBA8 pixels, actual row stride, pixel/alpha/storage tags,
  statistics and ordered geometry slots. Closing the program or generating
  again leaves older results intact. No borrowed native pointer escapes.
- Output identity is `(owner, name)`. Root owner is zero; part N has owner N+1.
  Every declared slot remains present, including empty lists and unset singular
  values. Points, rectangles and circles contain packed binary32 components
  already in caller coordinates. Adapters apply no second geometry transform.
- Names use exact byte-counted UTF-8, including empty names and embedded NUL.
  Float arrays retain their storage bits (inspect without floating-point
  operations that can quiet signaling NaNs); Python additionally exposes `value_bits`
  because Python floats use binary64.
- Program calls and close/dispose serialize per instance. Use independent
  programs for parallel native generation. Deterministic output does not
  imply that calls on one program execute in parallel.
- Native failures include status, reason, stage and location presence flags.
  Only fields selected by `location_flags` identify a valid VM/file location.
  Messages use an 8192-byte buffer and expose a truncation indicator.

See the [ownership and error contracts](c-api-contracts.md) and
[ABI policy](c-api-abi.md).

## Pixels and presentation

Raw images preserve the runtime's **legacy destination** alpha tag (1).
Its compositor is specified in the [C header](../backends/capi/iprop.h): it is
neither standard straight-alpha nor premultiplied-alpha compositing. In
particular, RGB can be nonzero where stored alpha is zero. Adapters leave raw
pixels unchanged and report this tag.

CGImage/SpriteKit, Unity and Pillow convenience helpers interpret stored
channels as straight RGBA for presentation. Android `toBitmap()` instead
provides lossless, unpremultiplied RGBA storage; use the explicit
`toPremultipliedBitmap()` conversion for Android Canvas/View drawing. That
conversion can quantize translucent RGB and is excluded from raw pixel equality.
Host APIs can
then premultiply them internally. This interpretation cannot recover source
colours or coverage from an already composited legacy image. Metal upload
copies stored channels unchanged and leaves blending to the application.
Unity flips rows only in its texture helper; raw data and geometry retain
top-left storage and coordinates. Create engine/UI objects on the engine's
required thread.

## Build and verification

`-DIPROP_RUNTIME_ONLY=ON` builds the runtime and the C ABI without configuring
host tools, documentation, engine dependencies or tests. Mobile builds use
this mode; it is a library build, not a complete installed SDK. Full native
SDKs additionally include the binding source packages and a relocatable
`ipropc.pc` for Apple system-library consumption.

From a source checkout, prepare cross-binding fixtures:

```sh
cmake --build build --target ipc ipgen iprop_capi --parallel 4
python3 backends/test/prepare-bindings.py build/tools build/binding-fixtures
```

Fixtures compare pixels against the CLI PNG renderer and cover typed inputs,
negative zero, high seeds, geometry kinds, lists, unset slots, repeated calls,
size overrides, errors, ownership and concurrent calls. Binary fixtures cover
embedded NUL/Unicode names, exact special float bits and part-owner numbering.
Backend guides provide executable verification commands and state which
platform SDKs are required. Host C#/JNI tests exercise actual native calls;
Unity editor and Android device tests are separate verification steps.

## Required pixel equality gate

Every backend must return identical dimensions and **every RGBA byte** for the
same compiled program, uint32 seed, dimensions and typed inputs. There is no
tolerance, perceptual metric, alpha-channel exemption or backend-specific golden.
This includes RGB underneath zero alpha. Ordered geometry values are compared
by their binary32 bits as well; the focused fixtures separately verify owner IDs
and Unicode/NUL names not exposed in WASM reflection.

The complete Native/WASM suite records every successful generation, including
all specialist suites and host overrides. It publishes `pixel-corpus.bin` and a
SHA-256/count manifest only after all native CLI comparisons and independent
pins pass. At this revision it contains 16,177 cases from 1,880 programs; future
cases automatically enter the same artifact. Generate it with:

```sh
IPROP_PIXEL_CORPUS_DIR=build/pixel-corpus \
node backends/html5/test/run-tests.js build/wasm build/tools
```

CI distributes this one artifact to Python/Pillow and production C# on Windows,
Linux and macOS, Swift/CGImage on macOS (plus Metal where available), packaged
Android JNI/Bitmap on an emulator, SFML texture/render readback and Godot
Image/texture readback. All cases must be consumed; a byte difference fails the job.

Unity Editor texture/render tests run locally in both Gamma and Linear color
spaces. Their CI automation is deferred until a CI license is configured, as
tracked in the repository's `TODO.md`. The production C# binding continues to
consume the full corpus in the native CI matrix. See the
[Unity verification commands](../backends/unity/README.md#verify) for the separate
Editor/GPU checks.

Exact render tests use native size, point sampling and no blending. Scene
scaling, filtering, lighting, color grading and platform display conversion are
application rendering operations; compare the generated buffer or the tested
unblended texture path when checking the format's deterministic pixel contract.
