# Stage 12 work calibration

`capture.py` measures 141 unchanged legacy operation sources, the 21 shared
interruption pipelines at 64² and 256², and five deliberately costly cases.
It compiles without optimization/compression, retains programs and source
hashes, then runs three warmups and five recorded plain/probed generations.
No timing threshold determines correctness.

```sh
cmake --build build --target iprop_execution_benchmark --parallel
python3 benchmarks/execution-limits/capture.py --build build --output /tmp/work-profile
node benchmarks/execution-limits/wasm.js build/wasm /tmp/work-profile/native.json /tmp/work-profile/combined.json
```

Run without other builds/tests. Native timing covers the public C generation
entry through result packing, excluding load/validation, later queries, hashing
and result destruction. A separate non-cancelling callback measures wall-clock
gaps between checkpoints. WASM uses the public wrapper and a boolean cancellation
getter; its whole-call time also includes load/validation and JS delivery.
Its between-poll measure excludes the getter's preflight read and the final
host delivery. Both record the tail after the final poll separately.

The WASM run requires identical work/fuel/RGBA hashes or error reasons for
every native observation. Instrumented times include callback/getter overhead;
wall-clock gaps also include scheduling, allocator and GC effects. An observed
maximum is not a universal latency guarantee.

Legacy/64² sources retain their declared 5M fuel. Marked stress/256² sources
receive 1B test fuel to isolate work; production defaults and tariffs do not
change. The large radius-64 morphology case still hits its conservative fuel
reservation first; this is a recorded 3001 outcome. The off-canvas Worley scan
and many cheap helper calls instead hit independent work error 3015.

The retained [calibration summary](../../spec/verification/v0.9.json)
and compressed raw observations document the selected 100M default, 1024
logical steps per polling interval and 32 KiB bulk initialization/copy chunks.
