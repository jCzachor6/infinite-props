# Authoring and repair benchmark

This directory defines the shared 0.4/0.5 authoring experiment before compiler
changes. It contains **24 fixed tasks: 16 assets and 8 repairs**, and a protocol
for first-pass generation plus at most one repair. The definitions are not
measured results. No generator output, compiler result, reviewed image, token
usage or improvement claim is implied by their presence.

The user authorized the current Codex session model for a live pilot.
`protocol.json` records `codex_session` and the observed configuration hints
`gpt-6.1-sol` / `max`. Those hints do not identify the actual resolved model
revision or sampling parameters. Unexposed identity, sampling, per-task
inference time, token usage and cost are **null**, never guessed. One sample per
task is exploratory evidence, not a statistical evaluation of model quality.

## Definitions

- `tasks.json`: requirements, required source features, complete repair inputs,
  render cases, image/output predicates and definition hashes.
- `protocol.json`: frozen references, exact prompt templates, isolation,
  adapters, resource ceilings, raw record fields, metrics and offline replay.
- Future run records live under `runs/<run_id>/`; a future runner must preserve
  them without substituting authored solutions for actual model responses.

| ID | Task |
| --- | --- |
| asset-01 | Centered rectangle and point attachment |
| asset-02 | Nested-loop checkerboard |
| asset-03 | Indexed color palette bands |
| asset-04 | Variant and archetype badge |
| asset-05 | Typed host uniform overrides |
| asset-06 | Seeded dots and ordered list output |
| asset-07 | Probability gates, including zero/one probability |
| asset-08 | Direct-seed random draw preserving the parent stream |
| asset-09 | Typed positional helper and attachment list |
| asset-10 | Translucent glaze on an opaque destination |
| asset-11 | Imported part, override and translated attachment |
| asset-12 | Exact D4 part rotation and geometry |
| asset-13 | Point/rect/circle outputs and an unset slot |
| asset-14 | Dynamic color ramp |
| asset-15 | Terminating while/continue/break loop |
| asset-16 | Bounded, seeded marble with human visual review |
| repair-01 | Floating condition |
| repair-02 | Misspelled named builtin argument |
| repair-03 | Uniform visibility in a helper |
| repair-04 | Recursive helper |
| repair-05 | Integer palette used by ramp |
| repair-06 | Reading a part's list output as a scalar |
| repair-07 | Invalid 45-degree part transform |
| repair-08 | Reassigning a local with a different type |

All successful solutions can use the executable 0.4 subset. The tasks do not
require `let`, `fn`, geometry constructors, named helper parameters, named
part `seed`/`v`/`a` parameters, signed defaults, source `input i32`, or mixed
numeric palettes. The eight supplied repair programs are intentionally invalid;
their diagnostic hints are expectations to verify against the historical
compiler, not results already observed. Inputs use UTF-8 with LF exactly as
saved in JSON, including their final newline.

## Capture the baseline before changing the compiler

1. Freeze released `v0.4.0`, source commit
   `66d34c2831585afb231022da9296528a912e962e`, plus compiler, renderer and
   validator binaries with SHA256 and build/platform identity. Keep this
   snapshot separate from the mutable development tree.
2. Extract `docs/language-v0.4.md` from the released SDK, or generate the full
   book from that exact source/compiler pair. The canonical source chapter
   list is `docs/language/book.json`, beginning with `docs/language/index.md`.
   Save `ipc --describe-language` stdout verbatim and hash all reference bytes.
   Current modified documentation must not replace the frozen reference.
   For the current 0.5 candidate, use the complete generated
   `docs/language-v0.5.md`, catalog and documentation manifest schema 2 from
   the same source/compiler pair. Its product remains 0.4.0; the handbook
   filename follows the source language. Frozen 0.4 keeps its exact schema-1
   manifest and full archived handbook through the explicit historical profile.
3. Archive exact task/protocol bytes and prompt template hashes. Create a fresh
   generator agent with `fork_turns=none` for each version, supplying only the
   saved prompt, version reference/export, manifest and output destination.
   Generators must not inspect compiler internals, run the compiler, read
   another version's responses or consult solved fixtures.
4. Save the exact combined prompt/message sequence and actual raw response.
   Extract the returned UTF-8 sources without normalization and hash every
   file. Record malformed or missing task/file results explicitly.
5. Compile and render the 0.4 sources with the frozen binaries. For failed
   tasks, provide actual diagnostics or failed public criteria to the model
   once; save that exact repair prompt and actual repaired sources.
6. Retain all baseline records before changing compiler implementation. A task
   list or authored reference program is not a measured baseline. Freeze the
   candidate 0.5 toolchain/reference at its audited commit and repeat with the
   same task order, sample count, cases, budgets and prompt strategy.

A single batch can contain all tasks. Its actual model call count is one,
not 24; per-task inference timings and usage are unavailable unless exposed.
Repair feedback may batch failed tasks, with no second repair for any task.
The protocol distinguishes the invalid starting source for a repair task from
the model's first-pass proposed correction and its subsequent repair attempt.

## Evaluate functionality and images separately

Historical 0.4 uses ordinary compilation into an isolated temporary directory
and retains exact text stdout/stderr; it does not support check JSON. The
candidate uses the full `--check --diagnostics=json` pipeline, followed by
temporary compilation for rendering. Historical stable diagnostic codes,
full source ranges and fix-its remain unavailable rather than synthesized.

Every task has explicit render cases. The common seeds are `0`, `7`, `26` and
`4294967295`; variant/archetype and input-focused tasks provide their own fixed
cases. Run each case twice and compare decoded RGBA8 pixels and output
binary32 bits. PNG-container identity is recorded separately. Keep slot/list
order and unset values. Historical `ipgen` may omit output owners; obtain them
from the native C API or record the approved static slot mapping and its
limitation. Never claim an owner was observed when the adapter omitted it.

Pixel rules describing background followed by an overlay compose in that
order; they do not also require the background under the overlay. Whole-image
normative predicates (compositing, RNG, ramps and D4) need an independently
reviewed oracle. Until that oracle exists, partial sample pixels do not prove
the whole-image requirement. Required source features also need review: a
comment containing a builtin name does not prove meaningful use of the builtin.

The marble task additionally requires a human verdict on coherent blue marble
appearance. Record reviewer, verdict, gallery artifact hashes and limitations.
Automated color counts, compilation and determinism do not establish visual
quality. Pending human review remains pending and is reported separately.

## Equal budgets and recorded limitations

The protocol bounds files/source/response/binary sizes, root and part dimensions,
part count, output count and process logs. Task canvases are 32x32; the preflight
ceiling is 64x64 / 4096 root pixels. Compile and individual render subprocesses
have a 15-second wall timeout. The model batch has a 3600-second aggregate wall
ceiling when orchestration can enforce it; per-task inference limits are not
pretended to be enforced inside a batch.

Before rendering, inspect and validate the compiled artifact and verify its
dimensions, parts and declared fuel. The declared fuel ceiling is 1,000,000,
matching the current compiler. API 0.2 has no host fuel override: **do not
rewrite the binary fuel header or claim VM memory accounting/cancellation is
available**. Source/binary/pixel/output bounds and external wall limits constrain
the pilot but are not a total-memory accounting guarantee. If required
preflight information cannot be inspected reliably, report a blocked or
unsupported render, not an unchecked successful one.

Compare compiler success, traps, repeated-pixel/output equality, automated
criteria, source-feature review and human visual status independently. Keep
the denominator at 24 and list unfinished tasks. Only report observable timing
and usage. Unknown model revision/sampling and this small one-sample design
limit any inference about an improvement between releases.

## Offline replay and CI

Replay uses the saved actual first-pass and repair sources, never a live model.
Verify their hashes and the exact tasks, protocol, prompts, references,
toolchain identities and render inputs; then rerun check/compile/preflight/
render and implemented predicates. Preserve actual failures and unavailable
evidence. Keep both compiler versions' raw diagnostics and outputs for later
0.6 comparisons.

Regular CI will replay saved responses deterministically. Live model requests
are a separately invoked, bounded pilot. The task definitions alone do not
make replay implemented or its future CI integration complete.

## Captured current-session pilot

The actual 0.4 first-pass responses and exact prompt/reference are archived in
`runs/20261005-codex-pilot/0.4/`. `evidence.tar.gz` preserves 1,542 raw evidence
files; `evidence-manifest.json` pins every member and the archive. No solved
acceptance witness was supplied to the generator or substituted for a response.

Frozen 0.4 compiled all 24 responses. All 97 cases were rendered twice, with
194 successful executions, matching repeat RGBA/output bits/fuel, and all 24
public automated criteria passing. A separate assistant review verifies every
required source feature. The marble human visual criterion remains pending;
raw `summary.json` deliberately does not claim a fully reviewed result. No
failed public criterion justified a repair request, so the repair batch count
is zero, under the agreed maximum of one.

The definitions/protocol status fields are frozen snapshots from before
measurement; current observations live in the run artifacts. This n=1 pilot
is one inherited-model agent batch with shared task context. Resolved model
revision, sampler settings, per-task inference time, usage and cost are unknown.
The configured model label is only a hint. The already-perfect automated 0.4
baseline creates a ceiling: a future equal score does not demonstrate improved
model quality. 05-I will compare the candidate and record diagnostic/visual
evidence and these limitations without inventing an uplift.
