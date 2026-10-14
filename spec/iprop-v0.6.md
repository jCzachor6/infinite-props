# `.iprop` Format and Virtual Machine — Specification v0.6

**Status:** Development contract for product 0.7.0, stages 1–11 (2026-10-13).
**Scope:** Signed integer bounds, vector normalization, immutable collections, captured and constant samplers, sampling palettes, weighted and density distributions, shared render targets, captured layers, source-over alpha, coverage masks, clipping and disk morphology. This document
inherits [v0.5](iprop-v0.5.md), with the additions below taking precedence.
Every existing opcode number, type code, section layout, math kernel, RNG,
canvas operation, output and fuel rule remains frozen. Later 0.7 stages may
extend this development format; resource
exports use the separate table in section 17. Scalar asset constants add no binary layout or instruction.

## 1. Versions and containers

A runtime supporting this contract accepts formats 0.1–0.6. Plain 0.6 files
use the same header and retain every existing section/type encoding from 0.5.
The added instructions and collection sections require minor 6, including in
unreachable code. Validators reject a newer
instruction in every older minor rather than ignoring it.

Writers choose the oldest minor needed by the final instructions, function
types and sections, never older than 0.4. A new source builtin folded to
constants does not require 0.6. Files retaining 0.5 instructions or parts
still require 0.5; other files can remain 0.4.

The compressed envelope remains format 0.4, with unchanged 64 MiB encoded
and decoded bounds. It may wrap an uncompressed inner 0.6 file. The compressed
flag is invalid on a plain 0.6 header. A 0.5 runtime rejects inner 0.6 by its
version after expansion. Debug stripping, assembly and disassembly preserve
the required inner version.

## 2. Signed integer bounds

| Op | Mnemonic | Stack | Semantics |
| -- | -------- | ----- | --------- |
| 0xB1 | i32.min | `[i32 i32] -> [i32]` | lesser signed value |
| 0xB2 | i32.max | `[i32 i32] -> [i32]` | greater signed value |
| 0xB3 | i32.clamp | `[i32 i32 i32] -> [i32]` | bound x between a and b |

For `i32.clamp`, the stack order is x, a, b (deepest first):

```text
lo = signed_min(a, b)
hi = signed_max(a, b)
return signed_min(signed_max(x, lo), hi)
```

These are exact signed comparisons over the full i32 range. No arithmetic
overflow or conversion to f32 occurs. Equal and reversed bounds are valid.
The operations never trap intrinsically, read or advance no RNG and cost the
existing base instruction cost of 1 fuel.

The frozen `f32.min`, `f32.max` and `clamp` instructions retain their existing
NaN, signed-zero and operation-order rules. Source overload selection is
defined in [the 0.7 migration chapter](../docs/language/migration-v0.7.md).

## 3. Stable vector normalization

| Op | Mnemonic | Stack |
| -- | -------- | ----- |
| 0xB4 | vec2.normalize | `[vec2] -> [vec2]` |

Both input components must be finite; otherwise execution traps with
`NumericRange` and `vec2.normalize components must be finite`. A zero vector,
including any combination of signed zeros, returns `{+0, +0}`.

For a nonzero finite vector, convert each input exactly to IEEE binary64
and execute these steps in the written order, round-to-nearest ties-to-even:

```text
m = max(abs(x), abs(y))
sx = x / m
sy = y / m
xx = sx * sx
yy = sy * sy
length = sqrt(xx + yy)
return {binary32(sx / length), binary32(sy / length)}
```

Each division, multiplication, addition and square root above is binary64.
Only the final components round to binary32. FMA contraction and flushing
subnormals to zero are forbidden. Scaling prevents overflow/underflow of
the length for extreme finite f32 inputs; the final component may still
round to a subnormal or signed zero. A nonzero vector preserves the sign of
an input zero component through the divisions.

Normalization reads or advances no RNG, writes no canvas or output and costs
1 fuel. Its numeric trap is retained even when its result is unused. Constant
evaluation uses the same kernel for finite inputs; nonfinite operations stay
in the executable so that optimization preserves the runtime trap.

| Input component bits | Result component bits |
| --- | --- |
| `00000000,80000000` | `00000000,00000000` |
| `40400000,40800000` (3,4) | `3F19999A,3F4CCCCD` |
| `7F7FFFFF,7F7FFFFF` | `3F3504F3,3F3504F3` |
| `00000001,00000002` | `3EE4F92E,3F64F92E` |
| `3F800000,00000001` | `3F800000,00000001` |

## 4. Asset constants and format selection

Source `const` declarations are evaluated before lowering. Scalars use the
existing constant pools; vec2 values use two f32 constants and `vec2.make`.
Distribution descriptions use existing parameter constants and their 0.5
constructor instructions. Runtime constructors retain their checks and cost;
reading a description performs no RNG draw. Unused declarations add no
executable instructions, but invalid values and cyclic/effectful dependencies
are still compilation errors.

Constant evaluation uses the existing runtime value-operation adapters, so
math, conversions and constructor validation share the executable's kernels.
It cannot dispatch helpers, parts, canvas operations or RNG instructions.
Scalar const adds no required format version: even a constant computed with a 0.6
math operation can use a 0.4 executable. See the
[source declaration contract](../docs/language/assets.md#asset-constants).

## 5. Host compatibility and verification

C API remains 0.3 and ABI/SONAME remains 0.4. No public enum or struct layout
changes; the new operations return existing scalar/vector values and publish
existing geometric output slots. Collections are internal helper/local values.

The stage 1 gates cover opcode typing, aux/operand validation, old-format
rejection including unreachable code, IPA/IPDIS, compressed inner 0.6,
formatter roundtrip, exact i32 values, mixed float calls, normalization edges,
NaN/infinity traps, optimizer behavior and native/WASM output bits.
`tests/fixtures/frozen-format-v0.5.json` stores pre-change executable bytes
from commit e053240 and pins RGBA, output bits, inputs and fuel. The earlier
golden corpus and immutable 0.4 baseline remain independent compatibility
gates. Recompiled-source migrations are checked separately.

## 6. Type descriptors and collection constants

Optional section **6 (`types`)** is a `u32 count`, followed by `count` records:
`u8 kind`, `u8 element`, `u16 reference_or_reserved`. The following records are
supported; all stage-3 list records retain their exact bytes:

| Kind | Element byte | Final u16 | Type |
| --- | --- | --- | --- |
| 5 (`list`) | scalar 0–4 | zero | list of i32/f32/color/vec2/distribution |
| 10 (`sampler`) | scalar 0–3 | zero | sampler of i32/f32/color/vec2 |
| 5 (`list`) | 0x80 | index of an earlier sampler descriptor | list of sampler<T> |

The referenced sampler must precede the list record. Forward, dangling, cyclic,
list-of-list and sampler-of-resource references are invalid. Codes 6–9 are not
wire descriptor kinds. Duplicate descriptors, unknown kinds/elements and
nonzero scalar-record reserved fields are invalid. Count is at most 65,536. The table
is independent of instruction aux bytes and is shared by functions and pools.

Function parameter/result/local types retain one byte for frozen codes 0–4.
A parameterized type instead encodes **`u8 0x80`, `u16 descriptor_index`**.
References must resolve in section 6. Code 5 alone is not a slot encoding.
The function table's offsets still refer to the contiguous instruction region;
the legacy parts table is unchanged. Descriptors apply to complete types:
`list<i32>` and `list<f32>` never fit each other implicitly.

Optional section **7 (`collections`)** is `u32 count`, followed by entries:
`u16 descriptor_index`, `u16 length`, then exactly `length` element payloads.
Count is at most 65,536; length is 0–256; the descriptor must be a list of
frozen scalar elements or sampler<T>. Empty lists of descriptions are valid.
Element payloads are little-endian, with no per-element tag:

| Element | Payload |
| --- | --- |
| i32 | 4 exact pattern bytes |
| f32 | 4 exact IEEE binary32 bytes, including subnormals and NaN payloads |
| color | 4 RGBA8 bytes |
| vec2 | x then y, 4 binary32 bytes each |
| distribution | 1 family byte, then all four binary32 parameters (17 bytes) |
| sampler<T> | u16 index of a constant description in section 8, with exactly the list's element type |

Distribution families and parameter validation are the frozen 0.5 rules;
unset descriptions are invalid in the pool. All descriptors and entries are
validated even if unused. Trailing, truncated, oversized or invalid data is
rejected before execution. None of sections 6–8 exists in older minors.

## 7. List instructions

All aux bytes are zero. `t` is a descriptor index, `c` a collection index,
`B<T>` an internal builder unavailable to declared slots or source code.

| Opcode | Mnemonic and operand | Stack, deepest first |
| --- | --- | --- |
| 0xB5 | const.list c | `[] -> [list<T>]` |
| 0xB6 | list.begin t | `[i32 count] -> [B<T>]` |
| 0xB7 | list.append t | `[B<T>, T] -> [B<T>]` |
| 0xB8 | list.end t | `[B<T>] -> [list<T>]` |
| 0xB9 | list.get t | `[list<T>, i32 index] -> [T]` |
| 0xBA | list.len t | `[list<T>] -> [i32]` |
| 0xBB | poly.list | `[list<vec2>, color] -> []` |

`list.begin` checks count in 0–256, charges element fuel and memory, then
allocates a buffer. `list.append` evaluates no hidden expression; it writes
the already-evaluated element and rejects a full or mismatched builder.
`list.end` requires the declared count to be filled and freezes the buffer
without copying elements. `dup` cannot copy a builder. A builder cannot be
a local, parameter or result, or escape through a mismatched control stack.
The source compiler evaluates elements left to right, one at a time, keeping
operand depth bounded independently of list length and element component count.

Lists are immutable handles. Helper calls, returns, assignments and `dup`
share storage. `const.list` shares the pool entry and charges that entry only
on its first use in the generation. Indexing requires `0 <= index < length`
and traps with `IndexRange`; length/index/poly operations trap on an unset
handle. `poly.list` requires at least three points and uses the identical
scaling, alpha, clipping, rasterizer and bounding-box fuel of legacy `poly`.
Point storage is evaluated before the color. Existing variadic/named polygon
lowering is unchanged.

## 8. Arena limits, fuel and failure

Additional generation data has a **64 MiB** limit. A list allocation charges
128 bytes of metadata plus 64 bytes per reserved element, identically on all
targets. This conservative fixed accounting covers value/builder/ownership
storage and avoids native/WASM layout-dependent acceptance. Sharing handles
does not charge their buffers again. Frozen buffers remain owned by the
generation arena and are released when it finishes or is destroyed. Unfinished
builders and frozen resources are released after a failed format-0.6 generation.

Additional part canvases in 0.6 charge four bytes per source pixel before
allocation, share the same limit with lists, and release that charge after the
blit. The root canvas keeps its original limits. Files 0.1–0.5 retain their
existing part allocation policy. Math/RNG kernels and their fuel remain frozen.

Each list instruction costs base 1 fuel. `list.begin` additionally charges
one per reserved element before allocation; append/end each have their normal
base cost. Pool reads, handle copies, length and index have no element scan.
`poly.list` adds exactly the existing polygon visit cost, before rasterization.
Memory exhaustion traps with `OutOfMemory` and `generation data exceeds 64 MiB`.
Fuel exhaustion remains `FuelExhausted`. A trap never produces a successful
generation result and the failed 0.6 VM cannot resume as a successful run.

Legacy palettes may keep their existing selection-tree lowering and old pool
limits, including a last-element default for direct indexing. A palette used
as a first-class list has the new 256-element limit and checked list indices.
This preserves previously accepted large palette indexing and ramp.

## 9. Tool transport and acceptance

IPA signatures spell parameterized types as `list<T>`, `sampler<T>` or
`list<sampler<T>>`. `types:` lists
descriptors in table order. `collection: list<T> hex` stores the raw element
payload in hex; `-` spells zero elements. The typed width fixes the count.
IPDIS emits these declarations and exact raw data, including all floating
encodings. Instruction descriptor/collection operands are numeric indices;
they are checked without truncating values beyond u16. Assembly chooses the
oldest required format and validates its output before writing a file.

Stage 3 additionally verifies empty contexts, every element type, forward
const-list dependencies, helper sharing, RNG order, 256-element construction,
index/unset/memory/fuel traps, large legacy palettes, exact polygon images,
invalid descriptors/pools, old-version gates, debug/strip/compression,
IPA/IPDIS/formatter and existing C API/SFML/Godot/HTML5 output interfaces.
Frozen stage-1/2 format-0.6 binaries remain independent runtime evidence.

## 10. Captured samplers and explicit picks

All aux bytes are zero. Every operand `t` indexes a valid sampler descriptor
in section 6, including in unreachable instructions. No complex type is packed
into aux. Other descriptor kinds or missing indices are rejected before execution.

| Opcode | Mnemonic and operand | Stack, deepest first |
| --- | --- | --- |
| 0xBC | sampler.value t | `[T] -> [sampler<T>]` |
| 0xBD | sampler.range t | `[T lower, T upper] -> [sampler<T>]` |
| 0xBE | sampler.with t | `[T lower, T upper, distribution] -> [sampler<T>]` |
| 0xBF | sampler.pick t | `[sampler<T>] -> [T]` |
| 0xC0 | sampler.pick.list t | `[sampler<T>, i32 n] -> [list<T>]` |

Construction validates and snapshots its already-evaluated operands. Numeric
range lower must not exceed upper. f32 and vec2 components must be finite, also
for value descriptions; color values have no additional restriction. A shaped
description requires an initialized distribution satisfying the frozen constructor
rules. Probability mass/support are checked only when a draw executes. Each
description charges **256 bytes** before allocation, remains immutable and is
owned by the generation arena. Copy, assignment, parameter and return share
the allocation without RNG state or a second charge. An unset sampler handle
traps with `RuntimeViolation` and `sampler handle is unassigned` upon picking.

`sampler.value` returns its stored value on every pick without drawing.
`sampler.range` and `sampler.with` call the identical 0.5 integer/float kernels:
inclusive integer rejection sampling, pinned float interpolation and existing
distribution truncation/rounding. Color/vec2 ranges sample one f32 coefficient
in [0,1] and call the frozen RGBA/vec2 mix kernels. Equal numeric bounds retain
the old RNG draw. Integer draws may have redraws; descriptions never have an
independent stream. Single picks run in the current caller frame's stream.

Source capture has a distinct AST: it evaluates lower, then upper, then optional
distribution once, and captures only the outer range. Nested random expressions
in operands remain eager. Source `.pick` has a statically typed receiver and
arity; the receiver evaluates once before n. There is no implicit conversion
to T, lazy expression, generic dispatch, `sample` alias or random binding.
The global eager selection builtin `pick` remains unchanged.

For a batch, both expressions have run before the instruction. Check n in
**0–256** first (`NumericRange`, `sampler result count must be between 0 and 256`),
then the sampler handle/type. Charge n element fuel and reserve the entire
result list before the first draw. Each result comes from that same description
without reconstructing its bounds/parameters. Freeze/publish the list only
after all n results succeed. n=0 makes an empty typed list and performs no draw
or mass check, but still validates the handle. n=1 still returns a list.
Earlier receiver/count effects remain consumed if preflight fails.

Each capture and single pick pays its base 1 fuel. A nonconstant single pick
includes the existing scalar draw's base cost in that unit, plus integer redraw
or distribution kernel costs, and an additional 1 for color/vec2 mixing.
A batch pays base 1 plus n for storing results, plus each nonconstant draw's
base 1 and existing kernel/mix costs. Constant descriptions add no draw cost.
n=0 pays only the batch instruction, aside from source operand evaluation.
The batch and a manual loop have identical values/RNG, with different loop and
instruction overhead. Fuel failure cannot expose partial list elements.

`list<sampler<T>>` uses the existing list builder, index, length and sharing
operations with full element identity. List descriptors remain distinct for
each sampler element type. Empty literals require context and lists are limited
to 256 entries. Const samplers and range palettes use the same descriptions;
legacy palettes, math/RNG algorithms, parts and all public C layouts remain unchanged.
Failed format-0.6 executions clear all sampler/list arena ownership and cannot
publish or resume as a successful generation.

## 11. Constant samplers and sampling palettes

Optional section **8 (`samplers`)** encodes `u32 count` (0–65,536), followed by
entries in pool order:

| Field | Encoding |
| --- | --- |
| description type | u16 index into section 6, requiring sampler<i32/f32/color/vec2> |
| mode | u8: 0 constant, 1 uniform range, 2 range with distribution |
| reserved | u8 zero |
| lower or constant value | exact typed payload: i32/f32/color 4 bytes, vec2 x/y 8 bytes |
| upper | same typed payload, present only for modes 1/2 |
| distribution | u8 frozen family and four f32 parameters, present only for mode 2 |

Unknown modes, bad descriptor kinds/indices, nonzero reserved bytes, truncated
or trailing data, nonfinite f32/vec2 endpoints, reversed numeric ranges and
invalid distribution parameters reject before execution, including unused
entries. Positive probability mass is deliberately checked only by a pick.
Constant mode is distinct from an equal-bound range, preserving their different
RNG behavior. Endpoint/distribution bits are preserved without a sampled result.
List references must resolve to this pool and match the complete sampler type.
The combined fixed accounting of all constant list buffers and descriptions
must not exceed 64 MiB. Duplicate wire entries are allowed and are distinct
buffers; writers deduplicate descriptions by exact type, mode and payload bits.

Opcode **0xC1 `const.sampler c`** has aux zero and a u16 sampler pool index.
Its stack is `[] -> [sampler<T>]`; it requires format 0.6. Invalid aux/index and
all older minor gates are checked in unreachable code too. It costs base 1
fuel, retains the immutable description and makes no draw. Its first load
charges 256 bytes to the generation arena; later reads share the same handle
without another charge. No new public host type or ABI layout is introduced.

`const.list` of descriptions retains its existing opcode and header. Its first
load costs an additional 1 fuel per list entry, then preflights the list buffer
and distinct descriptions not already retained by that generation. The complete
charge occurs before publishing the list or retaining any new description.
Repeated entries, standalone const reads and other lists share each pool
description's charge. Cached list reads pay only the instruction base. Scalar
constant lists retain their frozen costs. All these resources share the existing
64 MiB generation quota, including dynamic captures, batches and part canvases.

Asset `const sampler<T>` and `const list<sampler<T>>` use the existing pure
constant resolver. It resolves one acyclic graph with palette declarations and
checks all declarations, including unused ones and forbidden effects in dead
branches. Bounds/parameters may refer forward to other constants; inputs, seed,
RNG, user helpers and `.pick` cannot execute during compilation. Capture changes
only an outer range into a description; nested random expressions remain effects
and are rejected in const initializers. Constant copying/indexing performs no
draw. An empty const list receives its type from the declaration.

A palette containing an outer range or sampler has type `list<sampler<T>>`.
Its ranges use the same capture representation; other scalar entries lift to
constant descriptions. T is i32/f32/color/vec2. Mixed numeric elements widen to
f32, including i32 description endpoints; other incompatible elements reject.
A plain scalar/vec2 palette retains value elements. All new palette expressions
are compile-time values with the same effect and cycle checks. Sampling palettes
require 1–256 entries. Existing literal palettes preserve their accepted syntax,
diagnostics, size limits and lowering for direct indexing and ramp.

Direct palette indexing evaluates its i32 index once and returns the selected
value or description without RNG. Negative or oversized indices select the last
entry. Only `.pick()` or `.pick(n)` samples the selected description, through the
existing caller-stream algorithms. A palette copied/passed as a first-class list
has at most 256 entries and ordinary checked indexing, including helper parameters.
`ramp` accepts only constant color/f32 palettes and rejects descriptions, lists of
samplers and vec2 palettes without sampling hidden interpolation stops. Source
metadata advertises these rules and enables sampler constant types/const `list.len`
overloads; global `pick`, ordinary helper names, old binaries and kernels remain
unchanged.

## 12. Point distributions and weighted draws

Stage 6 adds family **4 (weighted)** without changing scalar type code 4 or the
legacy family numbers, parameter records and kernels. The source literal is
`weighted { key: weight, ... }`, with a reserved family name. All operands are
i32/f32; each key and weight evaluates once in source order. Keys convert
exactly from their original type to binary64 for comparisons, preserving every
i32 including adjacent values above 2^24. They are finite and strictly
increasing; weights are finite/nonnegative and their ordered binary64 sum is
positive. There are 1–256 pairs. Descriptions are immutable snapshots without
RNG state. Pure asset const expressions validate at compile time; runtime
constructors validate even if their results are unused.

### Pool and wire references

Optional section **9 (Distributions)** requires minor 6. Its payload is:

```text
u32 count                         // <= 65536
repeat count:
    u8 family                    // 4
    u8 reserved                  // zero
    u16 point_count              // 1..256
    repeat point_count:
        u8 key_type              // 0 i32, 1 f32
        u8 value_type            // 0 i32, 1 f32
        u16 reserved             // zero
        u32 key_bits             // exact original scalar bits
        u32 value_bits
```

Every record is validated, including unused data. Unknown families/types,
reserved fields, nonfinite values, ordering, count/mass violations, truncation
and trailing bytes reject at load time. Pools may contain duplicate records;
writers deduplicate exact encoded bits, including operand types and signed
zero. Section-table order is irrelevant; parse section 9 before sections 8/7.

An embedded distribution in a sampler/list record keeps the frozen **17-byte**
encoding for families 1–3 (`u8 family`, four f32 parameters). Family 4 instead
uses **3 bytes**: `u8 family=4`, `u16 section_9_index`. Validate reference range
and exact family identity. Each list distribution element remains a complete
value record; scalar/list/sampler encodings without family 4 are unchanged.

IPA uses `distribution: HEX_RECORD` for a complete section-9 record, in pool
order. Existing `sampler:` and `collection:` hex records preserve references.
The assembler computes mixed distribution-list lengths by walking records;
IPDIS emits their exact bytes. No public C enums, layouts or API are added.

### Builder instructions, ownership and fuel

All instructions require minor 6, aux zero, and validation in dead code:

| Opcode | Mnemonic / operand | Stack, deepest first |
| --- | --- | --- |
| 0xC2 | const.distribution c | `[] -> [distribution]`; section-9 index c |
| 0xC3 | distribution.begin f | `[i32 count] -> [internal builder]`; family f=4 |
| 0xC4 | distribution.append t | `[builder, key, value] -> [builder]` |
| 0xC5 | distribution.end | `[builder] -> [distribution]`; operand zero |

Append operand t is 0–3: bit 0 selects f32 key (otherwise i32), bit 1 selects
f32 value. It describes only the two scalar operands, never a complex type.
Builder values cannot be duplicated, stored, passed or returned. Begin checks
count, reserves one fuel per pair and preflights the complete buffer before
operands run. Append validates each evaluated pair against the preceding key;
end checks exact length/positive mass and freezes without copying. This bounds
stack use independently of point count; no 512-slot list of all operands is
built. A trap destroys pending builders with the failed generation.

A point buffer charges **128 + 32 * count bytes** against the shared 64 MiB
arena. Helpers, copies, lists and samplers share it. First constant retention
pays one fuel per point and charges its buffer once; later reads pay only the
base instruction. Lists/samplers preflight all newly retained buffers before
publishing any cached handle; duplicate references share their charge.
Point/list/sampler constant pools share a combined 64 MiB accounting limit.
Existing scalar constant/list costs remain unchanged. Every new instruction
also pays the existing base cost of 1.

### Pinned weighted sampling

Validate numeric bounds (finite and lo <= hi) and the initialized distribution
before drawing. Advance the caller stream by 0x9e3779b9, hash with the frozen
splitmix32, then compute `u = (binary64(hash) + 0.5) * 2^-32`. Before processing
points, reserve **2 * point_count fuel** in addition to the base draw. Execute
two complete source-order scans, including zero and excluded points, even if
bounded mass is absent:

1. Start mass=+0. For each point with lo <= key <= hi, add its weight in
   binary64, in written order.
2. Compute target=u*mass, cumulative=+0. Add only positive weights of included
   points in written order. Select the first key where target < cumulative;
   continue the scan without changing that selection. Equality belongs to the
   next positive interval, never a zero-weight point.

Every arithmetic operation is binary64, correctly rounded separately without
FMA/reassociation or host probability functions. No redraws occur. If mass is
zero, trap with `random range has no probability under the distribution`
after the stream step. Equal bounds require a positive atom exactly there and
still draw. An impossible unselected positive-mass result traps as a numerical
error. F32 results round once to binary32 then restore the numeric bounds if
rounding leaves them; i32 results use the frozen half-away-from-zero conversion.
Color/vec2 sample this same coefficient on [0,1] and use the frozen mix kernels.
Batch sampler picks repeat this exact procedure on the caller stream. The RNG
grid implies quantized proportions, not exact seed-count percentages.

## 13. Density distributions and pinned integration

Stage 7 adds point family **5 (density)** through the same section-9 pool,
3-byte embedded references, arena and builder instructions as weighted.
Records require **2–256** points; all key/value tags, reserved fields, finite
numeric values and strictly increasing exact keys obey section 12. Values
are nonnegative heights. Constructor validation sums positive-length trapezoid
areas in source order in binary64 and requires a positive total. `distribution.begin`
accepts family operand 5; references must match family 5 exactly. No older
family record/kernel/opcode/type code or public host layout changes.

The source is `density { key: height, ... }`, with a reserved family name.
All snapshot, const/effect, sharing, quota, builder and retention rules of
section 12 apply. For consecutive original points `(a,p)` and `(b,q)`, natural
area is computed separately as `L=b-a`, `sum=p+q`, `average=sum*0.5`,
`area=L*average`. Accumulate these areas in written order, starting at +0.
The first point contributes no segment. An invalid static description rejects
at compile time; dynamic data trap during construction even if unused.

### Support, stream and clipping

Density is linear between consecutive keys and zero outside the closed natural
support `[first_key,last_key]`. Validate bounds and initialized description
before the one frozen `with` stream step, using the same open binary64
coefficient `u=(hash+0.5)*2^-32`. Support/mass checks occur after this step.

If lo==hi, return lo when it belongs to the closed natural support, even at
zero-height endpoints or in a zero-density gap. Otherwise trap with
`random range lies outside the distribution`. No segment scan/fuel is added.
For positive-length ranges, reserve **2 * (point_count-1)** fuel before work,
in addition to the base draw, including zero/excluded segments and failures.
Both scans visit every original segment in source order, without redraws.

For each original segment `(a,p0)` to `(b,q0)`:

```text
left  = max(lo,a)
right = min(hi,b)
if !(left < right): segment_area=+0
else:
    height(x):
        if x==a: return p0
        if x==b: return q0
        t=(x-a)/(b-a)
        l=p0*(1-t)
        r=q0*t
        return l+r
    p=height(left)
    q=height(right)
    L=right-left
    sum=p+q
    average=sum*0.5
    segment_area=L*average
```

Every subtraction, division, multiplication and sum above rounds separately to
binary64; FMA/reassociation and subnormal flushing are forbidden. Convex
endpoint interpolation keeps nonnegative heights without cancellation.
The first scan stores these at most 255 bounded segments in fixed temporary
storage and accumulates their areas in written order. These are transient
kernel data, not new generation-owned buffers.

### Stable inverse integral

Set target=u*total_area, cumulative=+0. The second complete scan skips
zero-area segments and adds each positive area in written order. Select the
first segment with target < cumulative+segment_area; equality belongs to the
next positive interval. Set z=target-cumulative before adding that selected
segment's area. Continue scanning without replacing the selection. No positive
bounded area traps with `random range has no probability under the distribution`.

For the selected segment, solve its local integral in this pinned order:

```text
if z==0: x=+0
else:
    difference=q-p
    s=difference/L
    if s==0: x=z/p
    else:
        A=p*p
        twice_slope=2*s
        B=twice_slope*z
        R=A+B
        if R<0:
            magnitudes=abs(A)+abs(B)
            tolerance=(8*2^-52)*magnitudes
            if -R>tolerance: numerical trap
            R=+0
        root=sqrt(R)
        numerator=2*z
        denominator=p+root
        x=numerator/denominator
```

Use the correctly rounded binary64 square root; no host probability functions.
A nonfinite solution or other numerical violation traps with
`distribution draw has a numerical error`. Correct x's endpoint rounding to
`[0,L]` only after solving the integral, then set value=left+x and restore
`[left,right]` if that addition leaves them. These corrections preserve the
chosen positive interval; they never clamp a rejected sample to global bounds.
Finally use the frozen conversion: one binary32 rounding (restoring the numeric
bounds when necessary) for f32/coefficient results, or nearest integer, halves
away from zero, for i32. Color/vec2 use the existing common-coefficient kernels.
All batch picks repeat this procedure independently on the caller stream.

## 14. Shared render targets and captured layers

Parts and source `layer name [(alpha=surface.<mode>)] { ... }`
captures write RGBA8 buffers through the same RenderTarget/Surface mechanism.
A buffer stores width, height, raster scale and row-major straight RGBA8.
An immutable view retains its buffer and adds D4 and integer pixel placement;
creating/copying the view neither copies nor scans pixels. Closed captures
expose no writable buffer through a language value. Mask coverage is specified
in section 16. Part resource exports use section 17, never geometry output slots.

A capture starts transparent, with its parent's geometry/scale, and executes
its block once using the current frame's RNG. Part calls retain their own
stream and existing argument order. On close, the named local becomes a
snapshot; no automatic parent paint occurs. Primitive draws and weather use
the current target. The new `surface.paint` builtin evaluates its image once
and paints using the saved mapping without replaying geometry or RNG.

The `layer` descriptor is the four-byte Types-section record
`u8 kind=12, u8 element=8 (Void), u16 reserved=0`. Function slots use the
existing `0x80, u16 descriptor_index` reference. Old scalar codes 0–4,
list/sampler descriptors, pool encodings and the Parts section are unchanged.
Layer is storable/passable/returnable, but cannot be an input, const, palette,
list/sampler element, numeric operand, comparison or pixel-readable value.
An untouched layer local is unset; paint traps before scanning any pixels.
Unused/malformed descriptors and newer opcodes in dead code are checked.

| Op | Mnemonic | Stack | Control / operands |
| -- | -------- | ----- | ------------------ |
| 0xC6 | layer.begin local [mode] | unchanged | pushes a layer label, closed by existing end; local must be a declared layer local, not a parameter |
| 0xC7 | surface.paint mode | `[layer] -> []` | aux=0; mode=0 inherited, 1 legacy or 2 source-over |

`layer.begin` uses its aux byte for mode=0 inherited, 1 legacy or 2 source-over; its u16
operand is the destination local index. Unknown mode, wrong/dangling local,
nonzero paint aux and unbalanced scope fail validation, including dead code.
Layer labels have no stack results; their entry stack must be preserved like a
block. Return evaluates/retains function results before closing that frame's
layers. A branch closes layers strictly inside its target label; a branch to a
layer label closes it when its end executes. Break/continue therefore close
only exited captures, including partially drawn ones, and restore every
previous target. Callee cleanup leaves caller scopes active.

Mode 0 resolves against the active destination target. Source spelling accepts
`surface.legacy` and `surface.source_over`; it cannot obtain a runtime mode from
an integer variable or const alias. Their ordinary scalar values are 0 and 1,
respectively; wire mode encodings remain 1 and 2.
The root remains legacy in format 0.6. Destination alpha, hidden RGB and the
old channel division order remain frozen. Legacy surface paint and part blits skip
zero-alpha source pixels. Paint visits the full source in y/x order, applies
D4's pixel-index mapping followed by integer origin, checks bounds in i64, then
uses the same pixel compositor as primitives. Part geometry still comes from
the frozen PartRenderPlan rounding/scale rules; no second transform or
resampling is applied. Outputs written during capture publish once and retain
existing owner/caller/root coordinate rules.

At most 32 layer/clip scopes combined may be active across all frames; static nesting above 32
also rejects. The shared 64 MiB generation arena charges a capture once as
`128 + 4 * width * height`. Handle copies and D4/placement views cost no second
buffer. Captured buffers live until their last handle is destroyed; success and traps clear remaining handles.
Root storage keeps its existing canvas limits and is excluded from this arena.
Format-0.6 part canvases retain the existing `4 * width * height` accounting,
released when their last handle is destroyed; older binaries retain their pre-arena limits.

Layer begin costs base 1 plus N target pixels cleared, reserved before
allocation. Paint costs base 1 plus N source pixels visited, reserved before
scanning, even when pixels are transparent/off-target. Primitive and existing
part call fuel remain unchanged, including the existing upfront part blit
reservation. Default fuel remains 2,000,000. Exceeding fuel, arena or active
scope limits traps without publishing a successful partial image. Trap cleanup
releases frames, captures, saved targets and all generation resources.

IPA names `layer` in `types`, locals, parameters and results. Layer begin's
optional third token records nonzero aux (`layer.begin 2 1`); disassembly and
reassembly preserve it. Compression and stripping keep inner minor 6. No
public C enum, structure layout, reflection field or host output API changes.

## 15. Source-over and target mode inheritance

The root always starts in legacy, including format 0.6. A named layer with no
alpha mode inherits its active target. Helpers share that target and mode;
part canvases and their return blits inherit the caller's active mode. An
explicit nested layer may override it. Scope cleanup on end/return/break/continue
restores the complete prior target, including its mode. Trap cleanup restores
the root and releases all scopes/resources without publishing a successful
partial result.

Paint with no blend uses the active destination mode, never the image's capture
mode. An explicit blend affects only that operation and does not change later
draws. Image handles contain pixels and mapping, without implicit blend state.
No mode change adds RNG draws, fuel surcharges or different allocation limits.

Source-over operates on straight RGBA8 stored channel values, without
linear-light conversion or changes to color mixing. For each source/destination
channel `Cs/Cd` and alpha `As/Ad`, use u64 intermediates and integer division:

```text
D = As * 255 + Ad * (255 - As)
Aout = (D + 127) / 255
N = Cs * As * 255 + Cd * Ad * (255 - As)
Cout = (N + floor(D / 2)) / D
```

If D is zero, write RGBA(0,0,0,0). Otherwise store the nearest RGB value with
ties up, and the rounded alpha above. A zero-alpha source over a visible
destination leaves it unchanged; over transparent hidden RGB it writes zero.
Source-over blits therefore process transparent source pixels too. Clipping
and D4 use the existing pixel mapping; no resampling is introduced.

Store the rounded RGBA8 result after every primitive, part blit and paint.
Never defer rounding, fuse paints, flatten layers or change operation order.
The legacy kernel remains byte-exact, including opaque replacement, floor RGB
division, destination alpha preservation and zero-alpha blit skips. Existing
executable formats and previously emitted mode 0/1 bytes retain that behavior.

The source keywords `const`, `to`, `with`, `distribution`, `sampler`, `weighted`,
`density` and `layer` are lexically reserved. Their old use as identifiers is
an intentional source migration: reject at the token with a parser diagnostic,
exit code 1 and no artifact. Already compiled files with those names remain
executable; no binary string/name table restrictions are added.

## 16. Coverage masks, clip scopes and disk morphology

`mask` is an immutable coverage handle sharing the surface geometry, raster
scale and mapping. `mask.from(layer)` views the layer's stored alpha without
allocation, scanning, geometry replay or RNG. Computed masks store row-major
A8 in the same buffer abstraction, without RGBA pixel storage. Handle copies,
helper arguments/results and offset/D4 views share storage. Source `mask` and
`clip` are reserved, with the same explicit source-migration diagnostic and
binary compatibility policy as section 15. There is no `mask name { ... }` block.

The mask descriptor is `u8 kind=13, u8 element=8 (Void), u16 reserved=0`, referenced
by `0x80, u16 descriptor_index`. Masks are storable, passable and returnable,
but cannot be host inputs, geometry outputs, const, list/sampler elements,
numeric/comparison operands or pixel-readable values. An unassigned mask traps
with `mask is unassigned`. Existing descriptors and host enums/layouts stay frozen.

### 16.1. Pixel space and coverage operations

All mask values operate in the current asset's rasterized pixel space. Their
integer placement and exact D4 map pixel indices once. The mapped raster scale
must match the active target's scale; existing part canvases already rasterize
at the appropriate scale, so no resampling is needed. Nonfinite/nonpositive
scales, mismatched raster scales, invalid storage and bounds outside i32 pixel
coordinates trap. Width/height must be positive and representable as i32;
the last occupied pixel, rather than the exclusive right/bottom edge, must
fit i32. Reads outside an input rectangle give zero.

| Operation | Coverage | Result rectangle |
| --- | --- | --- |
| mask.union(a,b) | max(a,b) | bounding rectangle of both inputs |
| mask.intersect(a,b) | min(a,b) | bounding rectangle of both inputs |
| mask.subtract(a,b) | min(a,255-b) | bounding rectangle of both inputs |
| mask.invert(a) | 255-a | input rectangle; zero outside it |
| mask.expand(a,r) | maximum over the integer disk | input plus r pixels on every edge |
| mask.shrink(a,r) | minimum over the integer disk, including outside zero | input rectangle |
| mask.offset(a,dx,dy) | unchanged | translated input rectangle |

Binary algebra samples both operands in the active raster grid, without
clipping its allocated result to the canvas. Invert complements only the
input rectangle, including its zero-coverage pixels. Morphology radius is
an i32 in 0–64; include all integer `(dx,dy)` with `dx*dx+dy*dy <= r*r`.
Radius zero preserves coverage, radius one has five samples, and radius 64
has 12,853. Offsets are i32 target pixels and change only the mapping. Every
changing operation leaves the inputs immutable. Constant bad radii, including
asset-constant expressions, reject with `IPS-TYPE-001`, exit 1 and no artifact;
dynamic bad radii trap with `mask radius must be in 0..64` before scanning.

### 16.2. Clip inheritance and pixel writes

`clip(mask) { ... }` evaluates its operand once, installs another coverage
level and consumes no implicit RNG. Helpers inherit clips. A new layer has
the parent's geometry and inherits clips; scope cleanup restores its saved
target, mode and clips. A part starts its own canvas with no parent clips.
Only its return blit receives the restored parent's clip, exactly once.
Clips created inside the part independently constrain its stored image.
Geometric outputs are never clipped. Resource exports follow section 17 and do not inherit parent clip coverage.

At each in-target attempted write, start clip coverage at 255 and multiply
outer to inner using `mul(a,b)=(u32(a)*b+127)/255`. After combining all levels,
scale the primitive/layer source alpha with the same `mul`. Use the existing
effective legacy/source-over kernel and store RGBA8 after that write. Coverage
zero skips the write entirely, including hidden RGB canonicalization.

`surface.paint(mask,color[,blend])` requires color. It evaluates image then
color then the static mode, independent of named-argument spelling. First
scale the color's alpha by mask coverage, then by combined clip coverage.
Zero mask coverage skips that source pixel before any attempted target write;
nonzero coverage with rounded source alpha zero still uses the compositor.
Mode omission and one-operation overrides follow section 15. Mask painting
visits its full source rectangle, while only in-target pixels can write.

Weather retains its algorithm and seeded hash computations. For each proposed
write, interpolate each old/proposed RGBA channel by combined coverage:
`(old*(255-coverage)+proposed*coverage+127)/255`. Coverage zero retains the
old pixel; skipped transparent/protected/hash-ineligible pixels are not write
attempts. Existing weather alpha preservation and stream behavior remain exact.

Layer/clip scopes share one ordered stack. End, return, break and continue
unwind only exited scopes in reverse entry order, retaining the partial image
for every closed capture. A callee's return leaves caller scopes installed.
Traps clear the entire generation and cannot publish a valid partial image.
At most 32 combined scopes can be active across the helper/part call stack.

### 16.3. Wire instructions and accounting

All instructions below require inner format 0.6, including unreachable code.
Their aux/operand are zero except the mask paint mode operand. They have no
implicit RNG, outputs or changes to old instruction numbers.

| Op | Mnemonic | Stack / control |
| --- | --- | --- |
| 0xC8 | mask.from | `[layer] -> [mask]` |
| 0xC9 | mask.union | `[mask,mask] -> [mask]` |
| 0xCA | mask.intersect | `[mask,mask] -> [mask]` |
| 0xCB | mask.subtract | `[mask,mask] -> [mask]` |
| 0xCC | mask.invert | `[mask] -> [mask]` |
| 0xCD | mask.expand | `[mask,i32] -> [mask]` |
| 0xCE | mask.shrink | `[mask,i32] -> [mask]` |
| 0xCF | mask.offset | `[mask,i32,i32] -> [mask]` |
| 0xD0 | surface.paint.mask mode | `[mask,color] -> []`; mode 0/1/2 as section 15 |
| 0xD1 | clip.begin | pops mask, pushes a result-free clip label closed by existing end |

Validation checks stack types, combined lexical scope depth, unused operands,
descriptor shape, version and balanced labels even in dead code. Runtime adds
unassigned-handle, mapping, dynamic radius and combined call-depth checks.
IPA/IPDIS share these mnemonics and the `mask` type spelling. Compression,
debug/strip, type compaction and oldest-format selection retain their rules.

An A8 result charges `128 + width*height` bytes to the existing 64 MiB generation
arena. Its fixed metadata/buffer accounting is identical on native and WASM.
Sharing a buffer does not charge it again. Instructions cost one fuel, plus:

- A8 zeroing: one per allocated pixel.
- Algebra/invert: one per result pixel.
- Morphology: one per logical disk sample at each result pixel, including
  outside-zero samples and samples skipped by early termination.
- Mask paint: one per visited source-rectangle pixel.
- An attempted in-target write: one per active clip level, even if coverage
  becomes zero; no surcharge when no clips are active.

View construction and offset have no per-pixel cost. Reserve zeroing and all
logical scan work before allocation/scanning, with checked u64 counts. Existing
part/capture costs and the 2,000,000 default fuel stay unchanged. Resources
release when the last handle is destroyed and at generation completion/trap;
a failed run does not return an image.

## 17. Internal resource exports

Source `output mask name` and `output layer name` declare singular internal
slots. They do not extend frozen geometry enums, meta/code geometry outputs,
the Parts section, C API 0.3 or ABI line 0.4. Host `output_count`, part output
ranges and every existing output index still refer only to geometry.

Optional section **10 (ResourceExports)** requires format 0.6, even if all
exports are unused. Its exact little-endian layout is:

```text
u32 export_count                    // 1..65536
repeat export_count:
  u32 owner                         // part index, or 0xffffffff for root
  u32 name_string_index
  u16 type_descriptor_index         // mask (13) or layer (12)
  u16 reserved                      // zero
u32 function_count                  // exactly the Code function count
repeat function_count:
  u32 function_owner                // same owner encoding
```

Owners and string/type indices must be in range. Resource and geometry names
must be unique within each owner. Type descriptors are retained for unused
exports. No trailing bytes are permitted. With exports, every part has an
existing Parts record; the root entry belongs to root and each part entry to
that part. Helper calls preserve their declared asset owner. Static validation
checks every function and instruction, including unused helpers and dead code:
only an owner may write its slot, and only a direct parent may read it or draw
the owning part. The table is omitted entirely for programs without exports.

| Op | Mnemonic | Stack |
| --- | --- | --- |
| 0xD2 | out.mask slot | `[mask] -> []` |
| 0xD3 | out.layer slot | `[layer] -> []` |
| 0xD4 | part.get.mask slot | `[] -> [mask]` |
| 0xD5 | part.get.layer slot | `[] -> [layer]` |

Aux is zero. The u16 operand indexes only this resource table; bounds, type,
owner and format are validated even in dead code. Reads require the frame's
last-called direct child, and an unwritten slot traps with PartOutputMissing.
Writing an unset handle traps. Neither operation paints, advances RNG, copies
pixels or scans the buffer; each costs one base instruction fuel.

A write saves an immutable shared view in the owner's caller coordinates.
The view's D4 is composed with that call's D4; its origin is the minimum of
the transformed rectangle's pixel corners plus the call's target origin.
Use checked integer arithmetic for offset/expanded masks, including pixels
outside the part canvas; unrepresentable i32 bounds trap with NumericRange.
Raster scale follows D4 axis swapping without resampling. Propagation through
the parent's own export applies the next hierarchy level exactly once.
A later call without a write keeps the old buffer **and old mapping**. Copies
hold their snapshot after slot replacement. Exporting never paints implicitly.

Slots charge 64 bytes each once per generation. Buffers charge their existing
RGBA8/A8 allocation once, shared by layers, masks, helpers and export slots.
Destroying the last handle frees storage immediately and releases its quota.
Part canvases still charge 4 bytes per pixel in format 0.6, the root is excluded,
and older format limits are unchanged. Success and traps clear remaining
generation handles, with no cross-generation resources or seed-dependent cache.
The 64 MiB data cap, combined 32 scope limit and default fuel stay unchanged.

IPA represents records in table order as `export: mask|layer name root|index`
and function ownership as `owner: function_name root|index`. IPDIS preserves
these declarations, including part/helper owners; numeric opcode operands
retain resource-table indices through assembly, debug stripping and compression.
