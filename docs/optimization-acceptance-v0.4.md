# v0.4 compiler optimization acceptance

Verified on 2026-10-02 after C.1–C.4, against the reference lowering retained
by `ipc --no-opt`. The corpus contains 56 tracked `.ips` sources, including
parts, the 24 recipes and all v0.4 language fixtures.

| Artifact mode | Reference total | Optimized total | Reduction |
| --- | ---: | ---: | ---: |
| With debug | 151,484 bytes | 138,155 bytes | 8.80% |
| Stripped | 84,544 bytes | 77,939 bytes | 7.81% |

Each artifact fits its own reference size in both modes. Five seeds per mode
produce 530 pixel-identical image/bit-identical structured-output comparisons
and 30 matching runtime failures. Fuel cost and relocated byte offsets are
excluded from semantic equality. This check is registered in CTest when
Python is available and can be rerun directly:

```sh
python3 scripts/check-optimizations.py build/tools --report /tmp/optimizations.json
```

The pre-optimization v0.4 corpus was also rendered separately; all 56 sources
retain its images, raw outputs and trap reasons. The frozen v0.1–v0.3 binary,
image and specification pins remain unchanged. CTest passes all eight suites,
including C, SFML, Godot and editor checks. Native/WASM checks
pass seven frozen entries, 276 rendered images/structured outputs, 19 runtime
traps, 6,292 vector boundary results and 29 RGBA boundary results.

The core suite passes 337,900 assertions in 425 cases with AddressSanitizer,
UndefinedBehaviorSanitizer and float-cast-overflow checks. macOS does not
support LeakSanitizer in this environment, so leak detection was disabled.
A separate 65,536-element color palette reaches the u16 pool limit: optimized
and reference artifacts remain 2,621,628 bytes each and render the same highest
variant. This verifies fallback when a color fold would need another pool
entry.

The figures describe compiler IR optimization without container compression.
The checker selects unpacked output when that option is available; format
compression is verified separately.
