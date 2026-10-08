# `.iprop` Format and Virtual Machine — Specification v0.5

**Status:** Draft v0.5 contract (2026-10-08), frozen when product 0.6.0 is
released.
**Scope:** Colour channel reads and uniform random ranges. The v0.4 section,
type, container, mathematical, canvas, output, fuel and validation semantics
remain frozen.
This document inherits [v0.4](iprop-v0.4.md), with the changes below taking
precedence. The `.ips` language and compiler optimizations are separate.

## 1. Versions and header flags

An interpreter supporting 0.5 MUST continue to accept files with versions
0.1–0.4 under their existing validation and execution rules. A plain 0.5
file has the same 20-byte header and section layouts as 0.4.

The reference compiler and assembler write the oldest minor that holds every
instruction of the file, and never older than 0.4: a file without a 0.5
instruction remains a 0.4 file, readable by 0.4 interpreters.

The compressed envelope of v0.4 §2 is unchanged and remains a 0.4
container: header flag bit 1 MUST appear only with major 0, minor 4, so a
0.5 header with bit 1 set is invalid. The inner file of an envelope MAY now
have any supported uncompressed version, 0.1–0.5. A 0.4 interpreter rejects
such an inner 0.5 file by its version, after expansion.

## 2. Colour channel reads

| Op | Mnemonic | Stack | Semantics |
| -- | -------- | ----- | --------- |
| 0xA4 | color.r | `[c] -> [i32]` | the R byte, zero-extended |
| 0xA5 | color.g | `[c] -> [i32]` | the G byte, zero-extended |
| 0xA6 | color.b | `[c] -> [i32]` | the B byte, zero-extended |
| 0xA7 | color.a | `[c] -> [i32]` | the A byte, zero-extended |

Each instruction pops one colour and pushes the stored channel as an `i32`
in 0–255. Alpha is read exactly as stored; no channel is premultiplied,
normalized or otherwise converted. The instructions never trap, do not read
or advance the RNG stream, and do not touch the canvas or outputs. Each
costs the ordinary 1 fuel unit of v0.3 §8.3.

The `aux` byte MUST be zero; the operand is unused, as for the other
operand-less instructions. The 0xA4–0xA7 opcodes MUST appear only in files
with minor version 5 or later, including in unreachable code; an older file
containing one is invalid.

## 3. Uniform random ranges

| Op | Mnemonic | Stack | Semantics |
| -- | -------- | ----- | --------- |
| 0xA8 | rng.int | `[i32 i32] -> [i32]` | uniform integer in `[lo, hi]`, both ends included (§3.1) |
| 0xA9 | rng.float | `[f32 f32] -> [f32]` | uniform value in `[lo, hi]` (§3.2) |

`lo` is the deeper operand and `hi` the top. Both instructions advance the
current stream (v0.3 §8.2) with the step of v0.3 §12.2, `state = state +
0x9e3779b9` (wraparound), and use `hash(state)` (v0.3 §12.1) as the draw of
that step. They do not touch the canvas or outputs. The `aux` byte MUST be
zero; the operand is unused. The 0xA8–0xA9 opcodes MUST appear only in
files with minor version 5 or later. `rng.range` and `rng.index` keep their
v0.3 algorithms.

Bounds are checked before any step. `lo > hi` traps with a numeric-range
error; for `rng.float`, a NaN or infinite bound traps the same way. Equal
bounds are valid: the result is that bound, and the instruction still
advances the stream.

### 3.1 rng.int

All arithmetic below is exact (at least 64-bit); the result wraps to `i32`.

```
width    = hi - lo + 1                       // 1 .. 2^32
accepted = floor(2^32 / width) * width
repeat for draw = 1 .. 32:
    state = state + 0x9e3779b9
    x = hash(state)
    if x < accepted or draw == 32:
        return lo + (x mod width)
```

Draws at or past `accepted` would favour the smallest results, so they are
rejected and the next step is drawn. For a width of 2^32 or any power of two
up to it, the first draw is always accepted. The instruction costs 1 fuel
unit, plus 1 unit for each draw after the first, charged before that draw. If
the budget runs out, generation traps with the ordinary fuel error.

The 32nd draw is accepted unconditionally, which bounds the loop. With this
generator the bound is never reached: every rejection set lies within
`[2^31 + 1, 2^32)`, the set of width 2^31 + 1, and over the full cycle of
2^32 stream states the longest run of draws in that set is 30. An instruction
therefore takes at most 31 draws, and no draw is ever forced.

### 3.2 rng.float

```
state = state + 0x9e3779b9
u = (f32)(hash(state) >> 8) / 16777216.0f    // unit(), in [0, 1)
r = lo * (1 - u) + hi * u                    // binary32 operations, no fused multiply-add
if r < lo: r = lo
if r > hi: r = hi
return r
```

`1 - u` is exact. Rounding may move `r` past a bound (or, for bounds near the
binary32 maximum, to infinity); the comparisons return the bound instead, so
the result always lies in `[lo, hi]`, and equal bounds return that bound. The
instruction costs 1 fuel unit and advances the stream once.

## 4. Conformance

Every 0.1–0.4 file MUST produce the same pixels, structured output bits,
RNG state, traps and fuel use as under v0.4. Backends MUST return the four
channel bytes of every colour exactly, including alpha 0 and 255, and MUST
reproduce the range results, stream states and fuel use of §3 bit for bit.
