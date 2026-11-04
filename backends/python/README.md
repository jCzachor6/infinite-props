# Python

A Python 3.10+ package using stdlib `ctypes` and the native C API 0.6 / ABI 0.5.
The runtime library is loaded separately; Pillow is an optional image-export
dependency. See the [shared contract](../../docs/native-bindings.md).

## Install

Build/install the [native SDK](../capi/README.md), then from the source root:

```sh
python3 -m pip install './backends/python[images]'
```

Without Pillow use `pip install ./backends/python`. SDK installations contain
the package source under `share/iprop/backends/python` as well.
Pass `library=/absolute/path/to/library` to `Program`, set `IPROP_LIBRARY`,
or install the library on the system's normal loader path. Native library
names are `libipropc.dylib`, `libipropc.so.0.5` or `ipropc-0.5.dll`.

## Use

```python
from infinite_props import Program, Color, F32, I32

with Program("stone.iprop") as program:
    image = program.generate(7, inputs={"tint": Color(138, 146, 153)})
    origin = (program.metadata.origin_x, program.metadata.origin_y)
    print(program.inputs)

image.to_pillow().save("stone.png")
collider = image.output("collider", owner=0)
```

Overrides use explicit `Color`, `F32`, `U32` or `I32` values. Each generation
starts with defaults; values are never coerced to a different declared type.
`F32.from_bits(...)` preserves exact binary32 input bits. Outputs expose
packed `values` as Python floats and `value_bits` as exact uint32 leaves,
including NaN payloads and negative zero. Results are immutable copies and
survive context-manager exit. Calls and close serialize per program.

`generate(work_limit=0, timeout_ns=0, cancellation=None)` controls native
execution independently of fuel. Zero work selects the finite default; zero
timeout disables the monotonic clock. `program.cancellation_token()` returns
a one-way token whose `cancel()` may be called from another thread while
generation is running; it uses a separate native atomic flag and is freed
automatically after its last reference. A token can also be constructed with
`CancellationToken(library=...)` for the same runtime library. Errors keep
their structured reasons (3015 work, 3016 cancellation, 3017 deadline).
Completed statistics include `work_budget`, `work_remaining` and `work_used`.

`output list part` slots have `kind == OutputKind.PART`, an ordered `parts`
tuple and no packed float values. Each `PartSnapshot` exposes the complete
[C API record](../../docs/c-api-part-outputs.md), including source/texture
mapping, effective D4, integer translation, scales, anchor and bounds.
`image.resource(part.shape_resource)` resolves an entry in `image.resources`;
zero denotes an absent optional texture, and `region_index == 0xffffffff`
denotes a raster shape. A resource is copied once per result-local ID and
contains either exact A8/RGBA `data`, or `rings` and `regions` with explicit
hole indices. Tuples/bytes are immutable and survive native result/program
destruction and later generations. The wrapper negotiates both capabilities
and enables typed generation automatically.

`to_pillow()` copies top-left RGBA bytes and interprets stored channels as
straight RGBA for presentation. Raw data retains the runtime's legacy
destination alpha tag, as described in the shared guide.

The CLI accepts repeated named overrides and prints the existing
point/rect/circle geometry/statistics JSON. Programs declaring part outputs
receive an explicit error before writing an image; use the Python result API
for full typed data. No persisted result format is introduced.

```sh
iprop-python stone.iprop stone.png --seed 7 --input 'tint=#8a9299'
# Equivalent: python3 -m infinite_props ...
```

## Verify

Prepare shared fixtures, install the package and set the library path:

```sh
IPROP_FIXTURES="$PWD/build/binding-fixtures" \
IPROP_LIBRARY="$PWD/build/backends/capi/libipropc.dylib" \
python3 -m unittest discover -s backends/python/tests -v
```

Choose the corresponding library name on Windows/Linux. Tests use the
actual library, including Unicode/NUL names, raw f32 bits, ownership, errors,
size overrides and concurrent calls. With Pillow installed, verify image
conversion and CLI exports as well.

Replay the complete [shared pixel corpus](../../docs/native-bindings.md#required-pixel-equality-gate)
through Python and Pillow after generating it:

```sh
python3 backends/test/check-pixel-corpus.py build/backends/capi/libipropc.dylib build/pixel-corpus
```
