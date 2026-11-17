# Models: the `.iprop3d` format and the extended VM (1.2 preview)

**Status:** Preview contract of milestone M1, not frozen. Sub-operation
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
- [Skins](#skins)
- [Vertices and mapping](#vertices-and-mapping)
- [Primitives](#primitives)
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

## Types

A model program has one more value type:

| Slot type | Encoding in function signatures | Runtime value |
| --- | --- | --- |
| vec3 | 21 | Three binary32 components in one stack slot |

It is storable in locals, parameters and results. It is not a list, sampler
or stepper element in M1. Its typed zero is `(0, 0, 0)`.

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
curves, seeded primitives and streams, scalar noise (not `noise.mask`),
`seed.initial`, lists, samplers, steppers and distributions. Canvas
instructions, layers, masks, clips, blur, `surface.normal`, structured
outputs, parts, dynamic parts, Voronoi, contours and regions are invalid, in
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

`skin` in a stack signature is an `i32`. An operand written 0 must be zero.
An aux value outside this table fails validation, in unreachable code too.
Ranges reserved for later milestones: `0x21–0x3F` scopes, `0x4C–0x6F`
primitives, `0x7A–0x8F` skins, `0x90–0x9F` queries and outputs, `0xA0–0xAF`
parts, `0xB0–0xBF` sculpting.

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
from the same `.iprop`. The generation runs under the embedded program's own
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
uv = (f32(u), f32(v))
```

UV `(0, 0)` is the upper left corner of the image. A placement's scale does
not change `(s, t)`.

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

A primitive that would pass a vertex or triangle limit traps with
`RuntimeViolation` (`model exceeds 65536 vertices`, `model exceeds 131072
triangles`) before emitting anything; a texture past its limits traps with
`model exceeds 32 skin textures` or `model exceeds 4194304 skin texels`.

**Fuel.** Every instruction costs one unit. After its checks, a primitive
reserves 6 more for each vertex and 2 for each triangle it emits, and an
extrude also `ceil(n*n/3)` for an outline of `n` points, `ceil(n*n/2)` with a
bevel, because the checks and the triangulation of an outline grow with the
square of its points. `place.begin` reserves 24 more and `skin.draw` 64 more,
also when it reuses a texture. A texture's own generation spends the
embedded program's budget, not the model's. These prices put a vertex, a
triangle and a placement at about the CPU time per unit of a plain
instruction; the `vec3` operations and the skin modifiers cost one unit each.

**Work.** One budget covers the model program, the textures and the packing
of the result. A primitive spends one step per vertex and per triangle. A
texture's generation runs with the steps that remain and its use is charged
to the model. Deadlines and cancellation stop either.

**Memory.** A vertex is charged 48 bytes, a triangle 16 and a skin record 64
to the 64 MiB generation arena. Textures are bounded by their own limits.

A failed generation publishes nothing.

## Result

A successful generation yields:

- the bounds: the least and greatest stored position per axis, or all zero
  for a model without vertices;
- the nominal size and `unitsPerMeter`;
- the **textures** in order of first generation: key, program name, width,
  height, declared size, the RGBA8 image and its auxiliary images;
- the **surfaces**: the triangles grouped by the tuple (texture, roughness,
  metallic, emission, alpha, cutoff, two-sided, crisp) of their records,
  compared by bits, in order of first use. A surface lists its vertices in
  emission order (positions, normals, tangents with sign, UVs) and `u32`
  indices, three per triangle. A vertex belongs to the surface of the
  triangles that use it;
- per texture whether it **repeats**: whether a triangle uses it through a
  record with the repeat mapping;
- fuel and work counters, and the vertex and triangle counts.

The image of a texture has the 2D alpha contract. A surface whose alpha is
opaque ignores it.

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
- The primitives of [the model page](#the-model-page) under their mnemonics,
  with `box` and `extrude` taking an optional bevel and `lathe` an optional
  crease before the skin.
- `place(at =, yaw =, pitch =, roll =, scale =, mirror_x =, mirror_y =,
  mirror_z =) { … }`, named arguments only, at least one.

Drawing, surfaces, 2D outputs and 2D geometry are compile errors in such an
asset, and everything above is unknown in an asset that has `draw`: there
`vec3` and `skin` are ordinary names and a compile error as the declared type
of a parameter, a result, a local or a constant. An asset that has `render`
cannot be imported, by either kind of asset; its own imports are assets that
have `draw` and material libraries.

The authoring rules, with examples, are in the handbook chapter
[models](../docs/language/models.md).

A `const` may be a `vec3`. Its initializer may use the `vec3` literal,
members and operators, `vec3.len`, `vec3.dot`, `vec3.cross`,
`vec3.normalize` and `lerp`, and it is evaluated by the rules of
[the `vec3` operations](#the-vec3-operations): a constant has the bits the
running program would compute, and an initializer that would trap is a
compile error with the trap's message. Primitives, skins and `place` change
the model and are not constants. In M1 a `vec3` or a `skin` is not an
element of a list, a sampler or a stepper.
