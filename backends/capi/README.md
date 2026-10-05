# The C API (`backends/capi/`)

The stable C interface every native host links against: SFML, Godot (the
GDExtension wraps this library), the tools and any engine integration. It
ships as a shared library and a static twin, both named `ipropc`.
On Windows the static archive is named `ipropc_static` to avoid colliding
with the DLL's import library. Shared consumers use the header's DLL import
annotations; static consumers define `IPROP_CAPI_STATIC` (the CMake target
propagates it automatically).

## Installation

```sh
cmake --build build --parallel
cmake --install build --prefix /path/to/iprop
```

An external CMake project uses `find_package(iprop 0.4 CONFIG REQUIRED)`
with that prefix in `CMAKE_PREFIX_PATH`, then links `iprop::capi` (shared)
or `iprop::capi_static`. The shared target can be consumed by a C-only
project. Static consumers enable C and C++ in `project(... LANGUAGES C CXX)`
and use the C++ linker (`LINKER_LANGUAGE CXX`) for the C++ runtime.
The installed DLL/import library and static archive have distinct names.
The released 0.4.0 shared library uses ABI line / SONAME `0.4`.
That historical loader identity is independent of the C API feature version;
additive API releases must retain it. The current build derives it from the
product version; separating that configuration is a subsequent implementation
step. See the [frozen ABI contract](../../docs/c-api-abi.md).

The prefix includes all six CLI tools, examples, specifications, language/
optimization/release documentation and the
enabled SFML/Godot integrations. Shared SFML libraries are included;
Linux graphics/font libraries and the Godot engine/export templates are
system dependencies. Godot's installed preset has empty template paths;
set them locally before exporting, following its README.

Version 0.4 accepts frozen 0.1–0.3 files and v0.4 compressed containers.
Loading expands and validates the complete file before generation; the
program owns the expanded bytes. C API version 0.2 and its struct layouts
are unchanged. CMake consumers also need zlib 1.2.9 or newer; Windows CI
links static zlib with the shared MSVC runtime (`x64-windows-static-md`).
When using this vcpkg triplet, set `ZLIB_USE_STATIC_LIBS=ON` and include its
prefix in `CMAKE_PREFIX_PATH` for both the library and external consumers.
This also recognizes the `zs`/`zsd` names used by zlib 1.3.2 on Windows.

## The surface

`iprop.h` is the whole contract — C99, no C++ in the ABI:

- `iprop_program_load(bytes, length, &program, err, err_size)` — loads and
  validates an `.iprop` binary. The program handle owns its buffer; it is
  reusable across generations.
- `iprop_program_meta(program, &meta)` — the declared size, origin,
  variants, archetypes and fuel budget.
- `iprop_generate(program, &options, &result, err, err_size)` — one
  (seed, variant, archetype, size, uniforms) tuple → RGBA8 pixels plus the
  named structured outputs.
- `iprop_result_image(result, &image)` — a borrowed view: width/height,
  the row-major RGBA8 pixels and the output slots (name, kind, list flag,
  packed float values and owner, in the global slot order of spec §4.3).
  `owner == 0` identifies the root; `owner == N+1` identifies part N.
- `iprop_program_free` / `iprop_result_free`.

Every pointer the host receives (pixels, output names, values) is borrowed
from the result handle and valid until `iprop_result_free`; the host never
allocates on the library's behalf. Failures return a status code
(`IPROP_OK` / `IPROP_E_OOM` / `IPROP_E_FORMAT` / `IPROP_E_TRAP` /
`IPROP_E_ARGUMENT`) and, when the caller passes a buffer, a located
message.

## The ABI policy

1. **One stable surface.** The C API is the only ABI the project
   guarantees; the C++ runtime, compiler and tools are implementation
   details and may change freely.
2. **Independent versions.** `IPROP_API_VERSION_MAJOR`/`_MINOR` describe
   the header's API feature set, currently `0.2`. Product, source language,
   binary format and ABI loader identity are separate contracts. API `0.3`
   is planned as an additive extension on ABI line `0.4`; an ABI break needs
   an explicit new ABI line and migration, including before version 1.0.
3. **Opaque handles.** `iprop_program`/`iprop_result` are incomplete
   types to consumers; their layout is private.
4. **Borrowed, not owned.** Pointers delivered to the host are valid
   until the owning handle's `_free`; handles are not shared between
   threads (no global state, so independent handles may run concurrently).
5. **Frozen legacy layouts.** The six API `0.2` symbols, public structs,
   enum values, calling convention and ownership are frozen. Adding even a
   trailing field changes the ABI, including array strides. API `0.2` added
   the output owner before this freeze; the `0.1` layout is not part of the
   current ABI guarantee. API `0.3` will use new sized descriptors and new
   entry points, leaving every existing declaration intact.
6. **Ownership is explicit.** Success hands exactly one handle to the
   caller to free; failure hands none.

The [ABI contract](../../docs/c-api-abi.md) records the released baseline,
supported layouts, legacy behavior and rules for sized descriptors, UTF-8
views, collection strides and safe initialization. It specifies planned
API `0.3` conventions; it does not imply those functions already exist.

## The conformance (task 6.2)

`test/conformance.c` runs C-only suites in `test/golden.c`, `test/errors.c`
and `test/generation.c`; common assertions and fixtures live in
`test/test_support.{h,c}`. The golden corpus (the portrait at five seeds,
shapes, noise, parts) is assembled to `.iprop` at build
time and driven through the C entry point, compared pixel-exactly against
the committed golden PNGs and the pinned structured outputs, and the
error paths are exercised (junk bytes → `IPROP_E_FORMAT` with a message;
null arguments → `IPROP_E_ARGUMENT`). Current `.ips` sources also cover
typed helpers, palettes, loop control, pinned math bits, empty lists,
output owners, plant composition, variant/archetype combinations and
size/uniform overrides. It runs under `ctest` as
`capi_conformance`.
