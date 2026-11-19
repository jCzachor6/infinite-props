# Native language and engine bindings

The [Apple](../backends/apple/README.md), [Unity](../backends/unity/README.md),
[Android](../backends/android/README.md) and [Python](../backends/python/README.md)
packages call the same [C API](../backends/capi/README.md). They require C API
0.7 or later on ABI 0.5 (0.8 adds material maps) and load compiled `.iprop` files.
The [Rust and Bevy](../backends/bevy/README.md) crates (a preview) link the
same C API statically from a source checkout. Compile `.ips` with `ipc` before
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

## Typed part results

`output list part` uses the additive C API 0.5 contract; the C ABI remains 0.5.
Managed wrappers negotiate both typed-part/resource capabilities and enable
`IPROP_GENERATE_PART_OUTPUTS`. Each part slot keeps its declaration index,
static owner, exact name and append order, including empty slots. Old packed
float fields remain available for point/rect/circle slots; parts have their own
records. They are never encoded as float arrays or a flattened image.

| Host | Snapshots | Shared result-local resource table |
| --- | --- | --- |
| Python | `output.parts` | `image.resources`, `image.resource(id)` |
| Swift | `output.parts` | `image.resources`, `image.resource(id)` |
| C#/Unity | `output.Parts` | `image.Resources`, `image.Resource(id)` |
| Kotlin/Android | `output.parts` | `image.resources`, `image.resource(id)` |
| HTML5/WASM | `result.outputs[i].parts` | `result.resources[id - 1]` |
| Godot | `asset.get_output_slots()[i].parts` | `asset.get_resources()`, `asset.get_resource(id)` |
| SFML | `copyResult(result).outputs[i].parts` | `copyResult(result).resources` |

Snapshots carry all [C API part fields](c-api-part-outputs.md), using each host's
normal naming style. Resource IDs start at one, are local to one generation,
and share one owned table entry across snapshots and mappings. Zero denotes an
absent optional texture; `0xffffffff` denotes no region for a raster shape.
Geometry keeps ordered rings, parents, hole assignments and region metadata.
Raster resources retain exact top-left A8 or straight RGBA bytes, including
soft alpha and hidden RGB. Geometry vertices and source/texture mappings stay
in their original source space; effective D4/translation and root metadata
are copied without applying a second transform.

Every host makes one owned copy per resource ID. Old results survive native
result/program destruction and later generations. Godot also preserves retained
arrays across asset reload; SFML's complete `copyResult` is optional for the
image-only viewer. The low-level WASM module's RGBA image view remains borrowed
until its next call, while `Iprop.generate` owns that image copy. Typed WASM
resources are owned in both interfaces. No new persisted result format is added.

The shared part gate has 23 complete C++/checked-C observations covering holes,
soft A8/RGBA, shared resources, 256 snapshots/512 stores, empty and mixed slots,
all dynamic/static D4 and retained data. These are test fixtures, separate from
the image corpus and from any public export format. The pixel corpus keeps
part kind/count with zero float leaves; full snapshots/resources use this gate.

## Models (preview)

The following hosts generate [models](language/models.md) through the preview model API.

| Host | Interface |
| --- | --- |
| C | [`iprop3d.h`](c-api-models.md), static and shared libraries with their own API version |
| Browser | [`iprop3d.wasm` and `Iprop3D`](../backends/html5/README.md#models-preview), with a three.js adapter |
| Godot | [`IpropModel` and `IpropModelInstance3D`](../backends/godot/README.md#models-preview) |
| Python | [Model and TextureCache](../backends/python/README.md#models-preview), Pillow texture export |
| Swift | [Model and TextureCache](../backends/apple/README.md#models-preview), CGImage, Metal and SceneKit |
| C#/Unity | [Model and TextureCache](../backends/unity/README.md#models-preview), Mesh and Texture2D |
| Kotlin/Android | [Model and TextureCache](../backends/android/README.md#models-preview), Bitmap |
| SFML | [ModelHost and ModelRender](../backends/sfml/README.md#models-preview), texture upload and PNG export |
| Rust | [`iprop::Model`](../backends/bevy/README.md#rust-without-an-engine), owned results and known textures |
| Bevy | [`IpropPlugin`, `IpropModelInstance` and `IpropSprite`](../backends/bevy/README.md#bevy), meshes, `StandardMaterial` and a texture cache |

All five native bindings accept seeds, named inputs, a level of detail from
0 to 7, execution limits and cancellation. A loaded model can serve
concurrent generations with independent parameters. A texture cache keeps
image storage alive across calls. Raw known-texture records retain the
geometry and accounting while allowing the host to reuse its own images.

### Build and verification

Build the source checkout and run `backends/test/prepare-bindings.py` with
the native tools directory and fixture directory. It compiles the model
corpus and records exact float bits, array bytes, counters and pixel hashes
from `ipgen --dump` in `model-corpus.bin`. Each host's Verify procedure uses
these fixtures beside the existing pixel corpus. The installed SDK ships
binding sources. Build the matching preview library from the source checkout.

## Normal maps and other auxiliary images

A generation can publish images beside the main one: the normal map of a prop
that declares `output normal` ([authoring](language/relief.md),
[C API](c-api-aux-images.md)), then its material maps, roughness, metallic,
AO and emission ([authoring](language/material-maps.md)). Every binding
exposes the full list, a convenience for the normal map, a lookup by kind and
the opt-in request for a derived normal map; a result without auxiliary
images has an empty list.

| Host | All images | Normal map | Derive one | Declared by the program |
| --- | --- | --- | --- | --- |
| Python | `image.images` | `image.normal` | `generate(derive_normal=True)` or a `DerivedNormal` | `program.images` |
| Swift | `image.images` | `image.normal` | `generate(deriveNormal: DerivedNormal())` | `program.images` |
| C#/Unity | `image.Images` | `image.Normal` | `Generate(deriveNormal: new DerivedNormal())` | `program.Images` |
| Kotlin/Android | `image.images` | `image.normal` | `generate(deriveNormal = DerivedNormal())` | `program.images` |
| HTML5/WASM | `result.images` | `result.normal` | `generate(bytes, {deriveNormal: true})` | — |
| Godot | `asset.images` | `asset.normal_image` | `asset.derive_normal = true` | `asset.declared_images` |
| SFML | `copyResult(result).images` | `copyResult(result).normal()` | `generate(..., &request)` | `declaredImages(program)` |

Each image carries its kind (1 = normal, 2 = roughness, 3 = metallic, 4 = AO,
5 = emission, listed in that order), its source (1 = authored by the program,
2 = derived by the runtime), its name and owned top-left RGBA8 bytes of the
main image's size. Look one up with `image.image_of(ImageKind.ROUGHNESS)`
(Python), `image.image(.roughness)` (Swift), `image.Image(ImageKind.Roughness)`
(C#), `image.image(ImageKind.ROUGHNESS)` (Kotlin), `asset.get_image(kind)`
(Godot) or `copyResult(result).image(kind)` (SFML). An authored image always wins over a request, so one
code path serves both kinds of program. A kind or source a wrapper does not
know is skipped, never an error.

A normal map stores one direction per pixel: R is x rightward, G is y
**upward** and B is z toward the viewer, each byte `b` decoding as
`b / 127.5 - 1`. The bytes are linear data: upload them without an sRGB
transfer, and normalize after decoding. Roughness, metallic and AO maps hold
one linear value in R, G and B, uploaded the same way; an emission map is a
colour like the main image's. Alpha is 255 everywhere (alpha tag 2);
coverage is the main image's alpha. Green-up is the convention of Godot's
`CanvasTexture.normal_texture` and of Unity's normal maps, so neither needs a
channel flip: Unity flips only the row order on upload, as for the main image,
which leaves "up on the displayed sprite" unchanged.

| Host | Presentation helper |
| --- | --- |
| Godot | `IpropSprite2D` sets a `CanvasTexture` with the normal map, lit by `Light2D` |
| Unity | `image.Normal.ToTexture2D()` is a linear texture; `AuxiliaryImage.Bind(renderer, texture)` sets it as `_NormalMap` |
| Swift | `makeCGImage()`, `makeSKTexture()` for `SKSpriteNode.normalTexture`, `makeMetalTexture(device:)` |
| Kotlin/Android | `toBitmap()` keeps the exact bytes |
| Python | `to_pillow()` |
| HTML5/WASM | `drawToCanvas(canvas, result.normal)` shows the stored bytes |

The derived request has three numbers, each defaulting when zero: `depth`
(longer side / 16) and `rounding` (longer side / 32, at most 64) in image
pixels, and `detail` (0.5), the share of brightness in the height. It spends
work, not fuel, and leaves the main image and outputs untouched. Values out of
range are argument errors before execution.

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
same compiled program, uint32 seed, dimensions and typed inputs. Auxiliary
images are part of the gate: the corpus (format `IPXCRP02`) records the
derived-normal request of each case and every published image with its kind,
source and name. There is no
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
