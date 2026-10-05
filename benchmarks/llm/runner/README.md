# Archived-source benchmark runner

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

```sh
python3 benchmarks/llm/runner/test_runner.py
```

05-I extends this base with checked offline replay identities, run comparison,
review import/gallery, complete model request metadata, richer malformed batch
records, and CI replay. Separately observed process/batch wall time may be
recorded; unexposed inference timing, model revision/sampling and billing tokens
remain null. A Windows process/pipe adapter is needed before claiming this
POSIX implementation is verified on every CI platform.
