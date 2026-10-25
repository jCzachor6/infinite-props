# Unity / C#

The UPM package `Packages/dev.infiniteprops.runtime` provides a C# wrapper,
an `.iprop` importer with previews, an `IpropAsset` source object and owned
Texture2D/Sprite helpers. Unity 2022.3+; native targets are macOS arm64/x86_64,
Windows/Linux x86_64, Android and iOS arm64. WebGL uses the separate
[HTML5/WASM backend](../html5/README.md).

## Build and install

From the repository root, build plugins for the platforms you need:

```sh
python3 backends/unity/package-native.py --platform macos
python3 backends/unity/package-native.py --platform android --ndk /path/to/android-ndk
python3 backends/unity/package-native.py --platform ios
```

Windows/Linux builds run on the corresponding host (`--platform windows`
or `--platform linux`). Native build dependencies match the [C API](../capi/README.md).
Windows requires a suitable zlib SDK; select static zlib or ship its runtime
DLL beside the plugin. Pass SDK discovery settings through CMake environment
variables such as `CMAKE_PREFIX_PATH`. iOS postprocessing adds system zlib and
libc++. Android builds use static libc++ and 16 KiB-compatible ELF alignment.

The builder writes platform-scoped plugin import settings. Generated native
files are ignored by Git. Add the local package's `package.json` through
Unity Package Manager's **Add package from disk**, or copy the complete built
package into your project. Package source and stable asset GUIDs are shipped
with the SDK; building plugins requires the full runtime source checkout
(`--source /path/to/source` when running from an installed SDK).

## Use

Import a compiled `.iprop` file into Assets. The importer stores its bytes
in an `IpropAsset` and generates a preview using the inspector's seed.

```csharp
using InfiniteProps;
using System.Collections.Generic;

using (var program = importedAsset.Load())
{
    var image = program.Generate(seed: 7, inputs: new Dictionary<string, InputValue> {
        ["tint"] = InputValue.Color(138, 146, 153)
    });
    spriteRenderer.sprite = image.ToSprite(program.Metadata);
    var collider = image.FindOutput("collider", owner: 0);
}
```

`InputValue.Float`, `.UInt` and `.Int` preserve explicit binary input types.
Generate on worker threads if needed; create Unity objects on the main thread.
Dispose programs (SafeHandle also provides a finalization fallback). Returned
arrays own their data. The caller owns generated sprites **and their textures**
and must destroy both; see the included sample.

Raw RGBA and geometry use top-left coordinates. Texture helpers flip image
rows and Sprite helpers convert the declared origin to a Unity pivot. They
use straight-alpha display interpretation; raw alpha remains tagged with
the runtime's legacy destination compositor. See the
[shared contract](../../docs/native-bindings.md).

## Verify

With .NET SDK 8 and the shared fixtures prepared, put the built native
library on the OS loader path and run:

```sh
dotnet run --project backends/unity/tests/Conformance.csproj -- build/binding-fixtures build/pixel-corpus
```

On macOS set `DYLD_LIBRARY_PATH` to the plugin directory; on Linux use
`LD_LIBRARY_PATH`, on Windows `PATH`. This runs the production C# binding
against the actual C library without an editor. Importer, Texture2D/Sprite
and player-build checks require Unity; the package includes editor tests
for those paths.
Add `dev.infiniteprops.runtime` to the project's `testables` list and launch
Unity with `IPROP_FIXTURES` pointing to the prepared fixtures. Run the package's
EditMode tests through Test Runner or Unity's `-runTests -testPlatform EditMode`.

Generate the complete [shared pixel corpus](../../docs/native-bindings.md#required-pixel-equality-gate)
first. Prepare an isolated Editor test project after building the native plugin:

```sh
python3 backends/unity/tests/prepare-editor.py /path/to/isolated-project
/path/to/Unity -batchmode -projectPath /path/to/isolated-project \
  -runTests -testPlatform EditMode -testResults /path/to/results.xml
```

A real graphics device is required; omit `-nographics`. The Editor suite compares
every generated image, texture upload and unblended GPU render/readback with the
same corpus. Run the Editor suite locally in Gamma and Linear color spaces,
using `-ipropColorSpace Gamma` or `-ipropColorSpace Linear` to assert the active
mode. Unity Editor is currently excluded from CI; adding it after CI license
configuration is tracked in the repository's `TODO.md`. The standalone production
C# binding continues to run against the full corpus in CI.

To repeat locally in Linear after the first Editor run, select the mode and
restart Unity with `-ipropColorSpace Linear` added to the test command:

```sh
python3 backends/unity/tests/prepare-editor.py /path/to/isolated-project --color-space Linear
```
