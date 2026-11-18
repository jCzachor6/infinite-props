# Models: the `.iprop3d` format and the extended VM (1.2 preview)

**Status:** Preview contract of milestones M1–M3, not frozen. Sub-operation
numbers, section layouts and source spellings may change until the model
contract is frozen; a file written by a preview toolchain need not load in a
later one. Nothing here changes a `.iprop` file, the
[1.1 contract set](iprop-v1.1.md) or the 2D C API.

The intent, the examples and the later milestones are in the vision document
`docs/plans/2026-11-16-iprop3d.md` of the source repository. This document
states the rules an implementation must follow bit for bit.

## Contents

- [Conventions](#conventions)
- [Container](#container)
- [Types](#types)
- [The model page](#the-model-page)
- [Placement scopes](#placement-scopes)
- [Blocks and buffers](#blocks-and-buffers)
- [Skins](#skins)
- [Vertices and mapping](#vertices-and-mapping)
- [Primitives](#primitives)
- [Shapes along a path](#shapes-along-a-path)
- [Shaping blocks](#shaping-blocks)
- [Outputs](#outputs)
- [Parts](#parts)
- [Ownership](#ownership)
- [Accounting and limits](#accounting-and-limits)
- [Result](#result)
- [Source language](#source-language)

## Conventions

A **model** is a program whose entry function is `render`. One generation
takes the program, a `u32` seed, declared inputs and execution limits and
produces triangles and textures.

- **Axes.** Right-handed: `+X` right, `+Y` up, `+Z` toward the viewer. The
  front of a model faces `+Z` and it stands on the plane `Y = 0`.
- **Winding.** A triangle is counterclockwise seen from outside.
- **Units.** Model units; `unitsPerMeter` in the [model section](#the-model-section)
  relates them to metres. Angles of [placement](#placement-scopes) are degrees.
- **Arithmetic.** Unless a rule says otherwise, geometry is computed in
  binary64 in the written order, one IEEE operation per written operator,
  without fused multiply-add. A value entering from the program is binary32
  and widens exactly. Every stored component (position, normal, tangent, UV)
  is rounded to binary32 once, when its vertex is emitted, written `f32(x)`.
- **Constants.** `PIO2 = 1.57079632679489661923`, `PI = 3.141592653589793`,
  `TAU = 6.283185307179586`, `RAD = 0.017453292519943295` (the doubles nearest
  π/2, π, 2π and π/180), `SQRT2 = 1.4142135623730951`.
- **Trigonometry.** `sin64(x)` is the
  [pinned sine](iprop-v0.8.md#127-trigonometry) evaluated on a binary64
  argument, returning its binary64 value before the final cast;
  `cos64(x) = sin64(x + PIO2)`.
- **Turns.** For a finite binary32 angle `a` in degrees, `turn(a) = (c, s)`:

  ```
  q = a / 90.0
  if q == floor(q):                       // an exact multiple of 90 degrees
      m = q - 4.0 * floor(q / 4.0)        // 0, 1, 2 or 3
      (c, s) = [(1, 0), (0, 1), (-1, 0), (0, -1)][m]
  else:
      r = a * RAD
      (c, s) = (sin64(r + PIO2), sin64(r))
  ```

- **Normalisation.** `unit(v) = v / sqrt((v.x*v.x + v.y*v.y) + v.z*v.z)`, each
  component divided separately. A zero or non-finite length traps with
  `RuntimeViolation` (`degenerate direction`).

## Container

A model file uses the [container](iprop-v0.8.md#2-file-structure) of a
`.iprop` file, with the magic `49 50 33 44` ("IP3D"). The header fields, the
section table, the zlib envelope and the debug section keep their rules. A 2D
runtime rejects the file at the magic; the model runtime rejects a `.iprop`
file the same way.

| ID | Section | In a model file |
| --- | --- | --- |
| 1 | meta | required, the 2D layout with fixed canvas fields |
| 2 | const | required, unchanged |
| 3 | code | required; function 0 is named `render` |
| 4 | debug | optional, unchanged |
| 5 | parts | invalid |
| 6–9, 11 | types, collections, samplers, distributions, steppers | optional, unchanged |
| 10 | resource exports | invalid |
| 12 | model | required |
| 13 | skins | required |
| 14 | outputs | optional ([outputs section](#the-outputs-section)) |
| 15 | parts | optional ([parts section](#the-parts-section)) |

In the meta section `sizeX` and `sizeY` must be 1, both origins zero, the
flags zero and `outputCount` zero. `fuel` is the budget of the model program.
Inputs keep their 2D layout and types. In the code section `partCount` must
be zero. Any other value fails loading with `InvalidMetadata`
(`InvalidCode` for the part count).

### The model section

Section 12 is exactly 32 bytes:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | `sizeW` (f32) |
| 4 | 4 | `sizeH` (f32) |
| 8 | 4 | `sizeD` (f32) |
| 12 | 4 | `unitsPerMeter` (f32) |
| 16 | 4 | flags, zero |
| 20 | 12 | reserved, zero |

The four numbers must be finite and positive. The nominal box is
`x ∈ [-sizeW/2, sizeW/2]`, `y ∈ [0, sizeH]`, `z ∈ [-sizeD/2, sizeD/2]`; it
is a declaration and never clips geometry.

### The skins section

Section 13 holds the 2D programs a model can generate as textures:

| Size | Field |
| --- | --- |
| 4 | `programCount`, 0–64 |
| 12 × count | entries: `u32 nameIndex`, `u32 offset`, `u32 byteLength` |
| — | program bytes |

`nameIndex` names the program in the string pool (its import alias). `offset`
is relative to the section start, a multiple of 4 and not before the end of
the entry table; a range must lie inside the section and two ranges are
either identical or disjoint. Bytes that no entry covers must be zero.

Each range is a complete expanded `.iprop` file (not a zlib envelope) that
loads and validates under the 2D rules. Its `byteLength` is at least 20.
Failures report `InvalidMetadata` with the entry's position.

Two entries with the same range are **one program**: an asset imported under
several names (by a model and its parts) is stored once and its textures are
shared as one program's ([generating a texture](#generating-a-texture)).

### The outputs section

Section 14, present only in a file with 3D outputs, declares the output
slots, numbered from 0 in order:

| Size | Field |
| --- | --- |
| 4 | `count`, 1–256 |
| 8 × count | entries: `u32 nameIndex`, `u8 kind`, `u8 list`, `u16 owner` |

`kind` is 0 (`point3d`) or 1 (`box3d`), `list` 0 or 1, and `owner` `0xFFFF`
for the model itself or the index of a [part](#the-parts-section). A name is
a non-empty string of the pool, unique among the slots of one owner.
Trailing bytes are invalid. Failures report `InvalidMetadata`.

### The parts section

Section 15, present only in a file with 3D parts, lists the part instances:

| Size | Field |
| --- | --- |
| 4 | `count`, 1–256 |
| 16 × count | entries: `u32 nameIndex`, `u32 firstFunction`, `u32 functionCount`, `u32 parent` |

A name is non-empty and unique; the reference compiler uses the import path
from the model (`leg`, `pair.peg`). `parent` is `0xFFFFFFFF` for the model or
the index of an earlier part. The functions of the parts follow the
model's own as contiguous runs in entry order: the first part's run starts
after function 0, each run starts where the previous ends, `functionCount`
is at least 1 and the last run ends at the end of the function table. The
first function of a run is the part's **entry**: its parameters are the
part's inputs and it has no results. Failures report `InvalidMetadata`.

## Types

A model program has one more value type:

| Slot type | Encoding in function signatures | Runtime value |
| --- | --- | --- |
| vec3 | 21 | Three binary32 components in one stack slot |

It is storable in locals, parameters and results. Its typed zero is
`(0, 0, 0)`. It is no element of a sampler or a stepper.

A model file may also declare the descriptor `list<vec3>` (kind 5, element
21) in its types section; a `.iprop` file that does fails loading with
`InvalidCode` (`invalid parameterized type descriptor`). Such a list is
built and read by the list instructions of the 2D set; in the collections
section each element is three binary32 values, `x`, `y`, `z`.

A **skin** is not a VM type. The program carries it as an `i32`: the number of
a skin record, from 1. Zero is the unassigned skin. Every instruction that
takes a skin traps with `RuntimeViolation` (`skin is unassigned`) for zero
and with `IndexRange` (`skin index out of range`) for any other number that
names no record of the running generation.

## The model page

Opcode `0x0F` is the **model page**. Its aux byte selects a sub-operation and
its operand is that sub-operation's immediate. The page is valid only in a
model file; in a `.iprop` file `0x0F` stays a reserved opcode.

A model program may use the model page and these 2D instructions: control,
stack and constants, integer, float, colour, vec2, signed-distance fields,
curves, seeded primitives and streams, the nonperiodic scalar noise operations,
`seed.initial`, lists, samplers, steppers and distributions. Canvas
instructions, layers, masks, clips, blur, `surface.normal`, structured
outputs, parts, dynamic parts, the texture page, Voronoi, contours and regions are invalid, in
unreachable code too.

| Aux | Mnemonic | Operand | Stack |
| --- | --- | --- | --- |
| 0x00 | `vec3.make` | 0 | `[f32 f32 f32] -> [vec3]` |
| 0x01 | `vec3.add` | 0 | `[vec3 vec3] -> [vec3]` |
| 0x02 | `vec3.sub` | 0 | `[vec3 vec3] -> [vec3]` |
| 0x03 | `vec3.mul` | 0 | `[vec3 vec3] -> [vec3]` |
| 0x04 | `vec3.div` | 0 | `[vec3 vec3] -> [vec3]` |
| 0x05 | `vec3.scale` | 0 | `[vec3 f32] -> [vec3]` |
| 0x06 | `vec3.neg` | 0 | `[vec3] -> [vec3]` |
| 0x07 | `vec3.x` | 0 | `[vec3] -> [f32]` |
| 0x08 | `vec3.y` | 0 | `[vec3] -> [f32]` |
| 0x09 | `vec3.z` | 0 | `[vec3] -> [f32]` |
| 0x0A | `vec3.len` | 0 | `[vec3] -> [f32]` |
| 0x0B | `vec3.dot` | 0 | `[vec3 vec3] -> [f32]` |
| 0x0C | `vec3.cross` | 0 | `[vec3 vec3] -> [vec3]` |
| 0x0D | `vec3.normalize` | 0 | `[vec3] -> [vec3]` |
| 0x0E | `vec3.mix` | 0 | `[vec3 vec3 f32] -> [vec3]` |
| 0x20 | `place.begin` | mask | arguments by mask `-> []`, opens a scope |
| 0x21 | `smooth.begin` | 0 | `[f32] -> []`, opens a block |
| 0x22 | `refine.begin` | 0 | `[i32] -> []`, opens a block |
| 0x23 | `bend.begin` | 0 | `[f32] -> []`, opens a block |
| 0x24 | `twist.begin` | 0 | `[f32] -> []`, opens a block |
| 0x25 | `taper.begin` | 0 | `[f32] -> []`, opens a block |
| 0x26 | `displace.begin` | 0 | `[f32 f32 i32] -> []`, opens a block |
| 0x40 | `box` | 0 | `[vec3 vec3 skin] -> []` |
| 0x41 | `box.bevel` | 0 | `[vec3 vec3 f32 skin] -> []` |
| 0x42 | `cylinder` | 0 | `[vec3 f32 f32 i32 skin] -> []` |
| 0x43 | `cone` | 0 | `[vec3 f32 f32 f32 i32 skin] -> []` |
| 0x44 | `band` | 0 | `[vec3 f32 f32 f32 i32 skin] -> []` |
| 0x45 | `sphere` | 0 | `[vec3 f32 i32 i32 skin] -> []` |
| 0x46 | `lathe` | 0 | `[vec3 list<vec2> i32 skin] -> []` |
| 0x47 | `lathe.crease` | 0 | `[vec3 list<vec2> i32 f32 skin] -> []` |
| 0x48 | `extrude` | 0 | `[vec3 list<vec2> f32 skin] -> []` |
| 0x49 | `extrude.bevel` | 0 | `[vec3 list<vec2> f32 f32 skin] -> []` |
| 0x4A | `quad` | 0 | `[vec3 vec3 vec3 vec3 skin] -> []` |
| 0x4B | `tri` | 0 | `[vec3 vec3 vec3 skin] -> []` |
| 0x50 | `tube` | 0 | `[vec3 vec3 vec3 vec3 f32 f32 i32 i32 skin] -> []` |
| 0x51 | `sweep` | 0 | `[list<vec3> list<vec2> skin] -> []` |
| 0x70 | `skin.draw` | program | `[inputs… i32 i32 i32] -> [skin]` |
| 0x71 | `skin.density` | 0 | `[skin f32] -> [skin]` |
| 0x72 | `skin.fit` | 0 | `[skin] -> [skin]` |
| 0x73 | `skin.shift` | 0 | `[skin vec2] -> [skin]` |
| 0x74 | `skin.roughness` | 0 | `[skin f32] -> [skin]` |
| 0x75 | `skin.metallic` | 0 | `[skin f32] -> [skin]` |
| 0x76 | `skin.emission` | 0 | `[skin f32] -> [skin]` |
| 0x77 | `skin.cutout` | 0 | `[skin f32] -> [skin]` |
| 0x78 | `skin.two_sided` | 0 | `[skin] -> [skin]` |
| 0x79 | `skin.crisp` | 0 | `[skin] -> [skin]` |
| 0x7A | `skin.blend` | 0 | `[skin] -> [skin]` |
| 0x7B | `skin.region` | 0 | `[skin f32 f32 f32 f32] -> [skin]` |
| 0x90 | `model.lod` | 0 | `[] -> [i32]` |
| 0x91 | `out.point3d` | slot | `[vec3] -> []` |
| 0x92 | `out.box3d` | slot | `[vec3 vec3] -> []` |
| 0x93 | `out.list.point3d` | slot | `[vec3] -> []` |
| 0x94 | `out.list.box3d` | slot | `[vec3 vec3] -> []` |
| 0x95 | `part.get.point3d` | slot | `[] -> [vec3]` |
| 0xA0 | `part.render` | part | `[inputs… i32] -> []`, runs the part |

`skin` in a stack signature is an `i32`. An operand written 0 must be zero.
An aux value outside this table fails validation, in unreachable code too.
A slot operand names an entry of the [outputs section](#the-outputs-section)
whose kind and list flag match the operation (`out.point3d` a singular
`point3d`, `part.get.point3d` a singular `point3d`), else validation fails.
`part.render P` names an entry of the [parts section](#the-parts-section)
and takes the parameters of its entry, then the seed. Ranges reserved for
later milestones: `0x27–0x3F` scopes, `0x4C–0x4F` and `0x52–0x6F`
primitives, `0x7C–0x8F` skins, `0x90` and `0x96–0x9F` queries and outputs,
`0xA1–0xAF` parts, `0xB0–0xBF` sculpting.

### The `vec3` operations

The operations work on binary32 components with one IEEE operation per
written operator and canonicalise every NaN result to `0x7FC00000`, as the
[vec2 operations](iprop-v0.8.md#94-float) do.

- `add`, `sub`, `mul`, `div`: per component. `scale`: each component times
  the scalar. `neg`: each component negated.
- `len`: `sqrt((x*x + y*y) + z*z)`.
- `dot`: `(ax*bx + ay*by) + az*bz`.
- `cross`: `(ay*bz - az*by, az*bx - ax*bz, ax*by - ay*bx)`.
- `normalize`: in binary64, `m = max(|x|, |y|, |z|)`; a zero `m` gives
  `(0, 0, 0)`; otherwise the components are divided by `m`, then by
  `sqrt((x'*x' + y'*y') + z'*z')`, and cast. A non-finite component traps
  with `NumericRange` (`vec3.normalize needs finite components`).
- `mix(a, b, t)`: `t` clamps to `[0, 1]`; each component is
  `a*(1 - t) + b*t`, kept between `a` and `b`, as `vec2` mixing does.

They cost one fuel unit each and read no stream.

## Placement scopes

`place.begin` opens a scope that the matching `end` closes; it is a label like
`layer.begin`. Branches and returns that leave it close it. At most 32 scopes
(placements, and in a 2D program layers and clips) are open at once; one more
traps with `RuntimeViolation` (`scope depth exceeds 32`).

The operand is a mask of the arguments present, which lie on the stack in bit
order (bit 0 deepest):

| Bit | Argument | Type |
| --- | --- | --- |
| 0 | `at` | vec3 |
| 1 | `yaw` | f32, degrees about `+Y` |
| 2 | `pitch` | f32, degrees about `+X` |
| 3 | `roll` | f32, degrees about `+Z` |
| 4 | `scale` | f32 |
| 5 | `scale` | vec3 |
| 6 | `mirror_x` | i32 |
| 7 | `mirror_y` | i32 |
| 8 | `mirror_z` | i32 |

A zero mask, bits 4 and 5 together and bits above 8 fail validation. Checks
when the instruction runs, in this order: every float finite, else
`NumericRange` (`place needs finite arguments`); every scale component
positive, else `NumericRange` (`place scale must be positive`); every mirror
0 or 1, else `NumericRange` (`place mirror must be 0 or 1`).

A scope holds an affine map `p ↦ A·p + o` with a 3×3 matrix `A`, an offset
`o` and a flag `flipped`. The root scope is the identity, not flipped. With
absent arguments at their neutral values, the local map is

```
(cy, sy) = turn(yaw)     (cp, sp) = turn(pitch)     (cr, sr) = turn(roll)

Ry = | cy  0  sy |    Rx = | 1   0    0  |    Rz = | cr  -sr  0 |
     |  0  1   0 |         | 0  cp  -sp  |         | sr   cr  0 |
     | -sy 0  cy |         | 0  sp   cp  |         |  0    0  1 |

R = (Ry · Rx) · Rz
k = (sx * mx, sy * my, sz * mz)        // m = -1 for a set mirror, else 1
L[i][j] = R[i][j] * k[j]
```

Matrix products use `((a0*b0 + a1*b1) + a2*b2)` per element. The child scope is

```
A = A_parent · L
o[i] = ((A_parent[i][0]*at.x + A_parent[i][1]*at.y) + A_parent[i][2]*at.z) + o_parent[i]
flipped = flipped_parent xor (an odd number of mirrors is set)
```

A positive yaw turns `+Z` toward `+X`; a positive pitch turns `+Y` toward
`+Z`. The instruction costs 25 fuel units, its own and 24 more charged
after the checks, and reads no stream.

`C` is the cofactor matrix of `A`, each element a difference of two products
in this order:

```
C[0][0] = A[1][1]*A[2][2] - A[1][2]*A[2][1]
C[0][1] = A[1][2]*A[2][0] - A[1][0]*A[2][2]
C[0][2] = A[1][0]*A[2][1] - A[1][1]*A[2][0]
C[1][0] = A[0][2]*A[2][1] - A[0][1]*A[2][2]
C[1][1] = A[0][0]*A[2][2] - A[0][2]*A[2][0]
C[1][2] = A[0][1]*A[2][0] - A[0][0]*A[2][1]
C[2][0] = A[0][1]*A[1][2] - A[0][2]*A[1][1]
C[2][1] = A[0][2]*A[1][0] - A[0][0]*A[1][2]
C[2][2] = A[0][0]*A[1][1] - A[0][1]*A[1][0]
```

With `σ = -1` when flipped, else `1`, and `i = 0, 1, 2`:

```
point(p)[i]   = ((A[i][0]*p.x + A[i][1]*p.y) + A[i][2]*p.z) + o[i]
normal(n)     = unit(v)   where v[i] = σ * ((C[i][0]*n.x + C[i][1]*n.y) + C[i][2]*n.z)
tangent(t)    = unit(v)   where v[i] = (A[i][0]*t.x + A[i][1]*t.y) + A[i][2]*t.z
```

A flipped scope emits every triangle `(a, b, c)` as `(a, c, b)` and negates
the tangent sign.

## Blocks and buffers

A **block** is a scope with a buffer of its own: the
[shaping blocks](#shaping-blocks) and the [call of a part](#parts). Its
frame is where it opens: the block's scope is the identity map, not flipped,
and scopes opened inside it compose from there. Every scope therefore maps
into the frame of the innermost open block, or of the model when none is
open, and a primitive writes into that buffer.

A vertex written into a block's buffer keeps `point(p)`, `normal(n)`,
`tangent(t)` and its sign in binary64, unrounded; its UV is computed and
rounded as for any vertex. A vertex written into the model is rounded as
[vertices and mapping](#vertices-and-mapping) state.

When a shaping block closes, at its `end` or when a branch or a return
leaves it, in this order:

1. Every position of its buffer must be finite, else the generation traps
   with `NumericRange` (`model coordinate out of range`).
2. The block [processes](#shaping-blocks) its buffer, paying first.
3. It pays 3 fuel units and one work step for each vertex and 1 fuel unit
   and one work step for each triangle of the buffer.
4. Every vertex passes to the enclosing scope in buffer order, as if a
   primitive emitted it there with its `p`, `n`, `t` and sign: through that
   scope's map, normalised again, and rounded if that scope writes into the
   model; it keeps its UV and its skin record. Triangles follow in order,
   renumbered, and a flipped scope swaps their last two corners.
5. Its [pending outputs](#outputs) pass on in order.

Vertex and triangle limits count every vertex and triangle made, in every
buffer: each of them ends in the model.

## Skins

### Generating a texture

`skin.draw P` generates embedded program `P`. From the top of the stack it
pops the height, the width, the seed and then the program's inputs in reverse
declaration order, each of its declared binary type (a `u32` input is an
`i32`). A zero width or height selects the program's declared one. A negative
or larger than 2048 dimension traps with `NumericRange`
(`skin size must be in 0..2048`).

The texture is the result of a standard 2D generation of the embedded file
with that seed, those dimensions and those inputs: the image and every
auxiliary image it publishes. It is therefore the bytes a host would get
from the same `.iprop`. The generation runs under the model program's remaining
fuel budget and under the limits of the running model generation
([accounting](#accounting-and-limits)). A trap inside it fails the model
generation with that trap; the message is prefixed by `skin 'NAME': `.

Two `skin.draw` instructions of one generation with the same program, seed,
resolved dimensions and input bits share one texture: the second generates
nothing. The **key** of a texture is the 64-bit FNV-1a hash (offset
`0xcbf29ce484222325`, prime `0x100000001b3`) of these bytes: the FNV-1a 64
hash of the embedded file as eight little-endian bytes, then the seed, the
resolved width and height and each input's four bytes in declaration order,
all little-endian.

The instruction pushes a new record for the texture with these fields:

| Field | Default |
| --- | --- |
| mapping | repeat |
| density | 1 |
| shift | (0, 0) |
| roughness | 0.8 |
| metallic | 0 |
| emission | 0 |
| alpha | opaque |
| cutoff | 0.5 |
| two-sided | no |
| crisp | no |

### Material maps on skins

A texture carries every auxiliary image its program publishes: a normal map
and the [material maps](material-maps-v1.2.md) roughness, metallic, ao and
emission. A record's roughness, metallic and emission **multiply** the map of
their kind, as glTF factors do. For each of the three, a new record starts at
**1** instead of the default above when its texture has a map of that kind,
so the map shows as painted; a modifier still sets the number. Without a map
the number stands alone, and emission multiplies the texture's colour image.
An AO map has no number. A texture without maps keeps the defaults, so a
model that uses none is unchanged.

### Modifiers

Each modifier pops a skin and its argument, copies the record with one field
changed and pushes the new record's number. The source record is unchanged.

| Instruction | Change | Check |
| --- | --- | --- |
| `skin.density` | density | finite and positive |
| `skin.fit` | mapping = fit | — |
| `skin.shift` | shift | both components finite |
| `skin.roughness` | roughness | in 0–1 |
| `skin.metallic` | metallic | in 0–1 |
| `skin.emission` | emission | finite and not negative |
| `skin.cutout` | alpha = cutout, cutoff | in 0–1 |
| `skin.two_sided` | two-sided = yes | — |
| `skin.crisp` | crisp = yes | — |
| `skin.blend` | alpha = blend | — |
| `skin.region` | mapping = region, window (x, y, w, h) | inside the image |

`skin.region` pops `h`, `w`, `y` and `x`, in the units of the declared size
`aw × ah` of the texture's program. The window must have finite values,
`w > 0`, `h > 0`, `x ≥ 0`, `y ≥ 0`, `x + w ≤ aw` and `y + h ≤ ah`, the sums
computed in binary64; otherwise it traps with `NumericRange`
(`skin region must lie inside the image`). A region record never marks its
texture as repeating.

A failed check traps with `NumericRange`. A generation holds at most 1024
records; one more traps with `RuntimeViolation` (`model exceeds 1024 skin records`).

## Vertices and mapping

A vertex has a position, a normal, a tangent with a sign and a UV pair. A
primitive states them in the **local space** of the current scope, together
with face coordinates `(s, t)` in model units and the size `(fw, fh)` of the
face they lie on.

```
position = f32(point(p))
normal   = f32(normal(n))
tangent  = (f32(tangent(t)), w)        // w is +1, or -1 in a flipped scope
```

`s` grows rightward and `t` downward for a viewer outside the face; the
tangent is the direction of growing `s`, and `w * (normal × tangent)` points
up the image. With `aw × ah` the declared size of the texture's program:

```
repeat:  u = (s * density + shift.x) / aw      v = (t * density + shift.y) / ah
fit:     u = s / fw                            v = t / fh
region:  u = (x + c(s / fw) * w) / aw          v = (y + c(t / fh) * h) / ah
         c(a) = 0 if not a > 0, 1 if a > 1, else a
uv = (f32(u), f32(v))
```

UV `(0, 0)` is the upper left corner of the image. A placement's scale does
not change `(s, t)`. The clamp keeps every vertex of a region inside its
window, also on a bevel or a pole whose face coordinates leave the face.
Filtering in a host still reads texels next to the window's edge.

A position that is not finite after rounding traps with `NumericRange`
(`model coordinate out of range`).

## Primitives

Every primitive checks its arguments, reserves its fuel, work and memory and
only then emits. The skin is checked first (see [types](#types)), then the
other arguments by the rules of the primitive. Failed checks trap with
`NumericRange` unless stated. Floats must be finite. Vertices are emitted in
the stated order; a triangle list names them by their order within the
primitive.

A **face** below is four vertices `BL, BR, TR, TL` (lower left, lower right,
upper right, upper left for a viewer outside) with one normal `N` and one
tangent `T`, face coordinates `(0, fh)`, `(fw, fh)`, `(fw, 0)`, `(0, 0)` and
the triangles `(0, 1, 2)`, `(0, 2, 3)`.

### `box`

`box(at, size, skin)` with every component of `size` positive. With
`(w, h, d) = size`:

```
x0 = at.x - w*0.5    x1 = at.x + w*0.5
y0 = at.y            y1 = at.y + h
z0 = at.z - d*0.5    z1 = at.z + d*0.5
```

Six faces, 24 vertices and 12 triangles, in this order:

| # | Face | `BL` | `BR` | `TR` | `TL` | `N` | `T` | `fw × fh` |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | front | `x0 y0 z1` | `x1 y0 z1` | `x1 y1 z1` | `x0 y1 z1` | `0 0 1` | `1 0 0` | `w × h` |
| 1 | right | `x1 y0 z1` | `x1 y0 z0` | `x1 y1 z0` | `x1 y1 z1` | `1 0 0` | `0 0 -1` | `d × h` |
| 2 | back | `x1 y0 z0` | `x0 y0 z0` | `x0 y1 z0` | `x1 y1 z0` | `0 0 -1` | `-1 0 0` | `w × h` |
| 3 | left | `x0 y0 z0` | `x0 y0 z1` | `x0 y1 z1` | `x0 y1 z0` | `-1 0 0` | `0 0 1` | `d × h` |
| 4 | top | `x0 y1 z1` | `x1 y1 z1` | `x1 y1 z0` | `x0 y1 z0` | `0 1 0` | `1 0 0` | `w × d` |
| 5 | bottom | `x0 y0 z0` | `x1 y0 z0` | `x1 y0 z1` | `x0 y0 z1` | `0 -1 0` | `1 0 0` | `w × d` |

### `box.bevel`

`box.bevel(at, size, bevel, skin)` needs `bevel > 0` and
`2*bevel < min(w, h, d)`, else `NumericRange`
(`bevel must be positive and less than half the shortest side`).

Each face keeps its plane and its row of the `box` table, with every corner
moved inward by `b = bevel` along both face axes: its face coordinates run
from `b` to `fw - b` and from `b` to `fh - b`. These are vertices 0–23.

Twelve **edge strips** follow, then eight **corner triangles**; 96 vertices
and 44 triangles in all. An edge strip joins the inset edges of two faces
`P < Q` (by face number). It is a quad of four new vertices: the two ends of
`P`'s inset edge with `P`'s normal and face coordinates, and the two ends of
`Q`'s inset edge with `Q`'s normal and with `P`'s face coordinates continued
across the edge by `e = b * SQRT2`. The tangent of the two `Q` vertices is
`Q`'s when the edge is vertical (both faces are side faces) and `P`'s
otherwise. Strip order and the side of `P` each lies on:

| Strips | `P` | `Q` in order |
| --- | --- | --- |
| 0–3 | front | right (right edge), left (left edge), top (top edge), bottom (bottom edge) |
| 4–6 | right | back (right edge), top (top edge), bottom (bottom edge) |
| 7–9 | back | left (right edge), top (top edge), bottom (bottom edge) |
| 10–11 | left | top (top edge), bottom (bottom edge) |

A strip on `P`'s right edge has the vertices `P.BR`, `Q.BL`, `Q.TL`, `P.TR`
with face coordinates `(fw-b, fh-b)`, `(fw-b+e, fh-b)`, `(fw-b+e, b)`,
`(fw-b, b)`; on its left edge `Q.BR`, `P.BL`, `P.TL`, `Q.TR` with
`(b-e, fh-b)`, `(b, fh-b)`, `(b, b)`, `(b-e, b)`. A strip on `P`'s top edge
has `P.TL`, `P.TR`, then the two vertices of `Q` nearest them, with
`(b, b)`, `(fw-b, b)`, `(fw-b, b-e)`, `(b, b-e)`; on its bottom edge the two
vertices of `Q` nearest `P.BL` and `P.BR`, then `P.BR`, `P.BL` with
`(b, fh-b+e)`, `(fw-b, fh-b+e)`, `(fw-b, fh-b)`, `(b, fh-b)`. Each strip has
the triangles `(0, 1, 2)`, `(0, 2, 3)`.

A corner triangle joins the three inset corners that meet at a box corner,
counterclockwise from outside, starting with the side face of the lowest
number. Corner order: top corners at front-left, front-right, back-right,
back-left, then the bottom corners in the same order. Each vertex has its own
face's normal; the first has its face's coordinates and tangent, the other
side-face vertex continues them horizontally by `e` away from the face with
its own face's tangent, and the top or bottom vertex continues them
vertically by `e` with the first vertex's tangent.

### `cylinder`, `cone` and `band`

`cone(at, r0, r1, height, sides, skin)`; `cylinder(at, r, height, sides,
skin)` is `cone` with `r0 = r1 = r`; `band` is `cone` without caps. Radii
must not be negative and not both zero (`radius must be positive`), `height`
positive, `sides` in 3–64 (`sides must be in 3..64`).

The **column directions** for `j = 0 … sides`:

```
if (4*j) mod sides == 0:
    dir = [(0, 0, -1), (-1, 0, 0), (0, 0, 1), (1, 0, 0)][(4*j / sides) mod 4]
else:
    a = (j * TAU) / sides
    dir = (-sin64(a), 0, -sin64(a + PIO2))
```

Column 0 faces the back, and the columns run to the left, the front and the
right. Column `sides` repeats column 0: it is the seam.

The side is one **band** between a lower ring (radius `r0` at `at.y`) and an
upper ring (`r1` at `at.y + height`):

```
R = max(r0, r1)        C = TAU * R        L = sqrt(height*height + (r0 - r1)*(r0 - r1))
lower row, j = 0 … sides:   p = (at.x + dir.x*r0, at.y,          at.z + dir.z*r0),  (s, t) = (C * (j / sides), L)
upper row, j = 0 … sides:   p = (at.x + dir.x*r1, at.y + height, at.z + dir.z*r1),  (s, t) = (C * (j / sides), 0)
n = unit((dir.x*height, r0 - r1, dir.z*height))        T = (dir.z, 0, -dir.x)        (fw, fh) = (C, L)
```

For `j = 0 … sides-1`, with `a = lower j`, `b = lower j+1`, `c = upper j+1`,
`d = upper j`: the triangle `(a, b, c)` unless `r0 = 0`, then `(a, c, d)`
unless `r1 = 0`.

`cone` and `cylinder` then add a **lower cap** when `r0 > 0` and an **upper
cap** when `r1 > 0`. A cap of radius `r` at height `y` is a centre vertex
followed by ring vertices `j = 0 … sides-1` at `(at.x + dir.x*r, y, at.z + dir.z*r)`;
`T = (1, 0, 0)` and `(fw, fh) = (2r, 2r)`.

| Cap | `N` | `(s, t)` of the offset `(lx, lz)` | Triangles for `j = 0 … sides-1` |
| --- | --- | --- | --- |
| lower | `0 -1 0` | `(lx + r, r - lz)` | `(centre, ring j+1, ring j)` |
| upper | `0 1 0` | `(lx + r, lz + r)` | `(centre, ring j, ring j+1)` |

Ring `sides` is ring 0, `(lx, lz) = (dir.x*r, dir.z*r)` and the centre has
`(r, r)`. Like the top and the bottom of a `box`, the upper cap's image has
the back of the model at its top and the lower cap's image the front.

### `sphere`

`sphere(at, radius, sides, rings, skin)` with a positive radius, `sides` in
3–64 and `rings` in 2–32 (`rings must be in 2..32`). The centre is
`c = (at.x, at.y + radius, at.z)`. For `i = 0 … rings`:

```
if i == 0:            (sp, cp) = (0, 1)
else if i == rings:   (sp, cp) = (0, -1)
else if 2*i == rings: (sp, cp) = (1, 0)
else:                 p = (i * PI) / rings;  (sp, cp) = (sin64(p), sin64(p + PIO2))
```

Vertices in rows `i = 0 … rings`, each with columns `j = 0 … sides`:

```
n = (dir.x*sp, cp, dir.z*sp)        p = c + n*radius        T = (dir.z, 0, -dir.x)
C = TAU * radius        H = PI * radius
(s, t) = (C * (j / sides), H * (i / rings))        (fw, fh) = (C, H)
```

For each row `i < rings` and column `j < sides`, with `a = (i+1, j)`,
`b = (i+1, j+1)`, `c = (i, j+1)`, `d = (i, j)`: the triangle `(a, b, c)`
unless `i = rings-1`, then `(a, c, d)` unless `i = 0`.

### `lathe`

`lathe(at, profile, sides, skin)` and `lathe.crease(at, profile, sides,
crease, skin)`. The profile is a list of 2–64 points `(r, y)` with finite
components and `r` not negative (`lathe profile needs 2..64 points`,
`lathe radius must not be negative`); `sides` is in 3–64; `crease` is in
0–180 degrees (`crease must be in 0..180`). `lathe` is `lathe.crease` with a
crease of zero.

Segment `k` joins points `k` and `k+1`. With `(dr, dy)` their difference and
`len = sqrt(dr*dr + dy*dy)`: a zero `len` traps (`lathe profile repeats a point`)
and so does a segment with both radii zero (`lathe segment lies on the axis`).
The segment's profile normal is `(nr, ny) = (dy / len, -dr / len)`: the
outside is on the right of a profile that runs counterclockwise around the
solid's cross-section, which for a simple shape means from the bottom up.

```
cum[0] = 0        cum[k+1] = cum[k] + len[k]        total = cum[last]
Rmax = the largest r        C = TAU * Rmax        (fw, fh) = (C, total)
```

The joint at an inner point `k` is **smooth** when

```
(dr[k-1]*dr[k] + dy[k-1]*dy[k]) / (len[k-1] * len[k]) > c      where (c, _) = turn(crease)
```

and the sum of the two profile normals is not zero. A crease of zero makes
every joint hard, whatever the quotient rounds to. A smooth joint has one
row of vertices with the profile normal `unit2` of that sum
(`(x, y) / sqrt(x*x + y*y)`); any other joint has two rows, one per segment.

Rows are emitted segment by segment: the segment's lower row unless it is
shared with the previous segment, then its upper row. A row at point `k` with
profile normal `(nr, ny)` has columns `j = 0 … sides`:

```
p = (at.x + dir.x*r[k], at.y + y[k], at.z + dir.z*r[k])
n = unit((dir.x*nr, ny, dir.z*nr))        T = (dir.z, 0, -dir.x)
(s, t) = (C * (j / sides), total - cum[k])
```

Each segment then has the triangles of a [band](#cylinder-cone-and-band)
between its two rows, leaving out the triangle that collapses at a zero
radius. All rows come first, then the triangles in segment order.

Finally a **start cap** when `r[0] > 0` (a lower cap of radius `r[0]` at
`at.y + y[0]`) and an **end cap** when the last radius is positive (an upper
cap at the last height).

### `extrude`

`extrude(at, outline, depth, skin)` and `extrude.bevel(at, outline, depth,
bevel, skin)`. The outline is a list of 3–256 finite points `(x, y)`
(`extrude outline needs 3..256 points`); `depth` is positive.

1. `A2 = Σ (x[i]*y[i+1] - x[i+1]*y[i])` over the closed outline, added in
   order. Zero traps with `RuntimeViolation` (`extrude outline has no area`).
   When negative, the outline becomes `p0, p[n-1], p[n-2], …, p1`.
2. Two consecutive equal points trap (`extrude outline repeats a point`). Two
   edges that are not neighbours and [touch](#segment-contact) trap with
   `RuntimeViolation` (`extrude outline is self-intersecting`).
3. `z1 = at.z + depth*0.5`, `z0 = at.z - depth*0.5`; point `i` lies at
   `(at.x + x[i], at.y + y[i])`. `minX`, `maxX`, `minY`, `maxY` bound the
   outline; `W = maxX - minX`, `H = maxY - minY`.
4. **Front cap**: vertices `i = 0 … n-1` at `z1`, `N = (0, 0, 1)`,
   `T = (1, 0, 0)`, `(s, t) = (x[i] - minX, maxY - y[i])`, `(fw, fh) = (W, H)`,
   and the triangles of the [triangulation](#triangulation).
5. **Back cap**: vertices `i = 0 … n-1` at `z0`, `N = (0, 0, -1)`,
   `T = (-1, 0, 0)`, `(s, t) = (maxX - x[i], maxY - y[i])`, and the same
   triangles as `(a, c, b)`.
6. **Sides**: for each edge `i` from point `i` to point `i+1` (the last to
   point 0), with `(dx, dy)` their difference, `len = sqrt(dx*dx + dy*dy)`,
   `P[0] = 0`, `P[i+1] = P[i] + len[i]` and `total` the perimeter: one face
   with `BL = (p[i], z0)`, `BR = (p[i+1], z0)`, `TR = (p[i+1], z1)`,
   `TL = (p[i], z1)`, `N = (dy/len, -dx/len, 0)`, `T = (dx/len, dy/len, 0)`,
   face coordinates `(P[i], depth)`, `(P[i+1], depth)`, `(P[i+1], 0)`,
   `(P[i], 0)` and `(fw, fh) = (total, depth)`.

`extrude.bevel` needs `bevel > 0` and `2*bevel < depth`
(`bevel must be positive and less than half the depth`) and builds the
**inset outline** `q`: with `n[i]` the side normal of edge `i` and `m` the
edge before it,

```
k = 1 + (n[m].x*n[i].x + n[m].y*n[i].y)
q[i] = (x[i] - bevel * (n[m].x + n[i].x) / k,  y[i] - bevel * (n[m].y + n[i].y) / k)
```

A `k` that is not positive, an inset edge that points against its outline
edge (their dot product is not positive) or an inset outline that fails steps
1–2 traps with `RuntimeViolation` (`extrude bevel collapses the outline`).
The caps then use `q` for their positions and triangulation and keep the
bounds of the original outline for their face coordinates. A side face runs
from `z0 + bevel` to `z1 - bevel` with `t` from `depth - bevel` down to
`bevel`. After the sides come, per edge, a **front strip**
`(p[i], z1 - bevel)`, `(p[i+1], z1 - bevel)`, `(q[i+1], z1)`, `(q[i], z1)`
and then a **back strip** `(q[i], z0)`, `(q[i+1], z0)`, `(p[i+1], z0 + bevel)`,
`(p[i], z0 + bevel)`. The outline vertices of a strip have the side normal
and the inset ones the cap normal; all four have the side tangent. With
`e = bevel * SQRT2` their `s` is `P[i]` or `P[i+1]` and their `t` is `bevel`
and `bevel - e` on the front strip, `depth - bevel + e` and `depth - bevel`
on the back strip. Strips have the triangles `(0, 1, 2)`, `(0, 2, 3)`.

### Segment contact

`orient(a, b, c) = (b.x - a.x)*(c.y - a.y) - (b.y - a.y)*(c.x - a.x)`.
Segments `ab` and `cd` touch when `orient(a, b, c)` and `orient(a, b, d)` have
opposite signs and so do `orient(c, d, a)` and `orient(c, d, b)`, or when one
of the four is zero and that point lies within the bounding box of the other
segment.

### Triangulation

For a counterclockwise outline of `n` points, with a list of the remaining
vertex numbers that starts as `0 … n-1`:

```
while more than three remain:
    take the first position i in the list whose vertex v, with its neighbours u before and w after, satisfies
        orient(u, v, w) > 0, and
        no other remaining vertex x has orient(u, v, x) >= 0, orient(v, w, x) >= 0 and orient(w, u, x) >= 0
    emit (u, v, w) and remove v
    if no position qualifies: trap with RuntimeViolation (extrude outline cannot be triangulated)
emit the last three in list order
```

### `quad` and `tri`

`quad(a, b, c, d, skin)` takes the corners `BL, BR, TR, TL`.

```
N = unit(cross(c - a, d - b))
T = unit((b - a) - N * dot(b - a, N))
w = +1 when dot(cross(N, T), d - a) >= 0, else -1
fw = |b - a|        fh = |d - a|
```

It is one face with those four vertices. `tri(a, b, c, skin)`:

```
N = unit(cross(b - a, c - a))        T = unit(b - a)        w = +1
fw = |b - a|        fh = |cross(b - a, c - a)| / fw
(s, t) = (0, fh), (fw, fh), (dot(c - a, T), 0)
```

with the triangle `(0, 1, 2)`. `|v|` is `sqrt((v.x*v.x + v.y*v.y) + v.z*v.z)`.
In a flipped scope the sign `w` is negated like every other.

## Shapes along a path

Below, vectors combine component by component in the written order:
`a + b*k` is `(a.x + b.x*k, …)`; `dot`, `cross` and `unit` are those of the
[conventions](#conventions). The **start frame** of a path leaving in the
unit direction `T`:

```
R = (0, 1, 0)                 if T.y*T.y < 0.75
R = (0, 0, -1) if T.y > 0, else (0, 0, 1)    otherwise
V = unit(R - T*dot(R, T))
U = cross(V, T)
```

`settle(T, U)` makes a frame of a direction again:
`U' = unit(U - T*dot(U, T))`, `V' = cross(T, U')`.

### `tube`

`tube(a, b, c, d, r0, r1, sides, segments, skin)`: every argument finite
(`tube needs finite arguments`), radii not negative and not both zero
(`radius must be positive`), `sides` in 3–64 and `segments` in 1–256
(`segments must be in 1..256`).

With `n = segments`, rings `i = 0 … n` at `t = i / n`, `s = 1 - t`:

```
w0 = (s*s)*s   w1 = ((3*s)*s)*t   w2 = ((3*s)*t)*t   w3 = (t*t)*t
X[i] = ((a*w0 + b*w1) + c*w2) + d*w3
D[i] = ((b - a)*(s*s) + (c - b)*((2*s)*t)) + (d - c)*(t*t)
r[i] = r0 + (r1 - r0)*t
cum[0] = 0      cum[i+1] = cum[i] + |X[i+1] - X[i]|      L = cum[n]
```

A zero `D[i]` is replaced by `X[i+1] - X[i]` (by `X[n] - X[n-1]` for the
last ring); still zero, it traps with `RuntimeViolation`
(`tube curve has no direction`). `T[i] = unit(D[i])`. Ring 0 takes the start
frame of `T[0]`; ring `i + 1` takes the double reflection of ring `i`:

```
v1 = X[i+1] - X[i]      c1 = dot(v1, v1)
if c1 > 0:  U = U[i] - v1*((2*dot(v1, U[i]))/c1)    t = T[i] - v1*((2*dot(v1, T[i]))/c1)
else:       U = U[i]                                 t = T[i]
v2 = T[i+1] - t         c2 = dot(v2, v2)
if c2 > 0:  U = U - v2*((2*dot(v2, U))/c2)
U[i+1], V[i+1] = settle(T[i+1], U)
```

With the [column directions](#cylinder-cone-and-band) `(dx, dz)` of `sides`,
`C = TAU * max(r0, r1)` and `k = r0 - r1`, ring `i` has columns
`j = 0 … sides`:

```
radial = U[i]*dx - V[i]*dz
p = X[i] + radial*r[i]
n = unit(radial*L + T[i]*k)        T = U[i]*dz + V[i]*dx
(s, t) = (C * (j / sides), L - cum[i])        (fw, fh) = (C, L)
```

All rings come first, then the triangles of a [band](#cylinder-cone-and-band)
between rings `i` and `i + 1` for each `i`, leaving out the triangle that
collapses at a ring of radius zero. Then a cap at ring 0 when `r0 > 0` and at
ring `n` when `r1 > 0`, each like a cone's with the cone's X along `U` and its
Z along `-V`: a centre `X` and the ring points `X + U*lx - V*lz` with
`(lx, lz) = (dx*r, dz*r)`, the normal `-T` for the first and `T` for the
last, the tangent `U`, face coordinates and triangles as the lower and the
upper cap of a cone.

### `sweep`

`sweep(path, profile, skin)`: a path of 2–256 finite points
(`sweep path needs 2..256 points`, `sweep needs finite arguments`) and a
profile of 3–256 points, checked and oriented as an
[extrude outline](#extrude) with the messages `sweep profile has no area`,
`sweep profile repeats a point` and `sweep profile is self-intersecting`, and
[triangulated](#triangulation) (`sweep profile cannot be triangulated`).
Fuel reserves `ceil(n*n/3)` for a profile of `n` points and 24 for each
point of the path.

Segment `k` runs from path point `k` to `k + 1`: a zero difference traps
(`sweep path repeats a point`), `d[k] = unit(P[k+1] - P[k])` and `cum` sums
the lengths `|P[k+1] - P[k]|`, `L` the whole. Segment 0 takes the start
frame of `d[0]`. At each inner point `k`:

```
B = d[k-1] + d[k]        zero: RuntimeViolation (sweep path turns back on itself)
m = unit(B)      along = dot(d[k-1], m)
RU[k] = U[k-1] - d[k-1]*(dot(U[k-1], m)/along)       RV[k] the same with V[k-1]
U[k], V[k] = settle(d[k], U[k-1] - m*(2*dot(U[k-1], m)))
```

The ends take the axes of their segment: `RU[0] = U[0]` and the last point
those of the last segment. A profile point `q` lies at
`ring(k, q) = (P[k] + RU[k]*q.x) + RV[k]*q.y`. With `minX … maxY` the bounds
of the profile and `W`, `H` its size:

1. A **start cap**: `ring(0, q)` of each point, `N = -d[0]`, `T = -U[0]`,
   `(s, t) = (maxX - q.x, maxY - q.y)`, `(fw, fh) = (W, H)`, the triangles of
   the profile as `(a, c, b)`.
2. An **end cap** at the last point: `N` and `T` the direction and `U` of the
   last segment, `(s, t) = (q.x - minX, maxY - q.y)`, the triangles as they
   are.
3. For each segment `k`, for each profile edge from `q` to `r`, with
   `(dx, dy) = r - q`, `e = sqrt(dx*dx + dy*dy)` and `around` the profile's
   perimeter up to each point: a face of the vertices `ring(k, q)`,
   `ring(k, r)`, `ring(k+1, r)`, `ring(k+1, q)` with
   `N = U[k]*(dy/e) + V[k]*(-dx/e)`, `T = U[k]*(dx/e) + V[k]*(dy/e)`, face
   coordinates `(around(q), L - cum[k])`, `(around(r), L - cum[k])`,
   `(around(r), L - cum[k+1])`, `(around(q), L - cum[k+1])` and
   `(fw, fh) = (perimeter, L)`.

## Shaping blocks

A shaping block pops its arguments and checks them, in this order: the
nesting limit is the [place](#placement-scopes) one, and every failed check
traps with `NumericRange`:

| Block | Check |
| --- | --- |
| `smooth(angle)` | `angle` in 0–180 (`smooth angle must be in 0..180`) |
| `refine(count)` | `count` in 1–16 (`refine count must be in 1..16`) |
| `bend(angle)` | `angle` in -360–360 (`bend angle must be in -360..360`) |
| `twist(angle)` | finite (`twist angle must be finite`) |
| `taper(scale)` | finite and positive (`taper scale must be finite and positive`) |
| `displace(amount, frequency, seed)` | finite amount, finite positive frequency (`displace needs a finite amount and a positive frequency`) |

It reserves 24 fuel units and opens a [block](#blocks-and-buffers). What it
does when it closes follows. Vertices of **equal position** compare their
binary64 coordinates by value; groups of them are taken in order of their
first vertex. `settled(t, n)` is `unit(t - n*dot(t, n))`, or `t` when that
difference is zero.

**smooth** pays 8 fuel units and one work step per vertex. An angle of 0
changes nothing. Otherwise, with `(c, _) = turn(angle)`, each vertex `v`
sums, in group order, the normals of the vertices `u` at its position for
which `u = v` or `dot(n[u], n[v]) > c`. A vertex keeps its normal and its
tangent when every other `u` that passes has a normal equal to its own (a
copy on a seam of the image), or none passes, or its sum is zero; any other
takes `unit` of its sum as its normal and `settled(t, n)` as its tangent.
All new normals are found before any is set.

**refine(n)** adds, for `E` distinct pairs of vertex numbers that are edges
of triangles and `T` triangles, `E*(n-1) + T*(n-1)*(n-2)/2` vertices and
`T*(n*n - 1)` triangles, checked against the limits and paid before it
works: 5 fuel units and 48 bytes per added vertex, 2 fuel units and 16 bytes
per added triangle, one work step per added vertex and per resulting
triangle. The vertices it adds follow the existing ones. Point `(i, j)` of a
triangle `(a, b, c)`, `i + j ≤ n`, is `a` for `(0, 0)`, `b` for `(n, 0)` and
`c` for `(0, n)`; on the edge `ab` (`j = 0`), `ac` (`i = 0`) or `bc`
(`i + j = n`, at `j` from `b`) it is the edge's point at that fraction, and
inside it is new:

```
wa = (n-i-j)/n   wb = i/n   wc = j/n
p = (a.p*wa + b.p*wb) + c.p*wc      n = unit((a.n*wa + b.n*wb) + c.n*wc)
t = unit((a.t*wa + b.t*wb) + c.t*wc)      w = a.w      uv = f32((a.uv*wa + b.uv*wb) + c.uv*wc)
```

with the triangle's skin. The `n - 1` points of an edge are made once, when
first needed, walking from the end whose position is smaller (by `x`, then
`y`, then `z`; at equal positions, by number): point `m` at
`f = m/n` from start `p` to end `q` is `p + (q - p)*f`, its normal
`unit(n_p + (n_q - n_p)*f)`, its tangent likewise, `w` and skin of `p`, and
`uv = f32(uv_p + (uv_q - uv_p)*f)`. Triangles are visited in order, lattice
points row by row (`j` outer, `i` inner), and each triangle becomes, for each
row `j < n` and `i < n - j`, `((i,j), (i+1,j), (i,j+1))` and, when
`i + j + 1 < n`, `((i+1,j), (i+1,j+1), (i,j+1))`.

**bend**, **twist** and **taper** pay 3 fuel units and one work step per
vertex. They act over `[y0, y1]`, the least and greatest `y` of the buffer's
positions, with `L = y1 - y0`; with no vertex, with `L` not positive, or with
a bend or twist of 0 or a taper of 1 they change nothing. Otherwise every
vertex at `p` moves to `F(p)`, its normal becomes `unit(C(J)*n)` with `C` the
cofactors of the [placement scopes](#placement-scopes) and its tangent
`unit(J*t)`, where for `u = (p.y - y0)/L`:

```
bend:   a = angle*RAD   R = L/a   phi = u*a   s = sin64(phi)   c = sin64(phi + PIO2)
        l = R - p.x     k = l/R   (not k > 0: RuntimeViolation, bend is too tight for its geometry)
        F = (R - l*c, y0 + l*s, p.z)              J = | c   k*s  0 |
                                                      | -s  k*c  0 |
                                                      | 0   0    1 |
twist:  a = angle*RAD   th = u*a   s = sin64(th)   c = sin64(th + PIO2)   q = a/L
        x' = c*p.x + s*p.z    z' = c*p.z - s*p.x
        F = (x', p.y, z')                           J = | c    q*z'     s |
                                                      | 0    1        0 |
                                                      | -s   -(q*x')  c |
taper:  g = 1 + (scale - 1)*u    m = (scale - 1)/L
        F = (p.x*g, p.y, p.z*g)                     J = | g  p.x*m  0 |
                                                      | 0  1      0 |
                                                      | 0  p.z*m  g |
```

**displace** pays 18 fuel units and one work step per vertex, 4 fuel units
and one work step per triangle. An amount of 0 changes nothing. Otherwise
each group of equal positions `p` sums the
normals of its vertices in order; a zero sum leaves the group where it is,
any other moves every vertex of the group to
`p + unit(sum)*(amount*noise3(p.x*f, p.y*f, p.z*f, seed))` with `f` the
frequency. Then each triangle, in order, adds its face vector
`cross(b' - a', c' - a')` of the moved positions to the sum kept for each of
its corners, in corner order, keyed by the corner's position and normal
before the move. Each vertex takes `unit` of its key's sum as its normal, or
keeps its normal when the sum is zero, and its tangent becomes
`settled(t, n)`.

### Noise in space

`noise3(x, y, z, seed)` is gradient noise in binary64, in about -1 to 1.
Each coordinate must be finite with its `floor` in
`[-2147483648, 2147483646]`, else it traps with `NumericRange`
(`noise coordinate out of range`).

```
for each corner (dx, dy, dz) in {0,1}^3, z outer, then y, then x:
    h = hash(seed ^ ((ix+dx)*73856093) ^ ((iy+dy)*19349663) ^ ((iz+dz)*83492791))   // u32 wraparound
    g = G[h & 15]
    n[dz][dy][dx] = (g.x*(fx - dx) + g.y*(fy - dy)) + g.z*(fz - dz)
u = fade(fx)   v = fade(fy)   w = fade(fz)      fade(t) = ((t*t)*t)*((t*((t*6) - 15)) + 10)
a00 = n000*(1-u) + n001*u    a10 = n010*(1-u) + n011*u    a01 = n100*(1-u) + n101*u    a11 = n110*(1-u) + n111*u
b0 = a00*(1-v) + a10*v       b1 = a01*(1-v) + a11*v
noise3 = b0*(1-w) + b1*w
```

`ix = floor(x)`, `fx = x - floor(x)` and so on, `n` indexed `[dz][dy][dx]`;
`hash` is [§12.1](iprop-v0.8.md#121-hashx) and `G` the sixteen directions
`(1,1,0) (-1,1,0) (1,-1,0) (-1,-1,0) (1,0,1) (-1,0,1) (1,0,-1) (-1,0,-1)
(0,1,1) (0,-1,1) (0,1,-1) (0,-1,-1) (1,1,0) (0,-1,1) (-1,1,0) (0,-1,-1)`.

## Outputs

`out.point3d` and `out.list.point3d` pop a point and reserve 8 fuel units;
a component that is not finite traps with `NumericRange` (`a point3d output needs finite
components`). `out.box3d` and `out.list.box3d` pop the size, then `at`, and
reserve 16 fuel units: a value that is not finite or a size component that is not positive traps with
`NumericRange` (`a box3d output needs finite values and a positive size`).
A box then traps with `RuntimeViolation` when the current scope is
**sheared** (`a box3d output cannot be turned inside an unequal scale`) or
when a bend, twist or taper block is open between the current scope and the
frame of the running asset (`a box3d output cannot be written inside bend,
twist or taper`).

A scope is sheared when its parent is, or when its parent has an **unequal
scale** and it turns: `yaw`, `pitch` or `roll` given and not an exact
multiple of 90 degrees. A scope has an unequal scale when its parent has or
when its own `scale` is a `vec3` with two different components. Blocks keep
their parent's state; a part's block starts with neither.

A point is `p`, a box its centre `(at.x, at.y + size.y*0.5, at.z)` and the
three edges `(size.x, 0, 0)`, `(0, size.y, 0)`, `(0, 0, size.z)`, unflipped,
all binary64. The value is **delivered** to the current scope:

```
p' = point(p)                     with that scope's map
e' = A*e for each edge            flipped' = flipped xor the scope's flipped
```

If the scope writes into a shaping block, the value waits there as a
**pending output**. When the block closes, a bend, twist or taper moves the
point of each pending output by `F` (with the same rules, without a trap),
and each is delivered in order to the enclosing scope. Otherwise (the
model, or the block of a part call) the value is **committed** to its slot,
with one work step:

- a point stores `f32` of each coordinate, which must stay finite
  (`model coordinate out of range`);
- a box stores the centre likewise, and for each edge `e`
  `len = sqrt((e.x*e.x + e.y*e.y) + e.z*e.z)`, which must be positive and
  finite (`RuntimeViolation`, `degenerate direction`), the size `f32(len)`
  and the axis `f32(e/len)`, the third axis negated when flipped.

A singular slot keeps the latest value, a list slot every value in order.

## Parts

`part.render P` checks the call depth (`CallDepth`, `call depth exceeds
limit`), pops the seed and the arguments of the part's entry, reserves 24
fuel units and empties every slot of part `P`. It opens a
[block](#blocks-and-buffers) for the part and runs its entry in a frame of
its own, as a part call of a 2D program does: its stream and its initial
seed are the seed, and the caller continues after the instruction when the
frame returns. Values the part writes to its slots are committed in the
frame of its block, which is the frame of the scope that called it.

When the part's frame returns, after its own scopes have closed, its block
closes as a shaping block does, without step 1 and 2. The calling frame then
records `P` as its last part and the calling scope as where it reads.

`part.get.point3d S` traps with `InvalidOutput` unless `P` is the calling
frame's last part (`slot does not belong to the last called part`) and the
current scope is the one it recorded (`a part's outputs are read in the
block that called it`), and with `PartOutputMissing` when the slot holds no
value (`part output not written`). It pushes the latest value.

## Ownership

Every function belongs to the model or to the part whose run holds it.
Validation fails with `InvalidInstruction` when a function calls a function
of another asset, writes a slot it does not own, reads a slot that is not a
part of its own asset's, or runs `part.render` on a part that is not its own
asset's. A part's entry that returns values fails with `InvalidSignature`.

## Accounting and limits

| Limit | Value |
| --- | --- |
| vertices of a model | 65 536 |
| triangles of a model | 131 072 |
| textures of a generation | 32 |
| texels of all textures | 4 194 304 |
| side of a texture | 2048 |
| skin records | 1024 |
| embedded programs | 64 |
| open scopes | 32 |
| output slots of a file | 256 |
| parts of a file | 256 |
| points of a sweep path | 256 |
| segments of a tube | 256 |

A primitive that would pass a vertex or triangle limit traps with
`RuntimeViolation` (`model exceeds 65536 vertices`, `model exceeds 131072
triangles`) before emitting anything; a texture past its limits traps with
`model exceeds 32 skin textures` or `model exceeds 4194304 skin texels`.

**Fuel.** Every instruction costs one unit. After its checks, a primitive
reserves 6 more for each vertex and 2 for each triangle it emits, and an
extrude also `ceil(n*n/3)` for an outline of `n` points, `ceil(n*n/2)` with a
bevel, because the checks and the triangulation of an outline grow with the
square of its points; a sweep `ceil(n*n/3)` for its profile. `place.begin`
reserves 24 more and `skin.draw` 64 more, also when it reuses a texture.
Shaping blocks and parts pay as their sections state. A texture's own generation spends the
embedded program's budget, not the model's. These prices put a vertex, a
triangle and a placement at about the CPU time per unit of a plain
instruction; the `vec3` operations and the skin modifiers cost one unit each.

**Work.** One budget covers the model program, the textures and the packing
of the result. A primitive spends one step per vertex and per triangle. A
texture's generation runs with the steps that remain and its use is charged
to the model. Deadlines and cancellation stop either.

**Memory.** A vertex is charged 48 bytes, a triangle 16 and a skin record 64
to the 64 MiB generation arena, when a primitive or `refine` makes it;
passing it out of a block charges nothing. Textures are bounded by their own
limits.

A failed generation publishes nothing.

## Levels of detail

A generation takes a level of detail from the host, an integer from 0 to 7;
0 is the default. `model.lod` pushes it. The level is an input like the seed:
it changes nothing but what the program makes of it. A program that makes
every draw before it reads the level, or from named seeds, builds the same
prop at every level, with the same outputs and the same textures.

## Textures a host holds

A host may hand a generation the records of textures it already holds. A
record is a texture's key, the fuel and the work generating it cost, and the
kinds of its auxiliary images as bits (bit `k` for kind `k`). When `skin.draw`
asks for a texture whose key a record names, the first such record counts
and the texture is not generated:

- the model is charged the record's fuel and then its work, as it is charged
  for a generated texture, and fails with the same reason when either runs
  out, the message naming the skin;
- the texture counts toward the 32 textures and the texels of the
  generation, and its maps start a skin's numbers at one;
- the result reports it as reused, with its size, key, cost and kinds and
  without pixels.

A result reports the cost and the kinds of every texture, so each is the
record of a later generation. With true records the geometry, the outputs,
the counters and the failures of a generation are those of a generation
without records. A record that does not match its texture gives a model
with wrong numbers or counters, and nothing else: the runtime reads no
pixels of the host's.

### Stored textures

A texture kept beyond a process is stored as one IPTX file, named by its key
as sixteen lowercase hexadecimal digits and `.iptx`. All integers are little
endian.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | `IPTX` |
| 4 | 4 | file version, 1 |
| 8 | 8 | stamp |
| 16 | 8 | key |
| 24 | 4 + 4 | width, height |
| 32 | 4 + 4 | the program's declared width and height |
| 40 | 8 + 8 | fuel, work |
| 56 | 4 | kinds of the auxiliary images |
| 60 | 4 | number of auxiliary images |
| 64 | | name: u32 byte count and UTF-8 |
| | | the colour image: two f32 scales, then width*height RGBA8 pixels, rows from the top |
| | | each auxiliary image: u32 kind, u32 source, name, then an image as above |
| | 8 | FNV-1a 64 of every byte before it |

The stamp is the FNV-1a 64 of `IPTX`, the product version string and a u32
texture revision, which changes whenever the pixels, the fuel or the work of
a 2D program may. A file with another stamp or another file version is a
texture the host does not hold. The first 64 bytes carry the record, so a
directory is indexed without reading pixels; a file is verified when it is
read whole.

## Result

A successful generation yields:

- the bounds: the least and greatest stored position per axis, or all zero
  for a model without vertices;
- the nominal size and `unitsPerMeter`;
- the **textures** in order of first generation: key, program name, width,
  height, declared size, the RGBA8 image and its auxiliary images;
- the **surfaces**: the triangles grouped by the tuple (texture, roughness,
  metallic, emission, alpha, cutoff, two-sided, crisp) of their records
  (mapping, density, shift and a region change only UVs),
  compared by bits, in order of first use. A surface lists its vertices in
  emission order (positions, normals, tangents with sign, UVs) and `u32`
  indices, three per triangle. A vertex belongs to the surface of the
  triangles that use it;
- per texture whether it **repeats**: whether a triangle uses it through a
  record with the repeat mapping;
- the model's own **outputs** in slot order, each with its name, kind, list
  flag and values: a point as three binary32 values, a box as its centre,
  its size and its three axes, each three binary32 values;
- fuel and work counters, and the vertex and triangle counts.

The image of a texture has the 2D alpha contract. A surface whose alpha is
opaque ignores it; a cutout surface drops a texel below the cutoff; a blend
surface mixes the texel over what is behind it by its alpha and is drawn
after the others, without writing depth.

## Source language

Source 1.2 adds the following, only in an asset that has `render`:

- `render { … }` as the entry in place of `draw { … }`; `size WxHxD`;
  `scale N`.
- The type `vec3`, the literal `{x, y, z}`, members `.x`, `.y`, `.z`, the
  operators of `vec2`, `vec3.len`, `vec3.dot`, `vec3.cross`,
  `vec3.normalize`, `lerp`, and `to` on two `vec3` values: as for `vec2`,
  one draw picks a position between the two ends and `vec3.mix` gives the
  point there, so the result lies on the segment.
- The type `skin`; `alias.draw(seed, inputs…, size = {w, h})` as an
  expression for an imported asset that has `draw`; the `skin.*` modifiers.
- `alias.NAME` for a `const` declaration of an imported asset that has
  `draw`: the value is folded where it is read and nothing of the imported
  asset runs. A local or a parameter named like the alias is read instead;
  an unknown name is a compile error (`'ALIAS' declares no constant 'NAME'`).
  The asset's own `const` initializers cannot read it, and an asset that has
  `draw` cannot read its parts' constants.
- The primitives of [the model page](#the-model-page) under their mnemonics,
  with `box` and `extrude` taking an optional bevel and `lathe` an optional
  crease before the skin.
- `place(at =, yaw =, pitch =, roll =, scale =, mirror_x =, mirror_y =,
  mirror_z =) { … }`, named arguments only, at least one.
- `tube` and `sweep`, and the type `list<vec3>`: literals, constants, locals,
  parameters, results, indexing and `list.len`, no `.sort()` or
  `.distinct()`.
- The shaping blocks `smooth(a) { … }`, `refine(n) { … }`, `bend(a) { … }`,
  `twist(a) { … }`, `taper(k) { … }` and
  `displace(amount, frequency, seed) { … }`, positional arguments only; they
  share the limit of 32 open scopes with `place`.
- `output point3d NAME`, `output box3d NAME` and their `list` forms; a
  `point3d` is assigned a `vec3`, a `box3d` the literal `{at, size}` of two
  `vec3`. Assigning a `box3d` lexically inside `bend`, `twist` or `taper` is a
  compile error. A render asset declares no other output kind.
- An imported render asset is a **part**: `alias.render(seed, input = value,
  …)` is a statement, and `alias.outputs.NAME` reads one of its singular
  `point3d` outputs as a `vec3`. A part declares the same `scale` as the
  model.

Drawing, surfaces, 2D outputs and 2D geometry are compile errors in such an
asset, and everything above is unknown in an asset that has `draw`: there
`vec3` and `skin` are ordinary names and a compile error as the declared type
of a parameter, a result, a local or a constant. An asset that has `draw`
cannot import one that has `render`; a render asset imports assets that have
`draw` (skins), render assets (parts) and material libraries.

The authoring rules, with examples, are in the handbook chapter
[models](../docs/language/models.md).

A `const` may be a `vec3`. Its initializer may use the `vec3` literal,
members and operators, `vec3.len`, `vec3.dot`, `vec3.cross`,
`vec3.normalize` and `lerp`, and it is evaluated by the rules of
[the `vec3` operations](#the-vec3-operations): a constant has the bits the
running program would compute, and an initializer that would trap is a
compile error with the trap's message. Primitives, skins and `place` change
the model and are not constants. A `list<vec3>` constant is folded the same
way. A `vec3` is no element of a sampler or a stepper, and a `skin` no
element of any collection.
