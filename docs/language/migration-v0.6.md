# Migration and contracts for 0.6

This chapter describes source language 0.6 as implemented in the current
checkout. It is in development on top of product 0.5.0; the product release
number is separate.

## Independent versions

| Contract | 0.5 release | Current 0.6 development |
| --- | --- | --- |
| Product | 0.5.0 | 0.5.0 until the 0.6.0 release |
| Source language | 0.5 | 0.6 |
| Binary format | 0.4, accepting legacy 0.1–0.3 | 0.5 draft when needed, accepting 0.1–0.4 |
| Runtime C API feature version | 0.3 | 0.3, unchanged |
| Shared-library compatibility line | SONAME/install-name 0.4 | 0.4, unchanged |

The compiler writes the oldest format that holds a file: 0.5 when the
compiled code uses a 0.5 instruction or has parts, otherwise 0.4 as before. A
runtime from
product 0.5.0 or older rejects a 0.5 file by its version instead of misreading
it, and still runs the 0.4 files. The current runtime loads and generates
frozen 0.1–0.4 files with unchanged pixels, outputs, RNG, traps and fuel. The
draft [format 0.5 specification](../../spec/iprop-v0.5.md) lists the new
instructions; compressed containers keep their 0.4 envelope.

## Color channel reads

`c.r`, `c.g`, `c.b` and `c.a` read the RGBA8 channels of any color value as
`i32` in 0–255 (see [color channels](drawing.md#color-channels)). In 0.5 such a
read was a compile error, so no accepted 0.5 source changes meaning. Channels
are read-only, and colors still do not convert to numbers implicitly.

## Random ranges

`lower to upper` draws a uniform value from the current stream (see
[random ranges](random.md#random-ranges-with-to)): an `i32` for two integer
bounds, otherwise an `f32`. It compiles to the format 0.5 `rng.int` and
`rng.float` instructions. `to` stays an ordinary name: a local, helper or
part named `to` keeps its meaning, and where an expression could end a
statement, `to = ...`, `to.draw(...)` and a call of a declared helper
`to(...)` still start the next statement. Write the operator in parentheses
to use it there, for example `x = (1 to (5))`.

## Gradients and segments

`to` also accepts two colors or two `vec2` values and draws one shared
position between them (see [gradients](random.md#gradients-and-segments)),
optionally steered by `with`. The format 0.5 instructions `color.mix.rgba` and
`vec2.mix` perform the mix; `color.mix()` keeps its old meaning, including the
first color's alpha.

## Distributions

The new `distribution` type describes a gaussian, triangular or beta
distribution; `lower to upper with d` draws from it restricted to the range
(see [distributions](random.md#distributions)). Constructors and draws use the
format 0.5 instructions `distribution.gaussian`, `distribution.triangular`,
`distribution.beta`, `rng.int.with` and `rng.float.with`, whose numerical
kernels are pinned in binary64 so native and WASM agree bit for bit. A draw
costs 1 fuel plus the iterations of those kernels.

`distribution`, `with`, `gaussian`, `triangular` and `beta` stay available as
names. A local or helper called `distribution` or `with` keeps its meaning,
and a helper called `gaussian`, `triangular` or `beta` takes precedence over
the constructor of that name, unlike other builtins.

`index()`, `range()`, `roll()` and `choose()` keep their algorithms. The
integer sampler of `to` may take more than one stream step, so replacing
`index(n)` or `range(a, b)` with `to` changes the later draws of a generator;
existing generators are not rewritten.

## Nested parts

An imported asset may now import parts of its own (see
[nested parts](composition.md#nested-parts)). Each import path resolves
against the file that contains it; `ipc` follows the file system, so
`link/..` leaves a symlinked directory the way the OS does. Every alias path
is its own instance with separate inputs, outputs and RNG stream, even when
two branches import the same file, and at most 256 instances may result.
Cycles are rejected with the whole chain. In 0.5 an import inside an
imported file was a compile error, so no accepted 0.5 source is affected by
the nesting itself.

## Part outputs in the caller's coordinates

Recompiling changes the values of part outputs. A parent now reads
`alias.outputs.name` in its own coordinates: the child's D4 transform and
its placement are applied, so the value can place the next part or be
drawn directly. Before, the read value had only the D4 transform and the
parent added the placement itself. Hosts likewise receive every part output
in root asset coordinates, with the transforms of all enclosing calls
applied once. A source that added the placement by hand must drop that
addition. Because placing adds `0.0`, a part output that was `-0.0` is now
`+0.0`, and every NaN coordinate has one encoding.

The compiler records this in the format 0.5 parts section, which also names
each instance by its alias path (`hilt.grip`); the C API reports that name in
the existing part descriptor (`name_present == 1`). A file with parts is
therefore format 0.5 and needs this runtime. Frozen 0.1–0.4 files, which
have no parts section, keep their local output values and unnamed parts.

## Compatibility

Source 0.5 programs remain valid 0.6 programs. Existing generators need no
rewrite; `rgb()`, `rgba()`, `color.mix()` and the other color helpers keep
their results. Only part output values change when recompiled, as described
above.
