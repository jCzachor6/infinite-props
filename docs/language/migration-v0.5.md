# Migration and contracts for 0.5

This chapter describes the implemented source language 0.5 in the current
checkout. The product release number is separate. Use the compiler's
`--describe-language` export for its actual versions, capabilities, signatures,
evaluation order and effects; use the generated book's commit/status to identify
the documentation snapshot. The [implementation plan](../../plans/iprop-v0.5-plan.md)
also records work that remains planned.

## Independent versions

| Contract | Frozen 0.4 release | Current checkout |
| --- | --- | --- |
| Product | 0.4.0 | 0.4.0 until the release-version task |
| Source language | 0.4 | 0.5 |
| Binary format | 0.4, accepting legacy 0.1–0.3 | 0.4, unchanged |
| Runtime C API feature version | 0.2 | 0.3, additive |
| Shared-library compatibility line | SONAME/install-name 0.4 | 0.4, unchanged |

Source changes add no bytecode instructions or binary input types. Product,
language, format, API and ABI versions must be queried independently. The six
legacy C entry points, their layouts and generation policy remain supported.
A source declaration does not create metadata that the binary never stored.
`ipc --describe-language`, release preflight and package `release.json` name
them `product_version`, `language_version`, `format_version`, `c_api_version`
and `c_abi_soversion`. A 0.5 release checks the frozen `spec/iprop-v0.4.md`
of the format it emits; there is no `iprop-v0.5.md` binary specification.

## Source syntax and argument binding

Shifts bind more tightly than comparisons on both sides. `value < 1 << bits`
means `value < (1 << bits)`; comparison chains still associate leftward.
Explicitly grouped old expressions retain their meaning. Arithmetic,
short-circuit effects, wrapping integers and deterministic floating operations
follow the same VM contracts.

One optional semicolon may follow imports and each metadata/input/output/palette
declaration. Existing forms without semicolons remain valid. Newlines are
whitespace, never implicit statement terminators; use `return;` for a bare
return before another statement.

Builtin, helper and part calls accept positional, named and mixed arguments
through the shared binder. A named argument does not advance the positional
cursor: the next positional argument still fills the next positional signature
slot. Unknown, missing and duplicate bindings are rejected. Helper parameter
names must be unique, including in unused declarations. Part `seed`, `v`, `a`
controls can be named; placement, transforms and uniform overrides are named-only.
Input names `variant` and `archetype` remain ordinary uniforms.

The 18 lexer emoji aliases of 0.4 (for example `📦` for `asset` or `✏` for
`draw`) are removed. Replace each alias with its word; `ipc --format` of a 0.4
source cannot do this, because 0.5 rejects the alias as an unexpected
character. Emoji remain valid in comments and string literals. The language
description export moves to schema 2 without its `emoji_aliases` list.

The [syntax example](../../examples/language/syntax.ips) uses declaration
semicolons; [helper](../../examples/language/functions.ips) and
[composition](../../examples/language/composition.ips) examples use reordered
named arguments. Both legacy positional forms and these named forms remain
executable cases in the current book.

## Signed defaults and palettes

Defaults and palette elements accept literals, enclosing parentheses and one
numeric unary minus. Arithmetic, names, calls, RNG and repeated negation are
rejected in these declarations. Floating negative zero is preserved. An integer
used as an `f32` default converts directly to binary32 without first wrapping
to a 32-bit integer pattern; root and imported-part defaults share this resolver.

Source `input i32` checks its mathematical default in
`[-2147483648, 2147483647]` before wrapping, and stores the existing binary U32
type with the corresponding bits. Thus `-2147483648` is valid but positive
`0xffffffff` is not an i32 default. Existing `u32` defaults retain bit-pattern
behavior; use `0xffffffff` for signed -1 bits or declare `input i32` for a
mathematically negative default. Host U32 overrides and binary reflection retain
U32 tags; reflection cannot recover whether the source spelling was i32 or u32.

Mixed integer/float numeric palettes widen to `f32`; all-integer palettes stay
`i32`. Colors cannot mix with numbers, and an integer palette remains invalid
for `ramp`. Parenthesized negative elements and negative zero appear in the
[palette example](../../examples/language/palettes.ips).

`negative-default.ips` and `signed-input.ips` are successful current cases.
Their old compile-error observations remain only in the frozen 0.4 baseline;
do not restore those errors in the current `cases.json`. Negative U32, signed
range and non-literal declaration errors now have separate registered examples
in [input uniforms](assets.md#input-uniforms).

## Evaluation, effects and existing locals

Named arguments change binding, not execution order. Helpers evaluate arguments
once in parameter declaration order. Ordinary builtins use their exported
signature order; `poly` evaluates vertices before color. Parts evaluate uniform
overrides in input declaration order, then seed/v/a/placement. Transform arguments
are compile-time constants. A call containing comments retains written order in
formatted source while keeping the same runtime call plan.

`pick` and `choose` evaluate choices eagerly. `roll`/`choose` isolate selection
after argument evaluation and restore the parent's resulting state. Part draws
also restore the parent after their dynamic argument effects; child draws use
the isolated stream. Helpers share their caller's stream and can draw/write
their asset's outputs. The [call-effects case](../../examples/language/call-effects.ips)
checks helper order and eager choices against a replayed stream.

The export separates intrinsic operation effects from argument effects and
execution-wide resource failures. Helper/part summaries are conservative
callee summaries. Scalar output writes replace; list writes append. Fuel and
call-depth failures are resource-wide; numeric drawing/noise checks, unset part
output reads and unmatched stream pops are call-specific checks. Operand-stack
bounds are validated before execution. Integer division traps on zero or signed
minimum divided by -1; integer remainder traps on zero but returns zero for
that minimum/-1 pair.

Locals still use first assignment, function scope, exact reassignment types and
typed zero when an initializer's branch is skipped. The compiler checks source
traversal order and does not perform definite-assignment analysis. Helpers do
not capture draw uniforms; pass uniforms and `v`/`a` explicitly. Recursive
helpers, nested part imports, `let`/`fn` declaration syntax and named geometry
constructors are unsupported. Existing helpers and identifiers with those
spellings keep their existing meaning. Circle/rect values remain output-only.

## Checking, diagnostics and formatting

`--check` runs the full import/compile/validation pipeline in memory without
writing source or binaries. Stable typed codes, owning source identities and
Unicode-codepoint/UTF-8-byte ranges are shared by text and JSON diagnostics.
Recovery collects at most 20 diagnostics and suppresses dependent cascades;
a damaged AST never reaches generation. See the [diagnostic protocol](../compiler-diagnostics.md).

`--format ... --format-only` validates and atomically publishes canonical source
without an executable. Ordinary `--format` also compiles. Formatting preserves
comment contents/attachment, import destinations, legacy local behavior and
call effects. It is idempotent and preserves executable bytes after removing
debug source locations. It introduces no new declaration syntax; see
[tooling](tooling.md) for flags, conflicts and publication guarantees.

## Implemented C API and remaining work

The additive API 0.3 currently provides checked load/error boundaries, loaded
library version/capability queries, exact stored root-input defaults, output
declarations and stored part tables/signatures before generation.
Output declarations retain root/part owner
identity and stored order. Sized descriptors, exact UTF-8 views and transactional
error behavior are described in [root-input reflection](../c-api-reflection.md)
and [output declarations](../c-api-output-declarations.md).

`IPROP_CAP_PART_REFLECTION` provides part count/get and part-input count/get/find.
It exposes stored size/origin, owner N+1 and the part's output range; signature
indices belong to that part and introduce no host part-input override API.
Current binaries do not store a part/import name: `name_present` is zero and
the name view is NULL/zero, rather than a fabricated alias or function name.
An absent name is distinct from a present empty name. See
[stored parts and signatures](../c-api-parts.md).

`IPROP_CAP_METADATA_EXTENDED` adds stored emission and physical container
version/flags to the sized program query. The frozen legacy metadata record
is unchanged. See [extended metadata](../c-api-metadata.md).

`IPROP_CAP_RESULT_STATISTICS` exposes existing VM fuel counters, image bytes
and generated output element count through a sized result query. See
[result statistics](../c-api-statistics.md).

`IPROP_CAP_IMAGE_VIEW` exposes sized borrowed pixel views with actual dimensions,
byte count, tight stride, format and explicit storage origin/legacy alpha mode.
Its pointer and bytes are identical to `iprop_result_image` and remain owned by
the result. See [image views](../c-api-images.md).

`IPROP_CAP_RESULT_OUTPUTS` exposes every generated declaration slot and exact
owner/name lookup. `IPROP_CAP_TYPED_OUTPUTS` independently exposes checked
point/rect/circle copies with explicit whole-record capacity. Both preserve
legacy pointers/packing and exact stored float bits; borrowed views survive
`program_free`, while typed copies survive `result_free`. See
[result outputs](../c-api-result-outputs.md).

Root parameter handles remain planned. Optimization
level selection also remains planned; the existing production optimizer and
`--no-opt` are available. Do not infer those planned capabilities from the API
feature version. Query the actual loaded library's capability bits.

## Verification and migration

The immutable 0.4 baseline retains historical source/binary hashes, rejected
cases, pixels/output bits, traps and RNG observations. Current examples and
their expectations describe the implemented compiler; changing a historical
rejection into a current success does not rewrite that baseline. Correct old
programs retain their runtime behavior, including reference/optimized equality.

The handbook and compact `LLM_REFERENCE.md` are generated from the compiler
export and registered executable examples. The current language cases must all
be referenced, and successful examples cover every registered builtin. Run the
[documentation checks](development.md) against the actual compiler after editing
this chapter or a source example. Release tagging/publication remains a separate
task from implementing source language 0.5.
