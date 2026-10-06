# Compiler language description

`ipc --describe-language` writes one JSON document describing the implemented
source contract. It needs no input file and writes no artifacts. The additive
fields below retain `schema_version: 1`; existing builtin, call-rule and emoji
fields keep their meanings. Consumers should ignore unknown additive fields.
This is a capability description, not a promise of planned syntax.

## Independent versions

| Field | Meaning and source |
| --- | --- |
| `product_version` | Toolchain release string from `PROJECT_VERSION`. |
| `language_version.major`, `.minor` | Accepted source contract, from `ips/Version.hpp`; matches JSON diagnostics. |
| `format_version.major`, `.minor` | Binary format emitted by this compiler, from runtime constants. This is not a range of formats accepted by a loaded library. |
| `c_api_version.major`, `.minor` | Feature version of the bundled C header, from its public macros. |
| `abi_version.major`, `.minor` | C ABI compatibility line configured in `cmake/CAbi.cmake`. |

The current values are product `0.4.0`, source language `0.5`, emitted format
`0.4`, C header API `0.3` and ABI line `0.4`. These values are independent:
signed source inputs do not introduce a new binary input type. The C versions
describe the bundled contract without loading the C library; query a loaded
library with `iprop_library_query` for its actual capabilities and supported
format range. See [the ABI policy](c-api-abi.md).

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
| `types.helper_parameters` | Types admitted in helper parameter declarations. |
| `types.helper_value_returns` | Types admitted as declared helper return values; a helper without a return declaration may return no value. |
| `types.output_only_geometry` | Compound geometry values admitted for output writes, but not local slots, helper parameters or helper return values. |
| `types.point_value_type` | A point uses the storable `vec2` value type. |
| `types.implicit_widening[].from`, `.to` | Nonidentity implicit type conversion, derived from the type checker's `argFits`. |
| `types.implicit_widening[].contexts` | `numeric_operands`, `call_arguments` and `helper_returns` permit the advertised `i32` to `f32` conversion. |
| `types.local_reassignment` | `exact_type`: later assignment must match the first assignment's type, including when `i32` could otherwise widen to `f32`. |
| `locals.declaration` | `first_assignment`: assigning a fresh name creates its local slot. |
| `locals.scope` | `function`: branch/loop braces do not create shadowing scopes. |
| `locals.type_inference_order` | `source_traversal`: the checker visits statements in order, then branch before else branch, including loop bodies. |
| `locals.definite_assignment_analysis` | `false`: an earlier traversed assignment can declare a local even when runtime control flow skips it. |
| `locals.unexecuted_initializer` | `typed_zero`: untouched slots contain integer zero, float zero, transparent black or `{0,0}` according to their fixed type. |
| `locals.read_requires_prior_assignment` | `true`: reading a fresh local before its first assignment in checker traversal is rejected. |
| `visibility.uniforms` | `draw_only`: helpers receive uniform values through explicit parameters. |
| `visibility.uniforms_read_only` | Uniform names cannot be assigned. |
| `visibility.helper_parameters_read_only` | Helper parameter names cannot be assigned. |
| `visibility.draw_controls.names`, `.type`, `.read_only` | Draw's implicit `v` and `a` names are read-only `i32` values. |
| `visibility.seed` | `current_rng_state_in_all_functions`: draw and helpers may read the current RNG state through `seed`; assignment is rejected. |
| `visibility.palettes` | `all_asset_functions`: asset palettes are constants visible to draw and helpers. |
| `named_calls.targets` | Named arguments are implemented for builtin, helper and part calls. |
| `named_calls.mixed_positional_and_named` | Positional and named arguments may appear in the same call. |
| `named_calls.positional_binding` | `positional_parameter_ordinal`: the first positional argument binds the first positional parameter, and so on. Part placement/transforms/uniforms are named-only, as the exported part signature records. |
| `named_calls.named_arguments_advance_positional_ordinal` | `false`: a named argument does not move that positional cursor. `helper(y=2,1)` can bind `x,y`; `helper(x=2,1)` binds `x` twice and is rejected. |
| `named_calls.duplicate_arguments`, `.unknown_arguments` | Both are `false`: duplicate parameter binding or an unknown name is rejected. |
| `restrictions.recursive_helpers` | `false`: direct and indirect helper recursion is rejected; forward helper calls are accepted. |
| `restrictions.nested_part_imports` | `false`: a compiled imported part cannot itself declare imports. Source loading does not relax this planner restriction. |
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

The [CLI acceptance corpus](../scripts/check_language_contract.py), registered
as CTest `compiler_language_contract`, checks every new version/profile field
and compiles valid and invalid input/type/local/visibility/named-call/recursion/
import cases through the real `--check --diagnostics=json` pipeline. It also
rejects future declaration and geometry-constructor syntax.

The [control-flow tests](../tests/compiler/control_flow_test.cpp) execute
uninitialized branch/loop local slots and left-to-right/short-circuit operand
effects with optimization enabled and disabled. The
[call tests](../tests/compiler/calls_test.cpp) execute helper/builtin/polygon/
part/ramp argument order, named permutations and RNG traces. The
[part tests](../tests/compiler/parts_test.cpp) check placement/control ordering
and restoration of the parent RNG. The
[input tests](../tests/compiler/defaults_test.cpp) check signed input defaults,
frozen binary metadata and host override bit patterns. These cases are the
acceptance boundary for changes to the descriptive rules.
