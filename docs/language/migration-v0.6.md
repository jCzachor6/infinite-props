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

The compiler writes the oldest format that holds a file: 0.5 only when the
compiled code uses a 0.5 instruction, otherwise 0.4 as before. A runtime from
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

Source 0.5 programs remain valid 0.6 programs. Existing generators need no
rewrite; `rgb()`, `rgba()`, `color.mix()` and the other color helpers keep
their results.
