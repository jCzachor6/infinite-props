# Independent work limits and interruption (0.9, stage 12)

Status: the VM, checked C API, CLI and public WASM implementation are verified.
The remaining host bindings, calibration and final stage audit are in progress.
Fuel prices and the default 5,000,000 fuel have not changed.

## Independent deterministic work

Every generation has one `WorkBudget`: setup, helpers, nested static parts,
new geometry, dynamic parts and final C result packing share it. Neither a
new builtin call nor returning from a helper resets/refunds work. Zero in a
host request selects the finite default, currently **100,000,000** work units.
An explicit positive u64 replaces it; even `UINT64_MAX` is finite. The default
and poll interval will receive their measurement justification in step 12.3.
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
or bytes. Geometry storage polls during relocation/construction, including
prepaid loops. Fixed bounded scalar kernels and fixed automatic workspaces
remain bounded calls; allocation/system scheduling and a host's own callback
cannot have a portable wall-clock bound. Measurement of the slowest native
and WASM chunks and the final default rationale remain step 12.3.

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
