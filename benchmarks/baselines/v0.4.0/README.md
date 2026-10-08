# Released 0.4.0 baseline

This immutable observation belongs to release `v0.4.0`, source commit
`66d34c2831585afb231022da9296528a912e962e`. It is not the expected result of
the improved 0.5 compiler. Historical rejections stay here when the live
language example manifest is updated to accept the repaired constructs.

`manifest.json` records 476 released source/document/fixture blobs and their
SHA-256 hashes, **85 released `.ips` entry files**, their actual compile
diagnostics, debug/stripped reference/optimized binary sizes and hashes,
and generation observations for seeds `0, 1, 2, 7, 26` with asset-default
size, variant `0` and archetype `0`. Compilation disables compression so
artifact sizes describe compiler output rather than compression heuristics.
The measured scope is explicit: this capture does not exhaust every variant,
archetype, uniform override or possible input program.

The capture observed **12 compile rejections**, **670 successful exact
reference/optimized render pairs** and **60 matching trap pairs**. It stores
exact stderr (including source locations), PNG and decoded RGBA hashes,
raw IEEE-754 binary32 output hashes, output schemas/counts, input defaults,
and normative fuel spent by each implementation. Optimized/reference trap
comparison ignores relocated byte offsets; the original offsets remain in
the individual records. It never requires identical fuel costs after a
source optimization. The eight committed golden PNGs also have raw RGBA
hashes. Existing CTest/native/WASM conformance continues to verify the wider
semantic contract, including vectors, transformed outputs and old formats.

`rng-state.ipa` records the initial stream, the state after one RNG draw,
and the state restored after a nested stream. Splitting a `u32` into exact
16-bit halves avoids losing bits during conversion to `f32`. Both the raw
output bits and reconstructed `states_u32` are retained for all five seeds.

The frozen `backends/capi/test/legacy/v0.2/iprop.h` is byte-identical to the
released header. Its standalone **C99 client links a shared library**, uses
all six old symbols, verifies unknown uniforms, first-wins duplicates,
wrong-type traps, argument errors, result ownership after program free and
root/part slot ownership. The manifest pins the executable and library
hashes, platform, structure sizes and the observed report. OOM is explicitly
unobserved here; it requires controlled allocation-failure injection rather
than exhausting the machine's memory.

To reproduce the capture, use an unmodified released checkout and tools
freshly built from that source:

```sh
cmake -S . -B build-baseline -DCMAKE_BUILD_TYPE=Release -DIPROP_SFML=OFF -DIPROP_GODOT=OFF
cmake --build build-baseline --parallel
python3 scripts/capture_v04_baseline.py --tools build-baseline/tools \
  --work-dir build/baselines/v0.4.0
```

The script validates all released Git blobs against the working files,
checks the compiler's product/format identity and detects source/tool changes
during capture. It preserves the original tools, expanded binaries, PNGs,
raw output JSON, the frozen shared library and old client executable locally.
All manifest artifact paths are relative to `--work-dir`; elapsed time,
absolute machine paths and environment details go to the local
`observations.json`, never the committed manifest. Executable hashes and
layout observations are specific to the target recorded in the manifest.

For this first capture the preserved artifact directory is
`/Users/jakubczachor/.codex/scratch/iprop-v0.4-baseline-20261005`.
Verify it without recapturing historical behavior:

```sh
python3 scripts/capture_v04_baseline.py --verify \
  --work-dir /Users/jakubczachor/.codex/scratch/iprop-v0.4-baseline-20261005
```

For the later binary compatibility gate, run that **original executable**
with the new shared library through the platform loader's library search
override. Do not rebuild it with the new header or replace the archived
release library. On macOS, for example:

```sh
DYLD_LIBRARY_PATH="$PWD/build/backends/capi" \
  /Users/jakubczachor/.codex/scratch/iprop-v0.4-baseline-20261005/legacy-client/iprop_legacy_v02 \
  /Users/jakubczachor/.codex/scratch/iprop-v0.4-baseline-20261005/legacy-fixtures/uniforms.iprop \
  /Users/jakubczachor/.codex/scratch/iprop-v0.4-baseline-20261005/legacy-fixtures/output-owners.iprop
```

On Linux use `LD_LIBRARY_PATH`; Windows requires the replacement DLL in
the executable's loader search path and its import library only when building
the original client. This platform's successful baseline run proves the
recorded target only; 0.5 CI must separately test its supported targets.
No external LLM measurement has been collected by this task.
