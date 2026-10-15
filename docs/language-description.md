# Compiler language description

`ipc --describe-language` writes one JSON document describing the implemented
source contract. It needs no input file and writes no artifacts. It uses
`schema_version: 4`, which removes implicit draw controls. Schema 3 added typed
asset declarations and intrinsic `constant_evaluable` flags. Schema 2 removed the
schema 1 emoji aliases. Consumers require the supported schema version and
ignore unknown additive fields within it, while rejecting retired draw-control metadata.
This is a capability description, not a promise of planned syntax.

The binary format is unversioned, so operations carry no minimum format version.

## Independent versions

| Field | Meaning and source |
| --- | --- |
| `product_version` | Toolchain release string from `PROJECT_VERSION`. |
| `language_version.major`, `.minor` | Accepted source contract, from `ips/Version.hpp`; matches JSON diagnostics. |
| `c_api_version.major`, `.minor` | Feature version of the bundled C header, from its public macros. |
| `abi_version.major`, `.minor` | C ABI compatibility line configured in `cmake/CAbi.cmake`; the structured form of `c_abi_soversion`. |
| `c_abi_soversion` | Optional additive alias since schema 1: the runtime C library's SOVERSION string from `cmake/CAbi.cmake`, equal to `abi_version` as `major.minor`. Producers emit it; readers validate consistency when present. |

The current development values are product `0.7.0`, source language `0.7`,
C header API `0.4` and ABI line/SOVERSION `0.5`. These values are
independent: signed source inputs do not introduce a new binary input type.
The C versions describe the bundled contract without loading the C library;
query a loaded library with `iprop_library_query` for its actual capabilities.
See [the ABI policy](c-api-abi.md).

Schema 4 describes the constant policy, parameterized lists, samplers/steppers,
point distributions, surfaces, collection limits and receiver-first methods.
See [0.7 migration](language/migration-v0.7.md).

Release preflight and package manifests (`release.json`) use the same names as
strings: `product_version`, `language_version`, `c_api_version` and
`c_abi_soversion`. Packaging and documentation builds reject a compiler whose
export differs from the versions in the source tree. A release requires the
frozen format specification of its product line (`spec/iprop-v0.7.md` for
0.7.x). The current specification remains in development; it cannot serve as a
frozen release contract.

## Source rules

All fields in `source_rules` describe current behavior. Arrays of types are
sets; their ordering has no semantic meaning. `inputs` is a set of mappings
identified by `source_type`.

| Field | Meaning |
| --- | --- |
| `inputs[].source_type` | Accepted input declaration type: `color`, `f32`, `u32` or `i32`. |
| `inputs[].binary_type` | Metadata/host override type in the emitted binary; both source integer types use `u32` bit patterns. |
| `inputs[].value_type` | Type visible to expressions; both integer input types are interpreted as `i32`. |
| `types.storable` | Types admitted in local slots. |
| `types.parameterized` | Supported list/sampler/stepper constructors, element types and immutable arena ownership. Lists of sampler and stepper descriptions are allowed; lists of lists are not. |
| `point_distributions` | Contextual literal families, point counts, original numeric types, snapshot/evaluation order, bounded sampling and quota. |
| `collections` | List size, evaluation, immutable transfer, index, legacy palette and arena rules. |
| `collections.methods` | Optional seventeen receiver-first signatures for `.distinct()` and `.sort()`, from the shared frontend registry. Each records its same-list result, empty parameters, opcode, `constant_evaluable` flag and intrinsic effects. These are separate from global builtins and reserved keywords. |
| `surfaces.methods` | Optional receiver-first `.blur(sigma: f32)` signatures for layer and mask, from `surfaceMethodSpecs`. Each records its same-surface result, opcode, minor 7, false `constant_evaluable` and intrinsic effects. The method name is separate from global builtins and reserved keywords. |
| `surfaces.blur` | Optional Gaussian kernel, sigma range/units, evaluation order, premultiplied alpha, edge extension, preserved mapping, zero identity, fuel, scratch memory and preflight policies. Appears with `surfaces.methods`; both satisfy the complete contract when present. |
| `collections.transformations` | Optional stable first-occurrence/ascending policies, RGBA/x-y order, float zero/NaN equivalence, structural description equality, preserved element bits and unchanged-buffer sharing. Earlier exports may omit these additive fields. |
| `samplers` | Capture syntax, bounds/mass validation, receiver/count order, batch preflight, zero-draw behavior and currently supported const/palette capabilities. |
| `samplers.methods` | Eight statically resolved pick signatures from the shared frontend registry: receiver, count parameter if present, result, opcode and effects. They are separate from the global builtin `pick`. |
| `steppers` | Deterministic capture and quantile rules, required counts, truncated support endpoints, zero/one counts, binary64 integer rounding and RNG effects. |
| `steppers.methods` | Four batch-only pick signatures from `stepperPickSpec`; receivers are stepper<i32/f32/color/vec2>, counts are i32 named n, results are list<T>, and intrinsic RNG is none. |
| `palettes` | Compile-time capture/lifting, supported scalar/description types, numeric widening, index evaluation/fallback, first-class list limits and ramp restrictions. |
| `types.helper_parameters` | Types admitted in helper parameter declarations. |
| `types.helper_value_returns` | Types admitted as declared helper return values; a helper without a return declaration may return no value. |
| `types.output_only_geometry` | Compound geometry values admitted for output writes, but not local slots, helper parameters or helper return values. |
| `types.point_value_type` | A point uses the storable `vec2` value type. |
| `types.implicit_widening[].from`, `.to` | Nonidentity implicit type conversion, derived from the type checker's `argFits`. |
| `types.implicit_widening[].contexts` | `numeric_operands`, `call_arguments`, `helper_returns` and `asset_constants` permit the advertised `i32` to `f32` conversion. |
| `types.local_reassignment` | `exact_type`: later assignment must match the first assignment's type, including when `i32` could otherwise widen to `f32`. |
| `types.components` | Optional list of readable members, added in source language 0.6 within schema 2. Earlier exports omit it; absence means the export does not describe member access. When present, names correspond to opcodes and share a result type. |
| `random_range` | Optional description of random range syntax, overloads and effects, added in source language 0.6 within schema 2. Earlier exports omit it. |
| `random_range.ends` | Optional list of non-numeric range ends (`color`, `vec2`): one shared 0-1 coefficient, the mixed channels or components and the mix instruction, added with gradients within schema 2. |
| `random_range.with` | Optional description of `lower to upper with d`: evaluation order, overloads, truncation, integer rounding, fuel, traps and effects, added with distributions within schema 2. Earlier exports omit it. |
| `locals.declaration` | `first_assignment`: assigning a fresh name creates its local slot. |
| `locals.scope` | `function`: branch/loop braces do not create shadowing scopes. |
| `locals.type_inference_order` | `source_traversal`: the checker visits statements in order, then branch before else branch, including loop bodies. |
| `locals.definite_assignment_analysis` | `false`: an earlier traversed assignment can declare a local even when runtime control flow skips it. |
| `locals.unexecuted_initializer` | `typed_zero`: untouched slots contain integer zero, float zero, transparent black or `{0,0}` according to their fixed type. |
| `locals.read_requires_prior_assignment` | `true`: reading a fresh local before its first assignment in checker traversal is rejected. |
| `visibility.uniforms` | `draw_only`: helpers receive uniform values through explicit parameters. |
| `visibility.uniforms_read_only` | Uniform names cannot be assigned. |
| `visibility.helper_parameters_read_only` | Helper parameter names cannot be assigned. |
| `visibility.seed` | `current_rng_state_in_all_functions`: draw and helpers may read the current RNG state through `seed`; assignment is rejected. |
| `visibility.palettes` | `all_asset_functions`: asset palettes are constants visible to draw and helpers. |
| `visibility.constants` | `all_asset_functions`: typed asset constants are visible to draw and helpers of their defining asset. |
| `asset_constants.keyword`, `.syntax`, `.scope`, `.visibility` | Reserved `const <type> <name> = <expression>` declarations belong to an asset and are visible in all its functions. |
| `asset_constants.types` | Implemented declaration types, derived from shared type rules, including sampler<T>, stepper<T> and their lists. |
| `asset_constants.read_only` | `true`: assignments, loop counters and parameter shadowing are rejected. |
| `asset_constants.dependencies`, `.evaluation` | `forward_acyclic`, `compile_time`: forward references are allowed; a cycle is a source error. |
| `asset_constants.check_all_declarations`, `.check_effects_in_dead_branches` | Both `true`: unused constants and effects in both logical branches are checked. Pure evaluation still short circuits. |
| `asset_constants.user_helper_calls`, `.generation_state` | Both `false`: no helper/part execution, inputs, controls, current seed or generation output reads. |
| `asset_constants.distribution_validation` | `compile_time`: constant distribution constructors validate parameters before an artifact is emitted. |
| `named_calls.targets` | Named arguments are implemented for builtin, helper and part calls. |
| `named_calls.mixed_positional_and_named` | Positional and named arguments may appear in the same call. |
| `named_calls.positional_binding` | `positional_parameter_ordinal`: the first positional argument binds the first positional parameter, and so on. Part placement/transforms/uniforms are named-only, as the exported part signature records. |
| `named_calls.named_arguments_advance_positional_ordinal` | `false`: a named argument does not move that positional cursor. `helper(y=2,1)` can bind `x,y`; `helper(x=2,1)` binds `x` twice and is rejected. |
| `named_calls.duplicate_arguments`, `.unknown_arguments` | Both are `false`: duplicate parameter binding or an unknown name is rejected. |
| `restrictions.recursive_helpers` | `false`: direct and indirect helper recursion is rejected; forward helper calls are accepted. |
| `restrictions.nested_part_imports` | `true`: an imported asset may import parts of its own, resolved beside the importing file. Each alias path is a separate instance, up to 256 instances after expansion; import cycles are rejected. |
| `restrictions.let_declarations`, `.fn_declarations`, `.named_geometry_constructors` | All `false`: declaration introducers `let`/`fn` and named geometry value constructors are unavailable. Existing legacy helpers, vector literals, drawing builtins and color constructors retain their implemented syntax. |
| `effect_order.binary_operands` | `left_to_right`: ordinary binary operand effects execute in that order. |
| `effect_order.logical_operators` | `short_circuit_left_to_right`: `&&`/`\|\|` may skip the right operand, including its effects. |
| `effect_order.call_arguments` | `signature_evaluation_order`: use each builtin's exported `evaluation_order`, `call_rules.helper_evaluation_order`, and the exported part order. |
| `effect_order.named_arguments_change_evaluation_order` | `false`: reordering written named arguments preserves signature order of execution. |
| `effect_order.part_parent_rng` | `restored_after_child`: a part runs on its own RNG stream; its RNG use does not advance the parent's stream after the call. |

Type lists and widening come from the compiler's type helpers. Call signatures,
defaults and ordering come from the shared `CallSignature`/`BoundCallPlan`
model used by typing and lowering. The `evaluation_order` arrays include
nonexecuting signature slots such as a ramp palette, so consumers must not
infer that every listed parameter causes a runtime effect. Polygon coordinates
execute before its color argument. Part uniform arguments execute in input
declaration order before controls and placement; `$uniform` in the exported
template denotes all uniform slots, not a literal source parameter name.

## Executable evidence

Every builtin overload and variadic signature also exports `effects` from
the shared opcode/lowering contracts. `CallSignature.effects` carries this
metadata through binding, typing and planning; no parallel public signature
catalogue is used. A generic helper signature conservatively includes callee
effects. This export does not inspect a particular helper body to prove purity.

Every signature additionally exports `constant_evaluable`. It describes the
intrinsic operation only: all argument expressions must independently satisfy
the constant rules. Pure arithmetic, color/vector/SDF/curve operations and
explicitly seeded noise are eligible. RNG, seeded selection, drawing, outputs
and calls to user helpers/parts are not. `ramp` uses a compile-time legacy
palette; the palette index fallback remains unchanged. Builtin eligibility
comes from the same implementation used by the constant resolver, including
special color, rounding, conversion and beta lowerings.

| Effect field | Meaning |
| --- | --- |
| `effects.rng` | `none` means no intrinsic RNG access; `read_current` observes it; `advance_current` advances it once. `push_seeded_stream` saves the current stream and selects a seed; `restore_stream` restores the saved stream. `seeded_computation` depends deterministically on explicit seed arguments without reading/advancing the current stream. `isolated_seeded_stream` selects on a temporary seeded stream and restores its parent. `shared_callee_stream` conservatively includes a helper's effects on the caller's stream; `isolated_callee_stream` includes a part's independent stream, restored on return. |
| `effects.writes_canvas`, `.writes_outputs` | The operation may change pixels or structured output slots. A `true` value does not promise a visible change for every input. |
| `effects.summary` | `intrinsic` describes the operation itself; `conservative` includes effects that an arbitrary helper/part body may perform. |
| `effects.may_trap` | Potential intrinsic runtime checks on valid compiled source, excluding argument effects and execution-wide resources. `numeric_range` covers checked coordinates/lattice indices, curve thickness and nonfinite HSV hue. `stream_stack_underflow` checks for an absent saved stream. `part_render_arguments` checks placement/scaled part extent. `callee` includes the body's traps. `part_output_missing` checks for an unwritten last-called part output; `part_output_context` requires the slot to belong to this frame's last-called part. `divide_by_zero` and `integer_overflow` describe signed integer division; remainder has only the zero-divisor check. |
| `variadic.choice_evaluation` | `eager_once_before_selection_rng`: every choice is evaluated once in signature order before selection, including unselected choices and their effects/traps. |

`effects` excludes effects of its argument expressions. A nominally pure
`abs(1/v)` can still trap in its integer argument expression. All arguments to
`roll`, `choose` and part calls execute in the caller's stream before entering
the isolated stream. Thus a `unit()` argument advances the parent even when
the seeded selection or child body itself does not. `pick` advances the current
stream after all choices; `choose` advances its temporary seeded stream after
the seed and all choices, then restores the parent. `index(0)` still advances
the stream. Seeded noise and weather do not advance it. Integer conversion and
`round` map NaN to zero and saturate infinities/out-of-range values without an
intrinsic numeric-range trap.

| `effect_rules` field | Meaning |
| --- | --- |
| `intrinsic_scope` | `operation_excluding_arguments_and_resources`: the scope of each intrinsic summary. |
| `argument_effects` | `evaluated_before_operation_in_caller_stream`: compose runtime argument effects with the intrinsic operation in exported signature order. |
| `resource_traps` | Execution-wide fuel and call-depth limits can fail even around operations with an empty intrinsic `may_trap` array. |
| `validation_limits` | The validator bounds operand-stack depth before executing compiled source; the VM also defends this limit when given unvalidated bytecode. |
| `allocation_failure` | Execution also allocates storage; allocation can fail independently of intrinsic domain checks. This failure need not be a VM `Trap`. |
| `helper`, `part` | Conservative operation summaries from the shared call/part opcode metadata. |
| `seed_read` | Effects of reading the current RNG state through `seed`. |
| `output_write` | An output assignment writes its slot without drawing or consuming RNG; effects in its value expression remain separate. |
| `output_write_policy.singular`, `.list` | Singular slots replace their previous value; list slots append in execution order. |
| `part_output_read` | Reading a part output requires this frame's last-called part and a written output. |
| `integer_division`, `integer_remainder` | Intrinsic operator checks, separate from calls or their arguments. |

Defensive checks for malformed/unvalidated bytecode are outside these source
summaries. Host invocation checks are also separate from intrinsic calls.

The effect export check (`scripts/check_language_effects.py` in the source checkout), registered as
CTest `compiler_language_effects`, checks every exported signature and alias
for complete, consistent effects. The shared-registry
builtin tests (`tests/builtin_spec_test.cpp` in the source checkout) check metadata against every
opcode overload. The effect acceptance tests (`tests/compiler/effects_test.cpp` in the source checkout)
execute all registered current-stream/seeded overloads, all builtin numeric
checks, eager selection traces, stream underflow, helper drawing/output writes,
part placement/output-context/missing-output traps and global fuel/call-depth failures. These tests
check accepted source with optimization on/off and exact following RNG bits.

The CLI acceptance corpus (`scripts/check_language_contract.py` in the source checkout), registered
as CTest `compiler_language_contract`, checks every new version/profile field
and compiles valid and invalid input/type/local/visibility/named-call/recursion/
import cases through the real `--check --diagnostics=json` pipeline. It also
rejects future declaration and geometry-constructor syntax.

The control-flow tests (`tests/compiler/control_flow_test.cpp` in the source checkout) execute
uninitialized branch/loop local slots and left-to-right/short-circuit operand
effects with optimization enabled and disabled. The
call tests (`tests/compiler/calls_test.cpp` in the source checkout) execute helper/builtin/polygon/
part/ramp argument order, named permutations and RNG traces. The
part tests (`tests/compiler/parts_test.cpp` in the source checkout) check placement/input ordering
and restoration of the parent RNG. The
input tests (`tests/compiler/defaults_test.cpp` in the source checkout) check signed input defaults,
frozen binary metadata and host override bit patterns. These cases are the
acceptance boundary for changes to the descriptive rules.

The [versioned export contracts](export-contracts.md) define schema migration,
required current fields and additive compatibility.
The [formal language schema](language-description.schema.json) describes every
exported field without duplicating registered signatures.

## Parameterized lists

Schema 4 describes `list<i32>`, `list<f32>`, `list<color>`, `list<vec2>` and
`list<distribution>` in the generated storable/helper/constant type sets.
`source_rules.types.parameterized` publishes the `list` constructor, allowed
element types, nesting policy and `immutable_arena_handle` representation.
`source_rules.collections` publishes literal evaluation, empty contexts,
index behavior, transfer/immutability, palette compatibility and resource
limits. Each published capability must satisfy its complete contract.

The compiler catalogue adds fixed `list.len` overloads and the two-argument
`poly(points, color)` overload. The existing variadic polygon template remains
separate, with its original minimum arity and evaluation order. Type strings
are canonical descriptor spellings. Effects include `index_range` and
`unassigned_handle`; generation memory remains an execution-wide resource trap.

The export also publishes `sampler<T>` and `list<sampler<T>>` in storable/helper
sets. `source_rules.samplers.methods` comes from `samplerPickSpec`, the same
receiver-first signatures used by the typer and emitter. Both arities have
intrinsic `advance_current` effects, describing potential draws; constant
descriptions and zero batches make no draw. Their capture operands can have
effects of their own. It enables sampler constant types and marks
`list.len(list<sampler<T>>)` constant-evaluable, with the same pure declaration
resolver. `source_rules.palettes` describes range capture, scalar lifting and
direct fallback versus checked list indices. The full handbook and bounded
LLM reference generate method tables from this export.

The export includes `source_rules.point_distributions`: weighted literals are
constructors rather than global call signatures. The snapshot, numeric and
bounded-draw contract is shared by source typing, constant evaluation, runtime
builders and file validation. Weighted and density family names are reserved source keywords.

Density is also listed in `source_rules.point_distributions.families`. Family
names, minimum counts, value meaning and equal-bound rules come from the
same shared registry used by parsing, typing, builders and file validation.
No duplicate global signature list is introduced.
Family names used as numeric for-loop bounds retain the following block
as their loop body; nested argument expressions may still construct literals.

The optional `source_rules.resource_exports` policy describes internal mask/layer
slots, direct-child and last-called-part reads, immutable snapshots, no-write
placement retention, shared buffer lifetime, per-generation cleanup and fixed
slot accounting. These slots are separate from every host geometry output index.
