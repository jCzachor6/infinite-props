# Noise extensions for 0.9

**Status:** implemented stage 02 contract on `develop`; the frozen
[0.8 specification](iprop-v0.8.md) remains unchanged. These additions will
be incorporated into the full 0.9 specification before release.

## Instructions and language calls

Inputs below are listed deepest first; aux is zero and the unused operand
has the existing `OperandKind::None` behavior. All four instructions are pure
seeded computations, may trap with `NumericRange`, consume no stream draws,
and can be evaluated by the constant evaluator. Scalar and vec2 forms share
the same instruction after extracting x and y once in signature order.

| Opcode | Mnemonic | Stack inputs | Result | IPS call |
| --- | --- | --- | --- | --- |
| 0xDF | `noise.cell2` | f32 x, f32 y, i32 seed | f32 | `noise.cell2(x,y,seed)` / `noise.cell2(p,seed)` |
| 0xE0 | `noise.cell_id` | f32 x, f32 y, i32 seed | i32 | `noise.cell_id(x,y,seed)` / `noise.cell_id(p,seed)` |
| 0xE1 | `noise.warp_vec` | f32 x, f32 y, f32 amount, i32 seed | vec2 | `noise.warp_vec(x,y,amount,seed)` / `noise.warp_vec(p,amount,seed)` |
| 0xE2 | `noise.fbm_config` | f32 x, f32 y, i32 octaves, f32 lacunarity, f32 gain, i32 seed | f32 | `noise.fbm(x,y,octaves,lacunarity,gain,seed)` / `noise.fbm(p,octaves,lacunarity,gain,seed)` |

The first two overloads of `noise.fbm` keep their original catalog order and
opcode 0x8A. The new overloads are appended. No header/version field, type,
section, original opcode or old reference vector changes. A runtime lacking
these instructions rejects them during validation, including unreachable
instructions. Nonzero aux is invalid on new and old fixed instructions.

## Arithmetic and domains

Each arithmetic operation below rounds to binary32; contraction is disabled.
The hash and jitter formulas, gradient table, interpolation and octave seed
salt `0x9e3779b9` are exactly those in the 0.8 contract. Integer seed products
and additions wrap modulo 2^32.

### Worley F1, F2 and winner

Require finite x/y with floors in `[-16777214,16777214]`, then scan cy from
`floor(y)-2` through `floor(y)+2`, and cx from `floor(x)-2` through
`floor(x)+2`. Jitter still clamps a rounded upper endpoint with
`nextafter(coordinate,-infinity)`; origins must remain exactly representable.
For each distinct source cell compute `rx=px-x`, `ry=py-y`, and
`d=sqrt(rx*rx+ry*ry)` in the original order. Do not sort squared distances or
postpone sqrt: rounding can create an observable tie.

Initialize F1/F2 to positive infinity. If `d<F1`, move F1 into F2, then set
F1=d and winner=`cellHash(seed,cx,cy)`. Otherwise if `d<F2`, set F2=d.
An equal F1 therefore counts as the second distance, without replacing the
winner. Ascending `(cy,cx)` determines ownership. `cell_id` reinterprets the
winner hash bits as signed i32, with collisions allowed. All 25 candidates
are visited in a single scan. Legacy `noise.cell` reads F1 from this kernel
with unchanged arithmetic and traps.

The three actual equal-distance witnesses in the frozen
`tests/fixtures/compat-v0.8.json` vectors have winners
`(0,0)` rather than `(1,0)`; their IDs are therefore `hash32(seed)`.

### Configurable fBm

Validate lacunarity first (finite, `0<lacunarity<=8`), then gain (finite,
`0<=gain<=1`), reporting `fbm lacunarity out of range` / `fbm gain out of range`.
Clamp octaves to 1–8. If gain compares equal to zero, sample exactly one
octave, including for negative zero; otherwise sample the clamped count.
Start sum=0, total=0, amplitude=1, frequency=1. In ascending octave index i:

```text
g = grad(x*frequency, y*frequency, seed + u32(i)*0x9e3779b9)
sum = sum + amplitude*g
total = total + amplitude
amplitude = amplitude*gain
frequency = frequency*lacunarity
result = sum/total
```

Every performed gradient sample validates each scaled coordinate as finite
with floor in `[-2147483648,2147483646]`. Zero amplitude caused by later
underflow does not skip samples; only the explicit gain-zero path does.
Frequency underflow to zero is valid. Default parameters 2/0.5 match old fBm
bits and domains, but the old instruction remains separate.

### Coordinate warp

Validate finite amount first (`warp amount out of range`), then x/y in the
base gradient domain. If amount compares equal to zero, return x/y bits
without any arithmetic, offset or samples. This accepts a base-domain point
whose unexecuted higher octaves or offset would have failed. It still rejects
invalid base coordinates. Otherwise compute, in order:

```text
qx = legacy_fbm(x, y, 4, seed)
qy = legacy_fbm(x + 3.7f, y + 9.1f, 4, seed)
wx = x + amount*qx
wy = y + amount*qy
```

Validate wx, then wy in the base gradient domain and return `{wx,wy}`.
Each performed inner octave also validates its scaled coordinates. There
is no outer fBm and thus no validation of hypothetical outer octaves.
Coordinate-domain failures report `noise coordinate out of range`.
Legacy `warp` retains all three fBms, even at zero amount, and its old order
of checks. It is not implemented by calling `warp_vec`.

## Interim stage 02 fuel

Reserve before scanning/sampling. Counts include the VM's one dispatch unit;
argument instructions and vec2 extraction are charged separately:

| Operation | Total native fuel |
| --- | ---: |
| cell2 / cell_id | 25 (one full 5×5 scan) |
| configurable fBm | clamp(octaves,1,8), even when gain=0 |
| warp_vec, amount nonzero | 8 (two four-octave fields) |
| warp_vec, amount zero | 1 |

Legacy fuel is unchanged at this stage. Stage 13 will integrate the full
runtime tariff and the independent native-work limiter. Constant evaluation
uses the same bounded kernels; no runtime sample is emitted for folded
calls. Failed generation exposes no
successful result. Insufficient reserved fuel may precede a numeric error.

## Executable evidence

The extension vectors in `tests/fixtures/noise-v0.9.json` pin exact argument
and result encodings. An independent binary32 JS reference recomputes them,
including sorting Worley candidates rather than sharing the runtime's scan.
The runner `backends/html5/test/noise-v09-conformance.js`
checks runtime scalar/vec2 calls, constant scalar/vec2 calls, compiler
configurations, assembly roundtrips, boundary traps and malformed bytecode
on native and WASM. Native tests in `tests/math/noise_extensions_test.cpp`
also check clamping, recurrence, signed zero, evaluation order and exact
fuel thresholds. The unchanged 0.8 gate runs alongside these tests.
