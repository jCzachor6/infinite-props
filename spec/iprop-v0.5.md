# `.iprop` Format and Virtual Machine — Specification v0.5

**Status:** Draft v0.5 contract (2026-10-08), frozen when product 0.6.0 is
released.
**Scope:** Colour channel reads. The v0.4 section, type, container,
mathematical, canvas, output, fuel and validation semantics remain frozen.
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

## 3. Conformance

Every 0.1–0.4 file MUST produce the same pixels, structured output bits,
RNG state, traps and fuel use as under v0.4. Backends MUST return the four
channel bytes of every colour exactly, including alpha 0 and 255.
