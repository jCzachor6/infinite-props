# Infinite Props for Unity

Typed part results require C API 0.5 / ABI 0.5. Part output slots expose
`Parts`; each snapshot references the generated image's shared `Resources`
table by result-local IDs. Rings, hole assignments, exact A8/RGBA and effective
transforms remain available independently of program disposal and later calls.

Import compiled `.iprop` files to create `IpropAsset` objects and previews.
Use `asset.Load()` in a `using` block, then `program.Generate(seed)` to obtain
owned RGBA pixels, geometry and statistics. `image.ToTexture2D()` and
`image.ToSprite(program.Metadata)` create caller-owned Unity objects; destroy
generated sprites and their textures when finished. See the included sample.

A prop that publishes a normal map exposes it as `image.Normal`; pass
`deriveNormal: new DerivedNormal()` to `Generate` to derive one for a prop
that does not. `image.Normal.ToTexture2D()` creates a caller-owned linear
texture and `AuxiliaryImage.Bind(spriteRenderer, texture)` sets it as
`_NormalMap` for 2D lights.

Native plugins must be built before using the package. In the Infinite Props
source checkout run `python3 backends/unity/package-native.py --platform macos`
(or windows, linux, android, ios), then add this package through Package Manager.
Generation can run on a worker; Unity object creation belongs on the main thread.

The runtime uses MPL-2.0; the full license is included. This native package
targets Unity 2022.3+ on desktop, Android and iOS. WebGL integration uses the
separate Infinite Props WASM backend.

## Models (preview)

Run `python3 backends/unity/package-native.py --models` from the source
checkout for each target platform. On iOS the package links one static
archive. Desktop and Android plugins expose the model API in `iprop3dc`.

```csharp
var cache = new TextureCache();
using (var model = new Model(bytes))
{
    var generated = model.Generate(seed: 5, lod: 0, textures: cache);
    var mesh = generated.ToMesh();
    var texture = generated.Textures[0].ToTexture2D();
    // The caller owns the Unity mesh and texture.
}
```

Concurrent calls retain the native handle and a snapshot of cached images.
Passing `known: cache.Known` alone returns metadata for reused textures.
Assign materials in the application's render pipeline.

| Mesh attribute | Conversion to Unity |
| --- | --- |
| Position | Divide by units per metre and negate Z |
| Normal | Negate Z |
| Tangent | Negate Z and W |
| UV | Replace V with 1 − V for the bottom-up texture upload |
| Triangles | Swap the second and third index and create one submesh per surface |

Run `backends/test/check-csharp.py` with the normal Verify arguments and
`--models PATH_TO_MODEL_LIBRARY`. For the engine checks, build the native
package, pass `--models` to `backends/unity/tests/prepare-editor.py`, and
run the package's Edit Mode tests in a graphics-enabled Unity Editor.
