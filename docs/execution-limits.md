# Execution controls

Fuel prices follow the [runtime tariff](../spec/fuel-v0.9.md). Default fuel is
25,000,000 from program metadata. An independent default 100,000,000-unit work
budget also applies to old entry points. Zero work selects that finite default;
zero timeout disables the optional monotonic deadline. A cancellation token is
one-way and cooperative, not a reusable interrupt or a wall-clock guarantee.

Checks occur at most 1024 logical work steps apart and bulk copies use at most
32 KiB per chunk. Allocation, scheduling and cleanup are outside a hard latency
guarantee. Work/fuel and successful image/output/RNG results are deterministic;
deadlines and cancellation can instead fail a generation. No partial result is
published. Retained previous results remain valid. Reasons are 3015 (work),
3016 (cancelled), 3017 (deadline).

| Host | Entry and details |
| --- | --- |
| C/C++ | API 0.6 `iprop_generate_controlled`, sized execution/work descriptors and shared cancellation handle; [ABI policy](c-api-abi.md). |
| HTML5/WASM | Generation options and optional shared worker cancellation; [binding guide](../backends/html5/README.md). |
| Python | `generate(work_limit=0, timeout_ns=0, cancellation=None)`; [guide](../backends/python/README.md). |
| Swift | `generate(..., limits: ExecutionLimits(...))`; [guide](../backends/apple/README.md). |
| Kotlin/JNI | `generate(..., limits = ExecutionLimits(...))`; [guide](../backends/android/README.md). |
| Unity/C# | `Generate(..., limits: new ExecutionLimits(...))`; [guide](../backends/unity/README.md). |
| Godot | Optional `limits` dictionary; [guide](../backends/godot/README.md). |
| SFML | `ExecutionLimits` and shared native cancellation; [guide](../backends/sfml/README.md). |
| CLI | `ipgen --work-limit N --timeout-ms N --work-stats`; work counters are optional JSON metadata. |

All bindings use the same checked C API contracts (0.6 / ABI 0.5), not host-specific
fuel tariffs. The [technical contract](../spec/execution-limits-v0.9.md) defines
exact accounting, ownership and capability negotiation. Verification records
there distinguish executed host tests from unavailable platform environments.
