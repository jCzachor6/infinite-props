# Building and installing

Run source-build commands from the repository root. Native builds require
CMake 3.24+, C++20, C99 and zlib 1.2.9+. Python 3.10+ generates the handbook
and LLM reference; packaging requires Python 3.12+. Tests use Catch2, fetched
when `IPROP_FETCH_DEPS=ON` (the default).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

`IPROP_BUILD_TESTS=OFF` excludes tests. `IPROP_FETCH_DEPS=OFF` requires a local
Catch2 package when tests are enabled. `IPROP_SFML` and `IPROP_GODOT` each
accept `AUTO` (build when dependencies are available), `ON` (require them) or
`OFF` (exclude them). See [SFML](../backends/sfml/README.md) and
[Godot](../backends/godot/README.md) for their dependency/build settings.

`IPROP_RUNTIME_ONLY=ON` builds only the native runtime and C ABI, excluding
host tools, tests, engine dependencies and SDK installation. Mobile/plugin
builders use this mode. The [native binding guide](native-bindings.md) links
Apple, Unity, Android and Python build/package instructions.

## Generated documentation

With Python available, full native builds refresh the compiler-backed handbook.
Choose an empty directory for first generation or a directory owned by a previous
documentation manifest. A directory containing unrelated files is rejected.
For a separate output location, configure CMake with
`-DIPROP_LANGUAGE_DOCS_DIR=/absolute/path/to/language-docs`; the
[website](../website/README.md) accepts the same path through the environment.

```sh
python3 scripts/language_docs.py check --compiler build/tools/ipc
python3 scripts/language_docs.py build --compiler build/tools/ipc --output-dir build/language-docs
python3 scripts/language_docs.py verify --output-dir build/language-docs --against-source --compiler build/tools/ipc
```

The generated package contains expanded chapters, `docs/language-v0.7.md`,
`LLM_REFERENCE.md`, compiler catalog, schemas, examples and the current format
specification. Its manifest records independent versions, source identity and
file hashes. See [maintaining documentation](language/development.md).

## Native SDK

After a full build, install into a fresh prefix:

```sh
cmake --install build --prefix build/sdk
```

Tools are in `bin/`, C headers in `include/`, libraries and CMake exports in
`lib/` (or the platform's configured library directory), and documentation and
examples in `share/iprop/`. The generated chapters replace authored directives
when Python is available. CMake consumers use `find_package(iprop CONFIG REQUIRED)`;
integration and linking examples are in the [C API guide](../backends/capi/README.md).

The [WASM guide](../backends/html5/README.md) describes the Emscripten build.
[CI and publication](ci.md) documents candidate packages, extraction checks
and the separate requirements for freezing/tagging a release.
