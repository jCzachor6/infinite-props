# 0.8 reference for the 0.9 implementation

Captured on 2026-11-02 from `dc67f3355e9ead7b6ab0479f0208cfec3a4237d3`
before runtime/compiler changes. The parent runtime is 0.8.0. New reference
tests and benchmark tools were present as working-tree additions; their exact
identities and dirty paths are recorded rather than represented as a clean
historical build. Existing golden images and archived format fixtures were
left unchanged.

- [operations-macos-arm64.json.gz](operations-macos-arm64.json.gz): the complete
  JSON capture, compressed without timestamps; 141 workloads, 15 samples each,
  16 generations per sample, three warmup samples, seed 7. Includes source,
  parameters, hashes, raw CPU/wall samples, total and isolated operation fuel,
  allocation traffic and peak live C++ bytes. Release / AppleClang 21.0.0,
  arm64, ten logical CPUs. The sandbox denied the CPU model query; this is
  recorded as unavailable. The compiler disables FMA contraction.
- [verification.json](verification.json): successful native/WASM reports,
  expanded gate results, one-bit negative controls, fixture/tool identities
  and repeated benchmark validation.
- [ctest-macos-arm64.txt](ctest-macos-arm64.txt): the complete 72/72 CTest log.

An independent second capture used three measured samples per workload. All
141 cases reproduced their total/isolated fuel, allocation counts, requested
bytes, peak/live bytes and image hashes. CPU timings vary; they are not pass
thresholds. The per-generation CPU medians are positive, including cheap cases.
See [measurement/reproduction details](../../operations/README.md) before
comparing these data with another build.

An important baseline finding: all existing scalar noise functions, `fault`,
`warp`, and `color.mix/shade/hsv` cost **one fuel per native call** in 0.8,
including fBm/ridge with eight octaves. CPU work differs substantially, but
those prices cannot be discounted below the agreed minimum one. The three-stop
`ramp` workload costs 25 fuel per executed compiler lowering after removing
the caller; it has no dedicated VM opcode. These measurements are evidence
for stage 13, not changes to the current tariff.

## Frozen compatibility gate

[compat-v0.8.json](../../../tests/fixtures/compat-v0.8.json) contains two
unversioned 0.8 binaries with their SHA-256 and expected results. The runner
executes these exact bytes, then separately recompiles source with all eight
optimization/debug/compression configurations. References are read-only;
the runner has no regeneration mode.

- 78 fixed binary32 noise/fault/warp vectors cover seeds 0/7/2070/−1/INT_MIN,
  signed zero, negative/fractional coordinates, octave clamps and warp amounts.
  Scalar and vec2 calls execute with a runtime seed uniform to defeat folding;
  scalar and vec2 asset constants exercise folding separately. Disassembly
  confirms that runtime calls remain and constant calls disappear.
- Three actual Worley ties freeze F1 bits and the `(cy,cx)` order using an
  independent transcription of the published hash/jitter/distance formulas.
  0.8 exposes only distance; winner ownership becomes observable through the
  stage-02 `cell_id` and must reuse these witnesses.
- Domain cases preserve raw NaN/Inf input encodings, representable lattice
  boundaries, octave overflow, jitter rounding/rejections, finite warp overflow,
  zero-amount offset validation and warp-amount error precedence. Legacy
  `noise`/`fault` are tested within their defined finite lattice domain; their
  unchecked invalid-input conversions are not frozen as portable behavior.
- The three-level [composition](../../../tests/fixtures/compat-v0.8/composition.ips)
  covers all eight D4 mappings, fractional origins/placement, nonuniform output
  scaling, nested soft clipping, legacy/source-over compositing, RNG stream
  isolation and global point/rect/circle output propagation. It runs with
  three root seeds and two sizes (48 invocations per binary/configuration).
- Pixels use SHA-256 of all raw RGBA bytes, and output coordinates use exact
  u32 encodings. Test-only mutations of one output bit and one RGBA bit were
  both rejected by the actual native/WASM gate.
- [ID pins](../../../tests/compat_v08_ids_test.cpp) freeze all 193 existing
  opcode numbers/mnemonics, the header constants, 11 section IDs, 15 type kinds,
  three input IDs and three output IDs. The language gate retains 126 builtin
  overload contracts while allowing additional overloads. This compiler has
  no separate serialized numeric builtin-ID table.

The expanded gate passed **480 reference generations, 64 runtime errors and
16 constant diagnostics**, natively and in freshly rebuilt WASM. Its native
side is a CTest test; its WASM side runs in the existing conformance job.
The complete native/WASM suites include this expanded gate and passed.
Ten Node comparator tests and all 72 native CTest entries passed. Existing
archived versioned envelopes remain expected rejections, while the two new
unversioned binaries execute unchanged. Their fixture checksums are retained
in the verification report.

Fuel, exhaustion timing and future native-work limits are deliberately absent
from the bit reference. The new runtime tariff applies to older programs too;
the compatibility requirement is their numeric/image/RNG/output behavior at
sufficient resource budgets. Performance and bit conformance remain separate
checks. This evidence is local macOS/Node/WASM validation.
