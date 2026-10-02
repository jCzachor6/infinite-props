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

An external CMake project uses `find_package(iprop 0.3 CONFIG REQUIRED)`
with that prefix in `CMAKE_PREFIX_PATH`, then links `iprop::capi` (shared)
or `iprop::capi_static`. The shared target can be consumed by a C-only
project. Static consumers enable C and C++ in `project(... LANGUAGES C CXX)`
and use the C++ linker (`LINKER_LANGUAGE CXX`) for the C++ runtime.
The installed DLL/import library and static archive have distinct names.
The shared library SONAME includes the pre-1.0 minor ABI version.

The prefix includes all six CLI tools, examples, specifications and the
enabled SFML/Godot integrations. Shared SFML libraries are included;
Linux graphics/font libraries and the Godot engine/export templates are
system dependencies. Godot's installed preset has empty template paths;
set them locally before exporting, following its README.

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

## The ABI policy (task 6.3)

1. **One stable surface.** The C API is the only ABI the project
   guarantees; the C++ runtime, compiler and tools are implementation
   details and may change freely.
2. **Versioned.** `IPROP_API_VERSION_MAJOR`/`_MINOR` track the API. The
   major bumps on any ABI change. Pre-1.0 releases may break the ABI
   freely — the library and the host are always built from the same
   header (no runtime dispatch, no symbol versioning yet).
3. **Opaque handles.** `iprop_program`/`iprop_result` are incomplete
   types to consumers; their layout is private.
4. **Borrowed, not owned.** Pointers delivered to the host are valid
   until the owning handle's `_free`; handles are not shared between
   threads (no global state, so independent handles may run concurrently).
5. **Struct layout is ABI.** Adding even a trailing field changes the ABI
   (including array strides). API 0.2 adds the output owner; consumers must
   rebuild against this header. A future stable ABI will need sized structs
   or versioned entry points before extending layouts.
6. **Ownership is explicit.** Success hands exactly one handle to the
   caller to free; failure hands none.

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
