# Release 0.8.0 preparation

This document defines the content and checks prepared on `develop` for
0.8.0. Publishing is a separate step: the existing release workflow requires
the exact release tag in `main` history and a successful complete CI matrix.

## Release content

The [dated changelog](../CHANGELOG.md#v080--2026-10-28) supplies the GitHub
release notes. Product, editor, website, Unity and Python package versions
are 0.8.0, and the source-language version is 0.8.0. C API 0.4, ABI / SONAME
0.5 and the unversioned binary format stay unchanged.

- Frozen [format and VM contract](../spec/iprop-v0.8.md) and
  [material language contract](../spec/materials-0.8.md).
- [0.7 to 0.8 migration](language/migration-v0.8.md), updated grammar,
  handbook, compiler capability export and editor authoring support.
- [Material tree](../examples/language/material-tree.ips) and
  [shared library](../examples/language/materials/forest.ips), executable
  regression cases and native/WASM comparisons.
- Website documentation, current specification, search, versioned book and
  package manifests generated from the same compiler and source revision.
- Browser revalidation for stable website paths outside hashed `assets/`,
  including runtime files, showcase manifests and documentation data.

The frozen 0.7 specification is retained without edits. Materials lower to
scalar fields, so existing 0.7 binaries and host clients remain compatible.

## Verification on develop

Use a Release build with tests enabled. Keep generated documentation outside
a synchronized checkout using `IPROP_LANGUAGE_DOCS_DIR`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DIPROP_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
python3 scripts/release.py --check
```

The full [CI workflow](ci.md) checks Linux/macOS/Windows, the native backends,
sanitizers, Python, C#, Apple and Android integrations, native/WASM pixel and
output equality, schema contracts, installed SDK consumers and archive
extraction. The manual CI run must target the final `develop` commit and all
jobs, including `release-assets`, must succeed before release promotion.

The metadata check can run without creating a tag:

```sh
python3 -c 'import sys; sys.path.insert(0,"scripts"); from pathlib import Path; from release_preflight import metadata; print(metadata(Path("."), "v0.8.0", publishing=True))'
```

The full `release_preflight.py` command additionally requires an existing
tag on `main`; a metadata-only check does not prove that publication gate.

## Artifacts and publication boundary

The unchanged collector prepares 13 release files: the source archive,
three native SDKs, WASM and website archives, four extraction reports,
combined CI validation, a release manifest and the complete SHA256 list.
Every identity, version, digest and report must match the same commit.
Mobile/native binding source packages are included in SDKs; the Android
CI artifact also contains the tested AAR and device reports.

Preparing or pushing `develop` does not create `v0.8.0`, merge `main`,
publish a GitHub Release or deploy the production website. Those operations
follow the separately authorized publication workflow in [CI and publication](ci.md#automatic-releases).
