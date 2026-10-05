# Migration and contracts for 0.5

This chapter records the agreed implementation contract for the upcoming
0.5 release. It is a development specification, not a claim that every
feature already works in the current compiler. The checked tasks in
[the implementation plan](../../plans/iprop-v0.5-plan.md) identify verified
work; current behavior remains documented in the other chapters until each
change and its examples are delivered.

## Independent versions

| Contract | 0.4 baseline | Target 0.5 |
| --- | --- | --- |
| Product | 0.4.0 | 0.5.0 |
| Source language | 0.4 | 0.5 |
| Binary format | 0.4, including legacy 0.1–0.3 | unchanged |
| Runtime C API feature version | 0.2 | 0.3, additive |
| Shared-library compatibility line | SONAME/install-name 0.4 | unchanged |

Product/source changes do not create new bytecode instructions or imply a
new binary specification. Release tooling must select the actual format
contract separately. The future compiler C API belongs to 0.6 and a separate
library. Legacy binary files and the six existing C entry points remain
supported.

## Source syntax

Both operands of comparison operators use the shift precedence level.
Explicitly grouped existing expressions retain their meaning; the previously
inconsistent unparenthesized right shift operand is corrected. Arithmetic,
short-circuit effects, wrapping integers and deterministic float operations
follow the same VM contracts.

Metadata and import declarations accept an optional trailing semicolon.
Existing forms without semicolons remain; newlines are whitespace, not
implicit statement terminators. Builtin/helper/part calls accept argument
names through a common binder; existing positional forms and aliases remain.

## Type checking and constant defaults

Input defaults and palette elements accept literals, enclosing parentheses
and one numeric unary minus. This is a restricted constant grammar: it does
not admit arbitrary arithmetic, names, function calls or RNG. Integer default
validation examines the mathematical value before conversion/wrapping.
Negative zero is retained for f32.

Source `input i32` maps to the existing binary U32 input. Its mathematical
default is in [-2147483648, 2147483647], with the same bits interpreted as i32
inside draw. Existing U32 defaults, including 0xffffffff, retain their bits;
a negative U32 default suggests the i32 spelling. Runtime-only introspection
reports U32 because a binary does not preserve this source distinction.
Compiler source metadata may report the distinction separately.

A mixed integer/float numeric palette widens to f32. An all-integer palette
remains i32; colors cannot mix with numbers and an integer ramp still fails.
Signed defaults, palette lowering and imported-part defaults use one resolved
typed constant rather than assuming an AST Literal at each layer.

## Runtime semantics and argument effects

Builtins, helpers and part draws use one argument binder and call plan.
Unknown names, missing parameters, duplicate bindings and invalid mixes have
consistent diagnostics. Existing positional calls retain behavior. Named
helper arguments execute in parameter order, each once. Part evaluation
remains uniform overrides in declaration order, then seed, v, a and at.
Poly evaluates vertices before color. The formatter does not change RNG
order, including calls with comments.

Part draw accepts named seed/v/a; variant and archetype are not newly reserved
uniform names. Transform arguments remain compile-time values. Metadata and
structured output owner/slot order do not change.

0.5 keeps declaration by first assignment and the existing helper syntax.
Locals have function scope, exact reassignment types and existing default
runtime values. Helpers do not implicitly capture draw uniforms. Explicit
let/fn, definite-initialization analysis and named geometry constructors are
0.6 work; the 0.5 reference does not instruct models to use them. In 0.6, let
will remain mutable, require an initializer and have function scope without
shadowing. Explicit declarations permit the existing numeric widening only
at initialization; reassignment remains exact. Contextual let/fn and future
geometry names must preserve old identifiers such as let = 1, let(...), a
helper fn or vec2, and a part alias shape. The future vec2 constructor will
serve existing local/helper values; circle/rect constructors describe the
existing output-only geometry rather than adding VM record types.

## Checking, diagnostics and formatting

Check runs imports, parsing, type checks, planning, lowering, assembly in
memory and runtime validation, with no files written. It accepts/rejects the
same program and flags as compilation. Conflicting write flags are CLI
errors. Text and JSON use the same Diagnostic model with stable codes.

The JSON protocol is versioned, emits one document on stdout and has status,
product/language versions and diagnostics. Locations contain a zero-based
byte offset, one-based Unicode-codepoint line/column and exclusive end.
Diagnostics may include expected/received types, related locations,
suggestions and fix-its. A fix-it carries a source-content identity; clients
must not apply it to a different snapshot. CLI/I/O errors may be unlocated.
VS Code converts codepoints to UTF-16 at its boundary.

Exit codes are 0 for no errors, 1 for invalid source/import, 2 for invalid CLI
and 3 for an internal failure. Warnings alone are successful. Recovery is
bounded, suppresses dependent cascades and never sends a damaged AST to the
generator.

Canonical formatting preserves comments and their attachment. A call with
comments retains written argument order; execution still follows its call
plan. The formatter does not invent let or move assignments across branches.
Format-only validates before atomic output and preserves import meaning
when a file is placed in another directory. Formatting is idempotent and
preserves executable bytes after removing debug source locations.

## C API and ownership

API 0.2 layouts, enum values, signatures, array strides and borrowed-result
lifetimes are frozen. Its ignored unknown uniform, first duplicate and
wrong-type trap behaviors remain in the legacy entry point. Additive 0.3
entry points validate arguments and expose structured per-call errors,
program reflection, root parameters, typed outputs and generation statistics.
New extensible descriptors carry a caller size; initializer and collection
APIs must also be size/stride-safe. A runtime query distinguishes the actual
loaded library from the header macros used to compile the host.

Program reflection exposes stored input defaults, output schema, part
geometry and emission, not invented source aliases, constraints or i32 tags.
Parameters are root-only and retain the program data needed by their reset,
clone and queries. ID values are local to a particular program. Setters and
bulk binding are transactional; mutating a parameters handle concurrently
with generation is forbidden.

Results retain their owned pixels/outputs after program-free and subsequent
generations. New checked accessors preserve root/part owner identity,
including identical output names. OOM/error paths do not leak handles or
allow C++ exceptions to cross the C boundary. Errors are per-call, never a
global last_error. The 0.5 thread contract remains independent handles;
shared immutable programs and contexts are verified 0.6 work.

## Verification and migration

The frozen 0.4 corpus records source/binary hashes, diagnostics, image/raw
output bits, traps, RNG probes and optimized/reference sizes. Correct old
programs retain image/output behavior; reference-versus-optimized equality
and the optimized-size bound remain. Historical sources that were rejected
and become valid keep their old expectation in the baseline only; current
examples and optimizer acceptance are migrated together.

One compact LLM reference is generated from compiler registries, handbook
and executable examples. It lists actual capabilities and limitations of
the compiler version used. The 24-task pilot saves prompts and actual model
outputs, separates compiler/render/visual success and reports unknown model
revision/sampling or missing observations honestly. Deterministic CI replays
recorded attempts; live generation is separately budgeted.

Each completed task is reviewed, verified and committed on develop. Native,
WASM, C API, SFML/Godot and relocated SDK checks close milestones and the
release candidate. The implementation prepares 0.5 artifacts; merging,
tagging and publication are a subsequent release task.
