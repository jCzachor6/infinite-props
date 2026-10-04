# Preparing release 0.4.0

The candidate is maintained on `develop`. Product, editor and website versions
are 0.4.0; the format minor is 4 and the C API stays 0.2. The released 0.3
baseline remains on `main` until the release merge. Preparing packages does
not create a release tag or publish artifacts.

## Changes and migration

- Replace `draw(v: i32, a: i32) { ... }` with `draw { ... }`. Read-only `v`
  and `a` remain available inside draw; helpers keep explicit typed parameters.
- Replace bare builtin `mix` and `shade` with `color.mix` and `color.shade`.
- Builtin/canvas calls accept named arguments in signature evaluation order.
  Numeric casts, RGB constructors, overloaded lerp, typed picks, chance and
  explicit-seed roll/choose lower to existing instructions.
- UTF-8 diagnostics count codepoints and suggest canonical names. The source
  formatter and 18 single-codepoint emoji aliases normalize to word syntax.
  The [language reference](language-v0.4.md) and [24 recipes](../examples/recipes/README.md)
  provide exact spellings and examples.
- DCE, folding, CSE, small-loop unrolling and proven integer strength reduction
  retain pixels/output bits and never grow expanded artifacts. Fuel consumption
  may decrease. See [measured acceptance](optimization-acceptance-v0.4.md).
- `ipc` strips debug data and compresses when smaller by default. Use `--debug`
  for source maps and `--no-compress` for expanded files. The editor inspector
  requests debug data; binary exports use production defaults.

The v0.4 loader accepts frozen uncompressed 0.1–0.3 files and bounded lossless
DEFLATE envelopes. Older readers reject minor 4, even without compression;
`--no-compress` selects expanded output, not a legacy format version. Code,
image, output, metadata and RNG contracts remain unchanged. C ABI layouts
retain API 0.2; use the installed 0.4 CMake package for updated hosts.

## Build and package

Native builds and installed CMake consumers need zlib >= 1.2.9. Linux uses
the `zlib1g-dev` package; macOS can use the platform SDK. Windows CI supplies
`zlib:x64-windows-static-md` through vcpkg and adds its installation to
`CMAKE_PREFIX_PATH`, matching static zlib to the shared MSVC runtime.
Set `ZLIB_USE_STATIC_LIBS=ON` for this Windows triplet; packaging forwards
that choice to its installed CMake consumer checks.
The [C API installation guide](../backends/capi/README.md) describes consumers.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
scripts/release.sh 0.4.0 build --check
```

Require integrations with `IPROP_SFML=ON`/`IPROP_GODOT=ON` and their dependency
paths when including them. SFML uses 3.1.0; Godot uses engine 4.5.2 and
`godot-cpp` at `godot-4.5-stable`, built with `GODOTCPP_TARGET=template_release`.
On Linux SFML needs X11 development libraries including Xi; headless graphics
checks run through `xvfb-run`.

After committing the preparation changes, create the source/native archives:

```sh
scripts/release.sh 0.4.0 build
```

The script checks matching source/build versions, CTest, a relocated SDK, all
six tools, shared/static C consumers and included integrations. Artifacts are
`dist/iprop-0.4.0.tar.gz`, `dist/iprop-0.4.0-<os>-<arch>.tar.gz` and
`dist/iprop-0.4.0-SHA256SUMS.txt`. The native manifest records the source commit,
product/API versions and included backends. The script also extracts the
completed archive outside the build tree and reruns installed-tool/consumer
and included Godot checks. A per-SDK validation JSON records the tested commit
and archive digests. Packaging requires Python 3.12 or newer.

After preparing and testing the current WASM and built website, run
`python3 scripts/package_web.py`. It creates separate WASM/website archives
and reruns conformance and website smoke tests on their extracted contents.
See [CI artifacts and reports](ci.md) for downloadable builds and diagnostics.

## Final release gate

Inspect GitHub CI for the exact candidate commit: Linux, macOS, Windows, WASM,
SFML/Godot and sanitizer jobs must pass. Confirm frozen golden hashes and
artifact size/output acceptance. Then merge the reviewed candidate to `main`,
date the changelog, mark the v0.4 spec frozen, tag `v0.4.0` on that merge and
rebuild packages from the tagged clean commit before publication.
