# Infinite Props for Rust and Bevy (preview)

Two crates in one Cargo workspace:

- **`iprop`** loads compiled programs (`.iprop`) and models (`.iprop3d`) and
  generates them from a seed and named inputs, with no engine attached. It
  links the [C API](../capi/README.md) and the
  [model C API](../capi3d/README.md) statically.
- **`bevy_iprop`** is a plugin for Bevy 0.20: it loads both kinds of file as
  assets and generates sprites and models on the entities that ask for them.

Models are a preview: the model API (0.3) and the `.iprop3d` format may
change until the model contract is frozen. The crates are not on crates.io
and not in the installed SDK yet; use them from a source checkout.

## Requirements and build

Rust 1.97.1 or later (the version Bevy 0.20 needs), CMake, a C++20 compiler
and zlib. The `iprop` crate's build script builds the C API with CMake from
the checkout it lives in: a runtime-only build with the model libraries
(`IPROP_RUNTIME_ONLY` and `IPROP_RUNTIME_MODELS`). To link libraries built
elsewhere, set `IPROP_LIB_DIR` to the directory that holds the static
`iprop3dc`, `ipropc`, `iprop_model` and `iprop_runtime` libraries
(`iprop3dc_static` and `ipropc_static` with MSVC).

Depend on the crates by path:

```toml
[dependencies]
bevy_iprop = { path = "../infinite-props/backends/bevy/bevy_iprop" }
```

`bevy_iprop` enables only the Bevy features it uses (assets, images,
meshes, PBR materials and sprites); a game adds `bevy` with its own features.

## Rust without an engine

```rust
use iprop::{InputValue, Model, ModelRequest, Program, Request};

let program = Program::load(&std::fs::read("potion.iprop")?)?;
let image = program.generate(&Request::seed(7).input("tint", InputValue::Color([200, 40, 60, 255])))?;
// image.rgba: RGBA8 rows from the top; image.images: normal and material maps;
// image.outputs: points, rectangles and circles in pixels.

let model = Model::load(&std::fs::read("crate.iprop3d")?)?;
let crate_3d = model.generate(&ModelRequest::seed(5).lod(1))?;
// crate_3d.surfaces: positions, normals, tangents, UVs and indices per material;
// crate_3d.textures: RGBA8 with their maps; crate_3d.outputs: points and boxes.
```

Every result is an owned copy. A `Program` serializes its calls; a `Model`
can be generated from several threads at once. A request's `limits` holds
the work limit and a timeout: a zero work limit selects the default of what
runs, 100,000,000 steps for a program and 600,000,000 for a model. A failed
call returns an `iprop::Error` with the C API's status, reason, stage and
message. `Texture::known()` and `ModelRequest::known` name textures a host
already holds, which a generation then charges but does not make again.

## Bevy

```rust
use bevy::prelude::*;
use bevy_iprop::{iprop::{InputValue, ModelRequest, Request}, IpropModelInstance, IpropPlugin, IpropSprite};

fn main() {
    App::new().add_plugins((DefaultPlugins, IpropPlugin)).add_systems(Startup, setup).run();
}

fn setup(mut commands: Commands, assets: Res<AssetServer>) {
    commands.spawn(IpropModelInstance {
        model: assets.load("props/hydrant.iprop3d"),
        request: ModelRequest::seed(4).input("paint", InputValue::Color([47, 111, 74, 255])),
    });
    commands.spawn(IpropSprite { program: assets.load("props/potion.iprop"), request: Request::seed(7) });
}
```

- **`IpropModelInstance`** builds the model when its asset has loaded and
  again whenever the component changes. A child scaled from model units to
  metres holds one `Mesh3d` with a `StandardMaterial` per material.
  `IpropModelGenerated` then carries the model's outputs and bounds in model
  units.
- **`IpropSprite`** draws the program into an `Image`, shows it as a `Sprite`
  anchored at the program's origin and adds `IpropSpriteImage` with the
  normal map, when the program publishes one, and the outputs.
- **`IpropFailed`** holds the error of a generation that failed; changing
  the request tries again.
- **`IpropTextureCache`** keeps every texture generated so far by key. Later
  instances, other seeds that share a texture and other levels of detail
  name the held textures known and reuse their images.

The conversions are public for games that place meshes themselves:
`surface_mesh`, `surface_material`, `model_parts`, `rgba_image`,
`program_image` and `packed_material_map`.

### Conventions

Bevy, glTF and the model API share their conventions, so nothing is
flipped: right-handed axes with +Y up, triangles counterclockwise seen from
outside, UV (0, 0) at the upper left of the image, and tangents whose W
makes `W * cross(normal, tangent)` point up the image. A material maps as
follows:

| Model | `StandardMaterial` |
| --- | --- |
| texture | `base_color_texture`, sRGB |
| normal map | `normal_map_texture`, linear |
| roughness, metallic and AO maps | one image, occlusion in red, roughness in green, metallic in blue, as `metallic_roughness_texture` and `occlusion_texture`; a missing map is 255 |
| roughness, metallic | `perceptual_roughness`, `metallic`, multiplying their maps |
| emission | `emissive` with the emission map, or the texture without one |
| opaque, cutout, blend | `AlphaMode::Opaque`, `Mask(cutoff)`, `Blend` |
| two-sided | `double_sided` and no culling |
| crisp | nearest filtering; otherwise linear |
| repeats | `Repeat` addressing; otherwise clamped |

Metal reflects its surroundings: give the camera an environment map or a
light probe, or metallic surfaces look dark.

### Showcase

The `showcase` example puts a hydrant, a bench and a wall of sandbags from
the binding fixtures under the sun and turns them. It needs the rest of
Bevy, so it sits behind the `demo` feature:

```sh
IPROP_FIXTURES=$PWD/build/binding-fixtures cargo run --release \
    --manifest-path backends/bevy/Cargo.toml -p bevy_iprop --example showcase --features demo
```

With `IPROP_SCREENSHOT` set to a PNG path it saves one frame once every
model is built and quits.

## Verify

Build the native tools of the checkout, prepare the binding fixtures and
run both crates' tests against them:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DIPROP_SFML=OFF -DIPROP_GODOT=OFF
cmake --build build --target ipc ipgen
python3 backends/test/prepare-bindings.py build/tools build/binding-fixtures
IPROP_FIXTURES=$PWD/build/binding-fixtures cargo test --manifest-path backends/bevy/Cargo.toml --workspace
```

The tests read the same fixtures as the other [native bindings](../../docs/native-bindings.md#models-preview):
every case of the model corpus is compared with `ipgen --dump` bit for bit,
a 2D program with its maps and outputs with the CLI's images, and the
plugin builds meshes, materials and sprites in a headless app.
