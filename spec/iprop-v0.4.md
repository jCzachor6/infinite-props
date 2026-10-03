# `.iprop` Format and Virtual Machine — Specification v0.4

**Status:** v0.4 development contract (2026-10-02).
**Scope:** Lossless container compression. The v0.3 section, type, opcode,
mathematical, canvas, output, fuel and validation semantics remain frozen.
This document inherits [v0.3](iprop-v0.3.md), with the changes below taking
precedence. The `.ips` language and compiler optimizations are separate.

## 1. Versions and header flags

An interpreter supporting 0.4 MUST continue to accept uncompressed files
with versions 0.1–0.3 under their existing validation rules. A plain 0.4
file has the same 20-byte header and section layouts as 0.3.

Header flag bit 0 retains its debug meaning. Bit 1 (`0x00000002`) means the
file is a compressed container; it MUST appear only with major 0, minor 4.
All other flag bits MUST be zero. Reserved header bytes remain zero, and
the section count remains in 1–255.

## 2. Compressed envelope

For a compressed container, bytes after the 20-byte header are:

| Offset | Size | Field |
| --- | --- | --- |
| 20 | 4 | uncompressed file size, little-endian u32 |
| 24 | remaining bytes | one RFC 1950 zlib stream carrying RFC 1951 DEFLATE |

The stream contains the **complete original uncompressed `.iprop` file**,
including its header and section table. Thus decompression is byte-exact,
including its original minor version, flags, padding and debug data. The
inner file MUST have a supported uncompressed version (0.1–0.4); compressed
inner files and preset zlib dictionaries are forbidden.

The envelope's debug bit and section count MUST match the inner header.
The inner file MUST pass the ordinary layout, section and static validation
rules before execution. No section offsets in the inner file are relocated.
There are no VM changes and no new opcodes.

The zlib checksum MUST be checked. Truncated, invalid, concatenated or
trailing streams MUST be rejected. Decompressed byte count MUST equal the
announced count, and the stream MUST consume every byte after offset 24.

## 3. Resource bounds and loading

Compressed container size and announced uncompressed size MUST each be at
most 64 MiB (67,108,864 bytes). The uncompressed count MUST be large enough
for a 20-byte header and the announced section table. These checks MUST run
before allocating the decompression destination. The decoder MUST bound its
output by the announced size and reject shorter or longer results.

The existing section/resource limits apply again to the decoded file.
Uncompressed historical files retain their existing limits. The loader owns
the decoded byte buffer for the lifetime of all code/debug views, including
after moves. Decoding completes before any VM execution.

Low-level section/layout APIs operate on an expanded file. A raw compressed
container MUST be expanded before using section offsets. Encoding failures
report offsets in the envelope; validation and runtime offsets refer to the
expanded original file. Hosts may inspect both the container and inner header.

## 4. Encoding and tooling

A compressor MUST preserve the original bytes and MUST NOT silently change
an inner file's version or semantics. It MAY keep a file uncompressed when
compression is not smaller or the compression resource limit would be
exceeded. Reference tooling uses deterministic level-9 zlib encoding and
emits the envelope only when its complete size is strictly smaller.
Different compliant encoders may produce different streams for the same
original bytes; all MUST decode to that same original file.

Source compilation can use automatic compression with an explicit unpacked
mode for inspection. Debug stripping happens before compression. Stripping
an existing compressed file expands it, removes only its debug data, and
may recompress the result. A compressed file without debug may be returned
unchanged. Execution of stripped/packed and original files MUST have the
same pixels and structured output bits.
