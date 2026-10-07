# Release 0.5.0

Release date: 2026-10-07. Product, editor and website versions are 0.5.0;
source language 0.5, binary format 0.4, C API 0.3 and ABI line / SONAME 0.4
are separate contracts. The release tag is `v0.5.0` on `main`. Local
packaging and the tagged publication workflow are described below.

## Changes and migration

The [0.5 migration chapter](language/migration-v0.5.md) is the complete
reference; the [changelog](../CHANGELOG.md) lists every area.

- Accepted 0.4 sources keep their meaning, with one exception: the 18 lexer
  emoji aliases are removed. Replace each alias with its word before compiling
  with 0.5; `ipc --format` cannot convert them, because 0.5 rejects the alias.
- New source forms: signed defaults and palette elements, mixed numeric
  palettes widened to `f32`, `input i32`, optional declaration semicolons and
  named helper/part arguments. Comparisons now parse shifts on both sides,
  so `value < 1 << bits` means `value < (1 << bits)`.
- `ipc --check --diagnostics=json` checks without writing files.
  `--format ... --format-only` publishes canonical source without an
  executable; comments survive formatting.
- `ipc --describe-language` emits language description schema 2 (without
  `emoji_aliases`) and reports product, language, format, C API and ABI
  versions separately. Contract readers accept only the current schemas.
- The editor requires a 0.5 compiler: its fallback to 0.4 text diagnostics is
  removed.

The binary format stays 0.4: 0.5 adds no instructions or input types, and
frozen 0.1–0.4 files load and generate unchanged. The
[runtime matrix](optimization-acceptance-v0.5.md) records the preserved
images, outputs, traps and RNG across optimization, debug and compression
modes.

## C API 0.3 and hosts

API 0.3 adds sized descriptors and checked entry points beside the six frozen
API 0.2 functions, whose layouts and behavior are unchanged. Already-built
0.2 clients run against the 0.5 library. Query the loaded library with
`iprop_library_query` and test capability bits before using an addition.

- Checked load, metadata and result queries report `iprop_error_ex`: status,
  a stable reason code, the stage and VM location ([checked operations](c-api-checked.md)).
- Root input, output and part reflection describe a program before generation
  ([reflection](c-api-reflection.md), [outputs](c-api-output-declarations.md),
  [parts](c-api-parts.md), [metadata](c-api-metadata.md)).
- Parameter sets keep root input values per program load and reject unknown,
  duplicate or mistyped values when set; `iprop_generate_ex` generates from
  them ([parameters](c-api-parameters.md)).
- Image views, owner/name output lookup, typed point/rect/circle reads and
  result statistics read a result without reinterpreting packed arrays
  ([images](c-api-images.md), [result outputs](c-api-result-outputs.md),
  [statistics](c-api-statistics.md)).

The SFML host takes `--set name=value` inputs. The Godot `IpropAsset` exposes
`inputs`, `set_input` and `reset_inputs`; failures print the C API reason
code. The SDK ships two C99 examples in `share/iprop/examples/c`: a
reflection-only parameter panel and `render_inputs`, which sets inputs and
writes the generated image. `find_package(iprop 0.4 CONFIG REQUIRED)` accepts
the 0.5 package on the same ABI line.

## Removed

- Lexer emoji aliases (see above).
- The historical-0.4 contract reader profile; frozen 0.4 artifacts stay in
  the `v0.4.0` tag and `benchmarks/baselines/v0.4.0`.
- The editor's 0.4 text-diagnostics fallback.
- The LLM benchmark harness. `LLM_REFERENCE.md` remains generated
  documentation.
- Vendored stb and nlohmann/json: CLI tools write and read PNG through a
  small module over the runtime's zlib, and JSON through an ordered writer.

## Build and package

Build requirements and integration versions are unchanged from
[0.4](release-v0.4.md#build-and-package): zlib >= 1.2.9, SFML 3.1.0, Godot
4.5.2 with `godot-cpp` at `godot-4.5-stable` and Python 3.12 or newer for
packaging.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
scripts/release.sh 0.5.0 build --check
```

After committing the preparation changes, create and verify the source and
native archives, then the WASM and website archives:

```sh
scripts/release.sh 0.5.0 build
python3 scripts/package_web.py
```

Artifacts are `dist/iprop-0.5.0.tar.gz`, `dist/iprop-0.5.0-<os>-<arch>.tar.gz`
and `dist/iprop-0.5.0-SHA256SUMS.txt`, plus the WASM and website archives.
Both scripts extract their archives outside the build tree and rerun the
installed tools, C consumers and examples, conformance and website checks.
The documentation manifest in every archive names the same source commit.

## Final release gate

Inspect GitHub CI for the exact candidate commit on `develop`: Linux, macOS,
Windows, WASM, SFML/Godot and sanitizer jobs must pass. Then merge the
reviewed candidate to `main`, tag `v0.5.0` on that merge and push `main`
before pushing the tag. The [automatic release workflow](ci.md#automatic-releases)
rebuilds from that exact clean tagged commit, runs all CI and extraction
checks, and uploads and verifies a complete draft before publishing the
GitHub Release.
