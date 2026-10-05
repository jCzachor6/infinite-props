# C API: frozen ABI line 0.4 and additive extensions

The baseline is product `v0.4.0`, commit
`66d34c2831585afb231022da9296528a912e962e`, with header API `0.2`.
The existing shared-library loader identity is ABI line `0.4`.
This document freezes that contract and defines the rules for additive API
`0.3` and later API `0.4`. The runtime version query is implemented; other
extension entry points are identified by capability bits as they are added.
Documenting a planned entry point here does not make it available.

## Independent versions and compatibility direction

| Version | Meaning | Baseline / planned extension |
| --- | --- | --- |
| Product | Toolchain and distribution release | 0.4.0 / 0.5.0 |
| Source language | Accepted `.ips` syntax and diagnostics | 0.4 / 0.5 |
| Binary format | Executable `.iprop` representation and VM semantics | 0.4 / unchanged |
| Header C API | Available symbols and descriptors | 0.2 / additive 0.3 |
| C ABI line | Existing binary layouts, symbols and loader identity | 0.4 / unchanged |

An API 0.2 client built for a supported platform must continue to link and
run with an additive replacement of `ipropc` on that platform. This includes
an already-built shared client, not only recompilation with an old header.
A new client is not promised to run with a library lacking its new symbols.
Runtime capability queries introduced by API 0.3 describe the loaded library;
the header macros describe only the client's compile-time feature set.

The SONAME on ELF and corresponding macOS install-name / compatibility
identity must remain `0.4`. DLL exports and the C calling convention must
remain compatible on Windows. Product-versioned physical filenames may
change while the loader identity stays fixed. `cmake/CAbi.cmake` configures
the ABI line independently of `PROJECT_VERSION`. The shared target's
`SOVERSION`, generated runtime query and installed CMake package metadata
use this one configuration. `VERSION` continues to describe the physical
product release. DLL filenames and C export annotations remain unchanged.

CMake package version selection retains normal product-version, `EXACT`,
range and architecture checks, with an additional minimum-product boundary
for the ABI line. Its current floor is `0.4.0`: old `find_package(iprop 0.4)`
clients accept additive 0.5/0.6 packages on ABI line 0.4, while requests
predating that guaranteed baseline are rejected. An intentional ABI break
must update both the ABI line and the earliest compatible product. This
does not promise a C++ runtime ABI or run a new client on an older library.

Breaking this contract requires an explicit new ABI line, a migration and
old-client tests appropriate to the chosen boundary. Pre-1.0 version numbers
are not permission for an implicit break.

## API 0.2 declarations are frozen

The following six exported C symbols retain their existing signatures and
calling convention:

```c
iprop_status iprop_program_load(const uint8_t*, size_t,
                               iprop_program**, char*, size_t);
void iprop_program_free(iprop_program*);
void iprop_program_meta(const iprop_program*, iprop_meta*);
iprop_status iprop_generate(const iprop_program*,
                           const iprop_generate_options*,
                           iprop_result**, char*, size_t);
void iprop_result_free(iprop_result*);
void iprop_result_image(const iprop_result*, iprop_image*);
```

`iprop_program` and `iprop_result` remain opaque, library-owned handles.
Their internal C++ representations are not ABI. The definitions of
`iprop_uniform`, `iprop_generate_options`, `iprop_meta`, `iprop_output` and
`iprop_image` are ABI: field types/order, alignment, padding and array stride
must not change. Do not append even one field to an existing struct.

The existing enum types remain enums, with these numeric values:

- Status: `IPROP_OK=0`, `IPROP_E_OOM=1`, `IPROP_E_FORMAT=2`,
  `IPROP_E_TRAP=3`, `IPROP_E_ARGUMENT=4`.
- Uniform type: `COLOR=0`, `F32=1`, `U32=2`.
- Output kind: `POINT=0`, `RECT=1`, `CIRCLE=2`.

The supported distribution ABIs are the default 64-bit C ABIs used by
Linux x86-64, macOS arm64 and Windows x64 builds. They require 8-bit bytes,
4-byte IEEE-754 binary32 `float`, 4-byte `int` and enum representations,
8-byte pointers and `size_t`, and the platform's default C calling convention
and struct packing. Options such as `-fshort-enums`, non-default packing or
calling-convention switches are outside the supported contract. Additional
platforms require their own recorded and tested baseline; do not infer a
32-bit ABI from the 64-bit offsets below.

| Struct | Size / alignment | Field byte offsets on supported 64-bit ABIs |
| --- | --- | --- |
| `iprop_uniform` | 24 / 8 | name 0; type 8; f 12; u 16; r/g/b/a 20/21/22/23 |
| `iprop_generate_options` | 40 / 8 | seed 0; variant 4; archetype 8; size_x 12; size_y 16; uniforms 24; uniform_count 32 |
| `iprop_meta` | 32 / 8 | size_x 0; size_y 4; origin_x 8; origin_y 12; variants 16; archetypes 20; fuel 24 |
| `iprop_output` | 40 / 8 | kind 0; is_list 4; name 8; count 16; values 24; owner 32 |
| `iprop_image` | 32 / 8 | width 0; height 4; pixels 8; output_count 16; outputs 24 |

Record `sizeof`, alignment and every `offsetof` in platform conformance
checks. Checking only total size does not prove field compatibility.

## Legacy behavior and ownership

- Loading copies/owns the required binary storage. The caller may release
  the original byte buffer after the call.
- Successful load/generate assigns one new handle. Failure leaves the
  caller's existing output handle untouched and transfers no partial result.
  Passing a populated output variable on success does not free its previous
  handle; the caller must manage that handle before replacement.
- Freeing a null program or result is valid. Null inputs to the two legacy
  void queries produce no write.
- A generation requires a live program during the call. Its returned result
  owns its data independently, remains valid after `program_free`, and is
  not invalidated by another generation.
- Image pixels, output table, names and packed values are borrowed until
  `result_free`. The pixel format is RGBA8, row-major with top-left origin.
  Output slots remain in root-then-part order; owner 0 is root and owner
  `N+1` is code part `N`. Duplicate names under different owners are valid.
- Output values remain packed as 2/4/3 floats per point/rect/circle.
  Singular counts are 0/1; empty lists remain declared slots. A zero count
  does not promise a null values pointer and permits no element access.
- Legacy unknown uniform names are ignored, the first duplicate wins, and
  a declared uniform with an incorrect type produces `IPROP_E_TRAP`.
  Out-of-range variant/archetype indices retain their legacy trap behavior.
  New strict validation must use a separate entry point.
- Legacy error buffers need not be cleared on success and may remain
  untouched for bad arguments or OOM. New error descriptors define their
  own initialization policy without silently changing this contract.
- Existing handles are not shared between threads. Independent handles
  may be used concurrently; there is no global mutable error state.

## Rules for new API 0.3 descriptors and symbols

1. Add new entry points and descriptors. Preserve all legacy definitions;
   old functions may adapt to shared internals only with their old policy.
2. Use fixed-width integer tags/statuses/flags in new public layouts,
   rather than adding ABI-sized enums or C++ types. A new 32-bit status
   contract retains meanings 0 through 4 and distinguishes unexpected
   internal failures from argument, format, VM trap and OOM failures.
   New handles remain opaque; no STL, exceptions or allocator ownership
   crosses the C boundary.
3. Every extensible descriptor starts with `uint32_t struct_size`, the
   actual byte capacity supplied by the caller. Each function documents a
   minimum prefix and defaults for absent optional fields. Validate size
   before reading another field and read/write only complete fields inside
   both the caller's capacity and the known descriptor prefix.
4. Larger descriptors are accepted without touching unknown tail bytes.
   Output queries preserve `struct_size`; on success they write only their
   known available fields. Failure does not publish a partial query view or
   handle. Structured error output is the intentional exception: it reports
   the failure using its own validated available prefix.
5. Unknown requested flags are rejected. A future field requesting new
   behavior must be paired with a negotiated capability/feature flag, so an
   older library cannot silently ignore the request. Reserved input fields
   have defined zero defaults; output-only fields are not input requests.
6. Initialization is part of ABI safety. Use caller-side zero initialization
   with the size assigned, or a size-taking initializer that writes only
   the supplied capacity. Never export an initializer that assumes the
   caller allocated the library's newest `sizeof`.
7. `struct_size` does not fix array stride. Collections use count/get/find
   accessors, scalar copies, or an explicit byte stride with checked
   multiplication and per-item bounds. A borrowed array of sized elements
   must not imply `array[i]` uses the caller's newest `sizeof`.
8. Names and strings use UTF-8 pointers plus explicit byte lengths. Specify
   nullability and the owner of every view. Program metadata views expire
   with the owning program; result views expire with the result. Names
   obtained from a result remain valid after program destruction.
9. Parameters bind to root input IDs of one loaded program, initialize
   from its exact defaults and maintain necessary program data themselves.
   IDs are local to that load, not persistent identifiers across builds.
   Part-input reflection does not grant new host override semantics.
10. New error reporting is per call, with fixed reason/stage codes, explicit
    location validity and a caller-owned message buffer. Its basic handling
    path must not allocate. No exception crosses an entry point, and no
    global `last_error` is introduced. Unsupported cancellation/limits and
    shared-handle concurrency are not advertised before implementation.

These rules also govern API 0.4 extensions. A semantic change to legacy
calls is a compatibility decision even when the struct layout is unchanged.

## Evidence required before shipping an extension

Keep the released header and a separately built old binary client pinned to
the baseline commit. Run that binary against the replacement shared library
and check symbols, metadata, pixel/output bits and ownership on each
supported platform. Run a current C99 client against shared/static and moved
SDK installations. Add descriptor prefix/tail canaries, invalid flags and
stride tests, error-buffer/ownership regressions and allocation-failure
checks when their corresponding API is implemented. Tests must demonstrate
the promised boundary; rebuilding both sides with the newest header alone
does not verify old-binary compatibility.
