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
The shared library uses ABI line / SONAME `0.4`, first released with 0.4.0.
That historical loader identity is independent of the C API feature version;
additive API releases must retain it. `cmake/CAbi.cmake` independently defines
that line and the earliest compatible product (`0.4.0`). The build and runtime
library query consume the same configuration. Physical filenames and macOS
current-version metadata may follow the product, while ELF SONAME / macOS
install-name remain `0.4`; Windows keeps `ipropc.dll` and its C exports.
See the [frozen ABI contract](../../docs/c-api-abi.md).

Installed CMake packages expose `iprop_C_ABI_SOVERSION` and its major/minor
components. `find_package(iprop 0.4 CONFIG REQUIRED)` remains compatible with
later additive products on that ABI line, including 0.5/0.6. A requested
minimum older than product 0.4 is rejected; a version newer than the installed
product and a nonmatching `EXACT` request are rejected by the normal CMake
version checks. Version ranges keep their usual inclusive/exclusive bounds.
The compatibility promise applies to the C API; exported C++ runtime headers
and targets remain implementation details.

The prefix includes all six CLI tools, examples, specifications, language/
optimization/release documentation and the
enabled SFML/Godot integrations. Shared SFML libraries are included;
Linux graphics/font libraries and the Godot engine/export templates are
system dependencies. Godot's installed preset has empty template paths;
set them locally before exporting, following its README.

Current documentation uses manifest schema 2 with separate product, source
language, emitted format, C API and C ABI versions. The generated handbook is
`share/iprop/docs/language-v0.7.md`, named after the source language. See
[export contracts and the schema-1 migration](../../docs/export-contracts.md).

The runtime accepts frozen 0.1–0.5 files, v0.4 compressed containers and
format 0.6 math emitted by the 0.7 development compiler. C API and ABI layouts
remain unchanged; [stage 1 notes](../../docs/language/migration-v0.7.md) describe
the current implementation scope.
Loading expands and validates the complete file before generation; the
program owns the expanded bytes. The six legacy C API 0.2 entry points and
their struct layouts are unchanged by additive API 0.3. CMake consumers
also need zlib 1.2.9 or newer; Windows CI
links static zlib with the shared MSVC runtime (`x64-windows-static-md`).
When using this vcpkg triplet, set `ZLIB_USE_STATIC_LIBS=ON` and include its
prefix in `CMAKE_PREFIX_PATH` for both the library and external consumers.
This also recognizes the `zs`/`zsd` names used by zlib 1.3.2 on Windows.

## C99 examples

[`examples/c/parameter_panel.c`](../../examples/c/parameter_panel.c) builds a
host-owned model from root-input reflection. Rows keep the resolved root ID,
stored type, exact default bits and a copy of every name byte. The example
frees the program before printing those rows. Empty names and embedded NUL
are preserved; names are displayed as hex with a byte count. Source `i32`
aliases appear as U32 bit patterns. The schema supplies no UI ranges or
recoverable signed source type. Borrowed program views expire at
`iprop_program_free`, even if a parameter handle retains internal program data.

[`examples/c/render_inputs.c`](../../examples/c/render_inputs.c) renders an
asset with host-chosen inputs: `iprop_program_load_ex`, one parameter set
filled from `name=value` arguments through the typed named setters (by the
type `iprop_parameters_get_named` reports), `iprop_generate_ex`, the borrowed
image view written as an RGBA PAM file and the result statistics. Every
failure prints the structured status, reason, stage and VM location, e.g.
`missing=1 failed: input name was not found (status 4, reason 4005, stage 3)`.

The sources and their standalone CMake project ship in
`share/iprop/examples/c` in the installed SDK. Build them from a relocated SDK,
compile a real source fixture and run both linkages (`SDK` is the relocated
prefix):

```sh
cmake -S "$SDK/share/iprop/examples/c" -B panel-build -DCMAKE_PREFIX_PATH="$SDK"
cmake --build panel-build --config Release
"$SDK/bin/ipc" "$SDK/share/iprop/examples/recipes/24-uniform-emblem.ips" -o panel.iprop
./panel-build/parameter_panel_shared panel.iprop
./panel-build/parameter_panel_static panel.iprop
./panel-build/render_inputs_shared panel.iprop emblem.pam accent=#ff8800 density=3
```

With a multi-configuration generator the executables are under
`panel-build/Release`; Windows shared execution also needs the SDK's `bin`
directory in `PATH`. The static target propagates `IPROP_CAPI_STATIC` and its
runtime dependencies. The panel reflects declarations without generating an
image or constructing parameter handles.

The standalone project uses `find_package(iprop 0.4 CONFIG REQUIRED)` and
links `iprop::capi` / `iprop::capi_static`; the static executable selects the
C++ linker, while its source is still compiled as C99.

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
- `iprop_image_view_ex_init` / `iprop_result_query_image` — sized borrowed
  image storage under `IPROP_CAP_IMAGE_VIEW`, with actual dimensions, byte
  count, tight row stride, RGBA8 format and explicit top-left/legacy alpha
  semantics. The pointer and bytes match the unchanged legacy image view.
  See [image views](../../docs/c-api-images.md).

- `iprop_result_output_count/get/find` — every generated slot in original
  global order, exact owner/name lookup and sized views of legacy pointers.
  `iprop_result_output_point/rect/circle` copy one checked typed element with
  explicit capacity. See [result outputs](../../docs/c-api-result-outputs.md).

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
   the header's API feature set, currently `0.3`. Product, source language,
   binary format and ABI loader identity are separate contracts. API `0.3`
   extends API `0.2` on ABI line `0.4`; an ABI break needs
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
   current ABI guarantee. API `0.3` uses new sized descriptors and new
   entry points, leaving every existing declaration intact.
6. **Ownership is explicit.** Success hands exactly one handle to the
   caller to free; failure hands none.

The [ABI contract](../../docs/c-api-abi.md) records the released baseline,
supported layouts, legacy behavior and rules for sized descriptors, UTF-8
views, collection strides and safe initialization. It specifies the API `0.3`
conventions; the capability query identifies the loaded library's functions.

## The loaded library

Header macros identify the API used to compile a client. Query the actual
loaded library before choosing optional behavior:

```c
iprop_library_info_ex info = {0};
info.struct_size = sizeof(info);
if (iprop_library_query(&info) == IPROP_EX_OK) {
    /* product_major/minor/patch, api_major/minor, abi_major/minor,
       format_major + inclusive format_minor_min/max, capabilities */
}
```

The descriptor may be a shorter prefix or a larger future allocation. The
query requires four bytes for `struct_size`, preserves that value and writes
only complete known fields inside the declared capacity. Unknown tail bytes
are untouched; failure leaves the descriptor unchanged. There is no borrowed
storage or global error state. Initialize it with caller-side zeroing plus
the actual capacity, or with `iprop_library_info_ex_init(&info, sizeof(info))`.
The initializer also writes only complete known fields in that allocation.

`iprop_generate_options_ex` is separate from the old generation options.
Use `iprop_generate_options_ex_init(&options, sizeof(options))` to initialize
seed, variant, archetype and dimensions to zero (source dimensions), with
no requested flags. The common options reader supplies these defaults for
absent fields and rejects all nonzero flags currently. It snapshots values
before passing them to execution; failed validation publishes no partial
options. Its public generation consumer is added in the parameters stage.
Future input fields requesting behavior must use a recognized flag and
negotiated capability. A nonzero unknown tail alone does not request behavior.

`iprop_error_ex` establishes typed status/reason/stage and optional location
fields. `iprop_error_ex_init(&error, sizeof(error))` initializes success with
absent locations. Container and expanded offsets have explicit coordinate
spaces; function/PC and a usable debug line have presence bits. No filename
or column is invented. See the [error contract](../../docs/runtime-errors.md).
The descriptor now accepts a caller-owned `message_buffer`/`message_capacity`
and reports full `message_required_bytes` (excluding NUL) and truncation.
The common error sink clears success and renders argument/OOM/runtime messages
without allocating. Checked load/program/library queries now consume this
policy, as does `iprop_generate_ex`. See the
[implemented checked operations](../../docs/c-api-checked.md).

The shared new-operation boundary distinguishes typed argument/setup errors,
load/validation errors, actual execution traps, OOM and unexpected internal
exceptions. Legacy calls retain their old classification. See the
[status policy](../../docs/runtime-errors.md#new-status-policy-and-exception-boundary);
the shared runtime core is consumed by checked load/query adapters and the
legacy adapters, each with its documented policy.

New collections use count/get/find accessors returning one caller-sized
descriptor at a time. They do not return arrays whose stride depends on a
newer `sizeof`. All initializers reject null or capacities below four without
a write, preserve unknown tail/padding bytes, and require no allocation.

The development API reports library-query/sized-descriptor capabilities and
`IPROP_CAP_PROGRAM_LOAD_EX`, `IPROP_CAP_PROGRAM_QUERY`,
`IPROP_CAP_STRUCTURED_ERRORS`, `IPROP_CAP_LIBRARY_QUERY_EX` for implemented
checked load/metadata queries. `IPROP_CAP_ROOT_INPUT_REFLECTION` advertises
implemented root-input count/get/find and its sized declaration initializer;
see [exact name/default reflection](../../docs/c-api-reflection.md).
`IPROP_CAP_OUTPUT_DECLARATIONS` advertises the stored root/part output schema,
including empty slots, through count/get/find by owner and exact name; see
[output declarations](../../docs/c-api-output-declarations.md).
`IPROP_CAP_PART_REFLECTION` advertises the stored part table and its input
signatures; see [part geometry and signatures](../../docs/c-api-parts.md).
`IPROP_CAP_METADATA_EXTENDED` advertises appended emission and physical
container fields in the existing program descriptor; see
[extended metadata](../../docs/c-api-metadata.md).
`IPROP_CAP_RESULT_STATISTICS` advertises the sized completed-result query for
VM fuel, owned pixel bytes and output element count; see
[result statistics](../../docs/c-api-statistics.md).
`IPROP_CAP_RESULT_OUTPUTS` and `IPROP_CAP_TYPED_OUTPUTS` independently advertise
the [generated slot views and typed copies](../../docs/c-api-result-outputs.md).
`IPROP_CAP_PARAMETERS` advertises retained root parameter handles with generic
get/set, clone and reset; see [parameter values](../../docs/c-api-parameters.md).
`IPROP_CAP_PARAMETERS_TYPED` separately advertises scalar/RGBA8 setters by
resolved ID or exact UTF-8 name, including the U32 bit helper for signed i32.
`IPROP_CAP_PARAMETER_OVERRIDES` advertises strict override lists with an
explicit element stride: unknown, duplicate, mistyped or malformed elements are
rejected with stable reasons before any value changes.
`IPROP_CAP_GENERATE_EX` advertises checked generation from those validated
parameters. Typed output value accessors receive capabilities only when
implemented.
Cancellation, host memory limits and shared-handle concurrency are not
advertised by this release. A larger API minor number alone does not imply
those capabilities.

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
