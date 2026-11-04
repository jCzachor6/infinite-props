# Independent work limits and interruption (0.9, stage 12)

Status: the VM, checked C API, CLI and all seven hosts expose the shared
controls. Calibration and the completed local audit are recorded below and in
[the verification report](verification/execution-limits-v0.9.json).
Fuel prices and the default 5,000,000 fuel have not changed.

## Independent deterministic work

Every generation has one `WorkBudget`: setup, helpers, nested static parts,
new geometry, dynamic parts and final C result packing share it. Neither a
new builtin call nor returning from a helper resets/refunds work. Zero in a
host request selects the finite default, currently **100,000,000** work units.
An explicit positive u64 replaces it; even `UINT64_MAX` is finite. The default
and poll interval are justified by the stage-12 calibration below.
There is no new file field, instruction, result persistence format or tariff
version. Legacy generation entries also use the finite default.

Work is an integer count of bounded logical steps, independent of fuel. Its
subtraction checks the entire requested reservation before subtracting, so an
overflow cannot wrap into available work. Successful counts do not depend on
the clock, cancellation source, addresses, STL growth policy or RNG state.
The tariff is a property of this runtime, rather than a portable CPU clock.

The implementation counts:

- Every VM dispatch/implicit return, setup local/input/output traversal and
  control-table scan.
- Four corners for value/gradient noise, three for fault, one per actual fBm
  iteration plus its gradient corners; Worley has 25 candidates and five row
  steps. Legacy fBm/ridge with eight octaves therefore do 40 kernel units;
  legacy warp's three four-octave fBm calls do 60. An amount-zero warp vector
  does no sampling. Configured fBm with gain zero samples only one octave,
  while retaining its existing declared-octave fuel cost.
- Raster rows, pixels, polygon edges, morphology samples, blur taps/channels,
  clipping, painting and texture/part scans. A curve/distance query also
  reserves 25 bounded Newton evaluations (five candidates of at most five).
- Actual contour/Voronoi/offset/raster/bevel iterations, sorting/sifting,
  topology validation and region/part metadata traversals.
- Distribution iterations and point scans, list comparisons and referenced
  distribution-point comparisons; sampler/stepper batch elements.
- Buffer initialization/copy slots, UTF-8 name bytes, geometry capacities
  relocated during explicitly doubled growth and result records/float leaves.
  Geometry allocation construction polls but does not bill STL-dependent
  constructor/move counts. Allocation capacity itself remains a memory charge.

Loop overhead is explicit: for example a raster row and its pixel visits are
separate steps. Fuel's existing upfront conservative reservations do not bill
work; the actual loops/initialization do. Changing fuel discounts must never
remove or scale work accounting/checkpoints.

## Deadline, cancellation and bounded checkpoints

`GenerationInputs.limits` accepts a monotonic `steady_clock` deadline, a shared
one-way atomic `CancellationToken` and an optional bounded synchronous host
poll. A deadline and cancellation only terminate execution. They never alter
random draws, arithmetic, successful pixels, metadata, fuel or work totals.
No clock is read when a deadline is absent. The work guard is always active.

`kWorkPollInterval` is **1024**. Long algorithms check before their next bounded
step; reservations can advance the counter before physical work, so a jump
in `work_used` between polls does not measure uninterruptible work. Large
surface initialization/copies and strings are split into at most 1024 slots
or bytes and at most **32 KiB** in each bulk initialization/copy chunk.
Growing control tables also move elements with checkpoints. Geometry storage polls during relocation/construction, including
prepaid loops. Fixed bounded scalar kernels and fixed automatic workspaces
remain bounded calls. The largest fixed Voronoi workspace is statically capped
at **32 KiB**; lists/distributions have at most 256 elements, call depth 256,
locals 1024 and operand depth 512. A logical loop step may contain bounded
scalar math (including at most 25 curve Newton evaluations); 1024 is a count
of bounded steps, not machine instructions. Allocation, system scheduling,
cleanup after interruption and a host's callback cannot have a portable
wall-clock bound. Host rendering/encoding and later standalone queries have
their separate scope described below.

Generation errors use `WorkExhausted` (3015), `Cancelled` (3016) and
`DeadlineExceeded` (3017), all Execute-stage traps, including setup. There is
no fabricated instruction location during setup or final C packing. A failed
VM rejects result access and releases unpublished arena resources. The C API
keeps the caller's output pointer unchanged. Completed owned results survive
later failed generations. A cancellation racing after the final successful
poll can observe a completed result.

Loading/validation, subsequent standalone result queries and host
rendering/encoding are outside the generation deadline. VM result copying
and C packing performed to deliver generation are checked before publication.

## Additive C API 0.6, existing ABI 0.5

`IPROP_CAP_EXECUTION_LIMITS` advertises the new feature. Negotiate API >= 0.6
and this capability before resolving its symbols. The old structs, enum tags,
entry points, ABI/install identity and product version are unchanged.

- `iprop_generate_controlled` takes the ordinary options/parameters plus an
  optional separate sized `iprop_execution_limits_ex` descriptor.
- `work_limit=0` selects the finite default; `timeout_ns=0` disables the clock.
  Timeout is relative to the beginning of generation, uses monotonic time and
  saturates at the representable clock maximum without overflowing.
- A shared `iprop_cancellation` is created once, cancelled from another thread
  and freed after calls using the handle finish. Execution retains its atomic
  flag. Cancellation is one-way; create a new token for a new operation.
- An optional `iprop_cancel_poll` observes host-owned cancellation/shared
  memory. The callback/context are borrowed until the call returns, must be
  bounded, must not throw and must not reenter generation.
- `iprop_result_query_work` copies the completed native generation's budget,
  remaining and used counts through a separate sized `iprop_work_stats_ex`.
  The old fuel statistics struct retains every byte/offset and meaning.
- Both descriptors retain the normal four-byte minimum, complete-field reads,
  unknown-tail canaries, zero flags and transactional checked error rules.

The CLI exposes `--work-limit`, `--timeout-ms` and opt-in `--work-stats`.
Without `--work-stats`, its existing successful JSON shape is preserved.

## Synchronous WASM

Both `Module.generate` and `Iprop.generate` accept `workLimit`, `timeoutMs`,
`signal` and `cancelFlag` (the low-level call's sixth execution argument).
Counters are nonnegative JS safe integers; timeout accepts finite fractional
milliseconds representable in safe integer nanoseconds. Zero work selects the
finite default; zero timeout disables the clock. Results include native
`work: { budget, remaining, used }` alongside unchanged fuel statistics.

An already aborted signal works synchronously. An event on the same blocked
JS event loop cannot interrupt WASM. For active cross-worker interruption,
use an `Int32Array` in `SharedArrayBuffer` and `Atomics.store(flag,0,1)` from
another worker. Native checkpoints load it while the builtin is running.
Platforms without shared memory retain work limits and monotonic deadlines.

JS cancellation reads catch exceptions and convert them to native cancellation;
no JS exception escapes through live C++ frames. The normal owned Error is
thrown only after C++/C cleanup returns to `module_api.js`.

## Current evidence

`tests/execution_limits_test.cpp` covers exact work boundaries, original
fuel/pixels/RNG, 21 internal opcode interruption paths, cumulative cheap scalar
calls through helpers, overflow, atomic tokens, monotonic deadlines, large
copies and arena cleanup. Scalar work counts are checked separately from fuel.

The C99 shared/static test checks exact delivery budgets, descriptor prefixes
and canaries, timeout, cancellation and recovery after 2048 failures.
`execution-limits-conformance.js` compares all 21 native C/WASM work/fuel/image
observations, 42 exact/one-short boundaries, 21 recoveries and 2048 throwing
cancellation sources. Its worker test proves interruption inside `noise.mask`,
with an executing PC, rather than at the next opcode or after a JS timeout.
Existing memory/geometry-only stress tests explicitly request enough work to
reach the boundary they test; their original expected errors remain strict.

## Managed hosts

Python exposes keyword `work_limit`, `timeout_ns`, `cancellation` and a shared
`CancellationToken` (`program.cancellation_token()` selects the same library).
The generation argument retains its automatically finalized native token.
Swift takes an `ExecutionLimits` value and retains its atomic token with
`withExtendedLifetime`. C# takes immutable `ExecutionLimits` and connects the
standard .NET cancellation registration to a native SafeHandle; disposing the
registration precedes native free. Kotlin/JNI takes `ExecutionLimits` and uses
short token leases; close defers native free until active JNI calls finish.
Cancellation never takes a Program's generation lock.

All four negotiate API >=0.6 / ABI 0.5 and all three capabilities (parts,
resources, execution controls). Their native work statistics are separate from
fuel. Real wrapper tests compare all 23 complete typed scenes at exact work
and one-short budgets, exercise timeout and recovery, and cancel a running
large scan from another thread (with an executing PC). Kotlin also closes the
token concurrently to verify its lease.

The frozen 21,829-image corpus is unchanged. Its pixel replayers explicitly
supply 1,000,000,000 work units to reach historical high-fuel stress cases;
new default-limit and exact-budget tests are separate. This preserves the
pixel oracle and does not raise any production default or change fuel.

SFML takes a final optional `ExecutionLimits` value, retaining a shared native
token by value; `GeneratedResult::work` keeps counters and `Failure` owns the
structured cause. Godot takes a final optional dictionary with `work_limit`,
`timeout_ns`, `cancellation`; its signed integer API accepts 0..INT64_MAX.
A local `Ref<IpropCancellation>` holds the token across the call. Its `work`
and result tables publish together; failed generations preserve them and set
`last_error`. In each host cancellation only touches the independent native
atomic flag. Other mutable engine/asset operations retain their ordinary
thread restrictions.

## Measured default and chunks

The [calibration](verification/execution-limits-v0.9-calibration.json) records
188 cases, three warmups and five recorded plain/probed samples on native
AppleClang and Emscripten/Node, using the same binaries, work/fuel counters
and RGBA hashes (or identical trap reasons). All 141 legacy cases and all 42
64²/256² pipeline cases succeed with the default. Their respective maximum
work counts are **4,612,363** and **6,217,470**. The 100M finite default leaves
about **16 times** the measured pipeline maximum, retaining room for larger
compositions without allowing unlimited work after a fuel discount.

The large 2048² image and 256-site/four-relaxation Voronoi controls use
4,194,305 and 5,124,829 work units. Two stress cases with explicitly increased
test fuel are stopped by 3015: an off-canvas 2048² Worley mask and four million
small helper calls. Their measured whole-call medians are 404/742 ms native
and 678/3912 ms WASM. This default is a finite cost guard; interactive hosts
should select a monotonic timeout for their latency requirement. The radius-64
morphology control hits its unchanged conservative fuel reservation (3001).

The 1024-step interval balances polling overhead with interruption inside
long builtins. Bulk helpers additionally cap each physical copy at 32 KiB,
independent of record width; tests interrupt both large-record copying and
control-buffer growth before completion. Observed maximum between-poll gaps
are **4.245 ms native** and **0.967 ms WASM** in this capture, including
instrumentation and scheduling effects. These are measurements, not portable
time guarantees. Final wrapper delivery is recorded separately (maximum
observed tail 0.605 ms native / 1.524 ms WASM); it is outside the next-checkpoint
claim. Deadlines/cancellation remain optional and never replace deterministic
work accounting.

## Verification closure

Linux passes 100/100 CTest including the two GL tests checked there; macOS
passes the remaining 98/98. Full native/WASM conformance, all seven public
hosts, 23 complete typed binding scenes and the relocated SDK pass. Six native
host paths replay the unchanged 21,829-image corpus; WASM passes its complete
native/reference suite and the 188-case calibration comparison.

ASan/UBSan/LSan covers 93/93 cases through the full run plus a Python harness
closure. CPython 3.12's shutdown allocations are independently reproducible
without this library. The harness separates malloc buffer checks from pymalloc
leak checks, retains allocator-only interpreter suppressions and requires named
native overflow/leak probes to remain detectable. Both Python modes pass all
10 tests. The original failed harness log is retained alongside the closure.
Platform limits and exact source/log hashes are in the report; this is local
stage-12 verification, with the fuel tariff unchanged.
