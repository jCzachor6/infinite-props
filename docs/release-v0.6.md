# Release 0.6.0

Release date: 2026-10-08. Product, editor and website versions are 0.6.0;
source language 0.6, binary format 0.5, C API 0.3 and ABI line / SONAME 0.4
are separate contracts. The release tag is `v0.6.0` on `main`. Local
packaging and the tagged publication workflow are described below.

## Changes and migration

The [0.6 migration chapter](language/migration-v0.6.md) is the complete
reference; the [changelog](../CHANGELOG.md) lists every area.

- `c.r`, `c.g`, `c.b` and `c.a` read the channels of any color as `i32`.
- `lo to hi` draws a uniform number, both bounds included: `i32` for integer
  bounds, otherwise `f32`. Between two colors it draws one position on the
  RGBA gradient, and between two `vec2` values one point of the segment.
- The `distribution` type describes a gaussian, triangular or beta shape;
  `lo to hi with d` draws from it restricted to the range. Its kernels are
  pinned in binary64, so native and WASM agree bit for bit.
- Imported assets may import parts of their own. Paths resolve beside the
  importing file, each alias path is a separate instance, at most 256
  instances may result, and cycles are reported with the whole chain.
- `to`, `with`, `distribution`, `gaussian`, `triangular` and `beta` stay
  usable as names; existing locals and helpers keep their meaning.

One change affects accepted 0.5 sources: **part outputs are now in the
caller's coordinates.** A parent reads `alias.outputs.name` with the child's
transform and placement applied, and hosts receive every part output in root
asset coordinates. Recompiled sources that added the placement themselves
must drop that addition. Frozen 0.1–0.4 files keep their local values.

The compiler writes format 0.5 when a file uses a 0.5 instruction or has
parts, otherwise 0.4. A runtime from product 0.5.0 or older rejects a 0.5
file by its version, so recompiled assets with parts need the 0.6 runtime.
The [runtime matrix](optimization-acceptance-v0.6.md) records the preserved
images, outputs, traps and RNG of the frozen files and of every source across
optimization, debug and compression modes, with the two released sources
whose meaning changes on purpose.

`ipc --describe-language` keeps schema 2 and adds optional fields:
`source_rules.types.components`, `source_rules.random_range` and the
`distribution` type; `restrictions.nested_part_imports` is now `true`.
Embedding hosts that call `compileSource` may pass an import resolver; the
default resolves lexically, and `ipc` resolves on the file system.

## C API 0.3 and hosts

The C API stays 0.3 with unchanged functions and layouts, and already-built
0.2 clients still run against the 0.6 library. The existing part descriptor
now reports a format 0.5 instance name, its alias path such as `hilt.grip`,
with `name_present == 1` ([parts](c-api-parts.md)); older files report none.
Result outputs of parts are in root asset coordinates for format 0.5 files
([result outputs](c-api-result-outputs.md)). The SFML, Godot and HTML5 hosts
need no changes and generate the new files unchanged.

The repository adds a three-level example, `examples/colony`, and the
`examples/previews` scenes for gradients, distributions and segments.

## Build and package

Build requirements and integration versions are unchanged from
[0.5](release-v0.5.md#build-and-package): zlib >= 1.2.9, SFML 3.1.0, Godot
4.5.2 with `godot-cpp` at `godot-4.5-stable` and Python 3.12 or newer for
packaging.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
scripts/release.sh 0.6.0 build --check
```

After committing the preparation changes, create and verify the source and
native archives, then the WASM and website archives:

```sh
scripts/release.sh 0.6.0 build
python3 scripts/package_web.py
```

Artifacts are `dist/iprop-0.6.0.tar.gz`, `dist/iprop-0.6.0-<os>-<arch>.tar.gz`
and `dist/iprop-0.6.0-SHA256SUMS.txt`, plus the WASM and website archives.
Both scripts extract their archives outside the build tree and rerun the
installed tools, C consumers and examples, conformance and website checks.
The documentation manifest in every archive names the same source commit.

## Final release gate

Inspect GitHub CI for the exact candidate commit on `develop`: Linux, macOS,
Windows, WASM, SFML/Godot and sanitizer jobs must pass. Then merge the
reviewed candidate to `main`, tag `v0.6.0` on that merge and push `main`
before pushing the tag. The [automatic release workflow](ci.md#automatic-releases)
rebuilds from that exact clean tagged commit, runs all CI and extraction
checks, and uploads and verifies a complete draft before publishing the
GitHub Release.
