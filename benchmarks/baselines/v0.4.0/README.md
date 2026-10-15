# Released 0.4 baseline

This dataset belongs to `v0.4.0`, source commit
`66d34c2831585afb231022da9296528a912e962e`. The [manifest](manifest.json) records
released source/document/fixture hashes, compiler diagnostics, binary identities
and sizes, PNG/RGBA hashes, raw f32 output bits, schemas, input defaults, traps
and fuel. `rng-state.ipa` and recorded `states_u32` preserve stream observations.
The legacy C client/library records include platform layouts and artifact hashes.

These observations remain unchanged when the current compiler accepts a source
previously rejected. The current reader rejects older binary formats and does
not support substituting its ABI 0.5 library into the archived ABI 0.4 client.

## Capture or verify

Reproduce a capture only in an unmodified released checkout with tools built
from that release:

```sh
cmake -S . -B build-baseline -DCMAKE_BUILD_TYPE=Release -DIPROP_SFML=OFF -DIPROP_GODOT=OFF
cmake --build build-baseline --parallel
python3 scripts/capture_v04_baseline.py --tools build-baseline/tools --work-dir build/baselines/v0.4.0
```

For a previously preserved artifact directory, verify without recapturing:

```sh
python3 scripts/capture_v04_baseline.py --verify --work-dir /path/to/preserved-baseline
```

The tool checks released Git blobs and source/tool identity, and retains binaries,
images, output JSON, original tools and the legacy executable/library. Artifact
paths in the manifest are relative to the capture directory; environment details
belong to local `observations.json`. The checked-in manifest alone is not the
full executable capture. Its scope and platform do not imply coverage of all
seeds, inputs or targets.
