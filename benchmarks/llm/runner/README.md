# Archived-source benchmark runner

`session.py` prepares an exact request context, imports a caller's recorded
session response, evaluates it with the real CLI, and permits at most one saved
repair response for the tasks that failed. This is the `codex_session` import
adapter: it makes no online model call and cannot observe model identity,
sampler enforcement, inference time, tokens, or billing. Those fields stay null.
The caller must retain the actual session response; authored fixtures use
`--record-kind authored_witness` and are not measurements of a model.

```sh
python3 benchmarks/llm/runner/session.py prepare \
  --output /path/to/new-session --tools /path/to/frozen-tools --version 0.5 \
  --reference /path/to/generated/docs/language-v0.4.md \
  --reference-manifest /path/to/generated/docs/language-manifest.json \
  --language-export /path/to/generated/docs/language.json
python3 benchmarks/llm/runner/session.py import-response \
  --run /path/to/new-session --response /path/to/actual-response.json
python3 benchmarks/llm/runner/session.py import-repair \
  --run /path/to/new-session --response /path/to/actual-repair-response.json
python3 benchmarks/llm/runner/session.py status --run /path/to/new-session
python3 benchmarks/llm/runner/session.py replay \
  --run /path/to/new-session --output /path/to/new-replay
```

The full generated handbook, SDK export, task manifest, protocol and exact
protocol prompt are archived and sealed with SHA256 before response import.
The compact `LLM_REFERENCE.md` is not the comparative pilot reference. Adapter
`--version 0.5` requires the current 0.5 language contract; product version and
binary format are recorded independently (both may still be 0.4). The handbook
filename follows the generated manifest's product version. Compiler source
commit comes from that supplied manifest and is identified as an assertion;
executable paths and actual binary hashes are observed independently. Frozen
`--version 0.4` requires the original released handbook/export bytes and source
commit, using the explicit historical reader profile. It invokes ordinary
compile, with unavailable historical diagnostic codes/ranges remaining null.
Actual model records require a generated manifest with a committed source
identity and clean working-tree flag. Source-archive references without those
claims are accepted only for explicitly labeled authored harness witnesses.

Supplied SDK export bytes enter the prompt verbatim. Raw `ipc
--describe-language` stdout is archived separately. Their entire parsed JSON
objects must agree under canonical serialization with duplicate keys and
nonfinite numbers rejected; different whitespace is allowed, while boolean,
integer and floating representations are distinguished. This preserves the
original frozen SDK serialization and its historical SHA. The task/protocol
bytes and their frozen hashes cannot be overridden with changed definitions.

A prepared directory without a response remains `awaiting_initial_response`,
with no evaluated attempts and null success counts. Import saves the raw bytes
before extraction. Malformed JSON preserves that payload and records a missing
source failure for every selected task, with the full denominator of 24 and
unselected tasks listed explicitly. Responses above the existing 1 MiB ceiling
are refused before acceptance; the caller's original file remains unchanged.
Unsafe/oversized source maps are rejected before compiler execution. Candidate
check JSON must pass the current G.5 reader and agree with the actual exit code;
there is no silent downgrade to legacy diagnostics.

After evaluation, `completion-N.json` seals all extracted sources, exact
process logs, native diagnostics, artifacts and render records. Each task links
to its raw response, exact request/prompt and feedback hashes. First-pass counts
are retained separately from latest results. Actual failed sources and public
failure evidence produce `prompt-1.txt` and `request-1.json` once. A repair may
include only failed task IDs, and produces no second repair request. Success
without repair and exhaustion after one repair have distinct states. Interrupted
evaluation/feedback preparation is explicit and cannot be reused as a sealed
attempt. Existing inputs, responses and completed records are never replaced.

Replay verifies sealed inputs, response/artifact hashes, adapter source and tool
binary identities, then reevaluates the exact raw responses in a fresh output
directory. `--tools` may relocate binaries only when their hashes match. For a
saved repair, the replayed failed IDs and exact feedback/request context must
match before accepting the old repair response; divergent diagnostics cannot
silently rebind that response. Originals remain untouched. Process paths/times
are fresh observations, while pixel/output/fuel results and public predicates
can be compared independently. This adapter is available for later CI replay;
I.4 CI integration and a fresh live 24-task generation remain separate work.

Run integration tests with a current compiler and optionally the actual frozen
tools; the two fixture responses are clearly labeled authored witnesses:

```sh
IPROP_COMPILER=/path/to/current-tools/ipc \
IPROP_FROZEN_TOOLS=/path/to/frozen-0.4-tools \
  python3 benchmarks/llm/runner/test_session.py
```

This first runner executes actual saved model responses against the supplied
frozen tools. It does not contact a model, make a repair request, implement a
human visual verdict, or replace a failed model program with a reference solution.
Run it from the repository or supply explicit `--tasks` and `--protocol` paths.

```sh
python3 benchmarks/llm/runner/run.py \
  --response benchmarks/llm/runs/20261005-codex-pilot/0.4/response.json \
  --tools /path/to/frozen-0.4-tools \
  --version 0.4 \
  --output /path/to/new-immutable-run-directory \
  --prompt benchmarks/llm/runs/20261005-codex-pilot/0.4/prompt.txt \
  --reference /path/to/frozen/language-v0.4.md \
  --language-export /path/to/frozen/language.json
```

The response is the actual JSON `{"tasks":[{"task_id":"asset-01",
"files":{"entry.ips":"..."}},...]}`. Files are extracted exactly as UTF-8;
no whitespace or newline normalization is performed. Duplicate JSON keys,
unsafe paths, missing bundled imports, nested imports, and configured source
size/count ceilings are rejected. An existing output directory is not overwritten.

Each task compiles in its own workspace using the supplied compiler. Frozen
0.4 has no check JSON; candidate 0.5 first executes `--check --diagnostics=json`.
Successful compilation is the evidence for runtime static validation because
`ipc` runs `loadFile`/`validate` before returning its artifact. No separate
`ipval` binary is asserted to exist when the supplied tool directory lacks one.

After successful raw compilation and binary preflight, candidate 0.5 also
archives a canonical entry under `<task>/attempt-N/canonical/`. It runs the real
`--format-only` pipeline, recompiles that entry, requires exact equality of the
stripped production binary, and checks formatter idempotence. The CLI relocates
imports to the existing raw bundled parts; this artifact is a canonical entry,
not a standalone package of canonicalized imports.

`formatting.json` and the task's `formatting` field record this auxiliary verdict,
artifact hashes, exact process logs and separate timing. The three subprocesses
share the existing 15-second compile ceiling; canonical source/binary sizes use
the existing byte ceilings. Formatting failure does not alter raw compile/check
success, render predicates, repair feedback or first-pass counts. Rendering and
source-feature review still use the exact archived model sources and their
original binary. Candidate summaries report formatting counts separately.
Frozen 0.4 invokes no formatter and receives no formatting fields. Task,
protocol, prompt and actual-response bytes/hashes are preserved.

The independent binary preflight checks compressed/plain headers, bounded
decompression, section layout, metadata, root/part dimensions, declared fuel,
function table, input defaults, and the global output declaration/owner table
before running the VM. It does not claim to replace the opcode/type-flow
validator. In API 0.2 the dynamic output element ceiling can only be checked
after execution; declared slots are bounded before execution. Declared fuel
and process time bound the pilot, not total-memory accounting. No binary fuel
header is rewritten and no nonexistent host override is used.

Each fixed render case executes twice, saving exact bounded stdout/stderr,
commands, exit statuses, observed subprocess wall time, PNG bytes, decoded
RGBA bytes, image hashes, raw output bits and fuel. PNG-container and decoded
pixel hashes are separate. Owners come from the independently decoded global
slot table; raw historical `ipgen` has no owner field. The record names that
mapping explicitly rather than claiming the raw tool exposed owner IDs.

Public automated predicates cover dimensions, exact regions/full images,
output kind/listness/count/bit values, named formula cases, the suite's frozen
RNG sequences, alpha blend, discrete part transform and dynamic ramp. Expected
values are derived from task criteria and frozen algorithms, never by executing
the model source a second time as its own oracle. Repeated RGBA/output/fuel
identity and cross-seed image differences are recorded separately.

Outputs include:

- `run.json`: actual source/protocol/tool/prompt/reference hashes and limitations.
- `summary.json`: compilation and automated predicate counts with denominator;
  fully reviewed/model improvement/token metrics remain null where unobserved.
- `<task>/attempt-0/record.json`: source hashes, preflight, render cases and
  pending source-feature/human review fields.
- `repair-feedback.json`: actual diagnostics/traps/failed public predicates
  with the exact failing source map and its hashes; no solved witness source.

Source-feature inspection remains pending manual review for every task. The
marble visual criterion also remains pending until a human reviews the hashed
artifacts. Automated success is not a fully reviewed task-success claim.
The runner does not create a second model attempt; the caller may submit its
public feedback once and execute actual saved repairs with `--attempt 1` and
`--only` for selected task IDs in a separate output directory.

An external `source-feature-review.json` may supply a reviewed verdict bound
to each task definition hash and exact generated-file hash map. Reporting must
verify those hashes before merging the review; raw records are not rewritten.
An AI visual assessment is separate from the human visual verdict.

To archive missing exact context after evaluation without rerendering:

```sh
python3 benchmarks/llm/runner/capture_context.py \
  --run /path/to/measured-run \
  --reference /path/to/frozen/language-v0.4.md \
  --language-export /path/to/frozen/language.json \
  --prompt /path/to/actual/prompt.txt \
  --request /path/to/actual/request.json
```

This saves `captured-context.json`, retains the earlier derived run metadata,
and completes `run.json` context fields. It refuses to replace different archived
context and checks the saved request's exact prompt/task hashes. Sources,
process logs, raw diagnostics, pixels and render records remain unchanged.

For harness tests or authored witnesses, set `--record-kind authored_witness`.
Those records must not be reported as model measurements. The separate
`validate_contracts.py` development script and its authored sources are not
part of this distributed runner or the generator/repair context.

## Small interfaces and later I work

- `binary.py`: structural/resource preflight and explicit owner/slot metadata.
- `png.py`: bounded, CRC-checked RGB/RGBA8 PNG decoder with all five filters.
- `criteria.py`: independent public image/output predicates.
- `run.py`: safe source extraction, process adapter, archival records and feedback.
- `capture_context.py`: missing identity fields from actual archived context.
- `test_runner.py`: decoder, unsafe input, budget and incorrect-result regressions.
- `session.py`: sealed prepare/import/one-repair/replay orchestration.
- `test_session.py`: pending/malformed/integrity boundaries and real CLI repair.

```sh
python3 benchmarks/llm/runner/test_runner.py
```

05-I extends this base with checked offline replay identities, run comparison,
review import/gallery, complete model request metadata, richer malformed batch
records, and CI replay. Separately observed process/batch wall time may be
recorded; unexposed inference timing, model revision/sampling and billing tokens
remain null. A Windows process/pipe adapter is needed before claiming this
POSIX implementation is verified on every CI platform.
