# Materials — 0.8 language contract

**Status:** Frozen for product and source language 0.8.0 (2026-10-28).
This source-language addition uses the unchanged format and VM specified by
[`iprop-v0.8.md`](iprop-v0.8.md). Existing 0.7 binaries and host integrations
remain compatible; new material source requires the 0.8 compiler. See the
[0.7 to 0.8 migration guide](../docs/language/migration-v0.8.md).

## Source contract

- A material is defined only in a material file: a `.ips` file with one or
  more `material Name { ... }` definitions and no `asset`. Defining a
  material in an asset file is an error.
- A material file may declare its own `palette` and `const` values; field
  expressions may use them. They are private to the material file: an
  importing asset does not see them, and its own declarations (or another
  material file's) may reuse their names. Top-level `palette` and `const`
  declarations in an asset file are an error.
- Assets import material files with the ordinary `import alias from "path"`
  syntax. The alias is not a part and cannot be called or drawn; it
  qualifies the file's materials (`alias.Name`).
- Fields: `base: color` is required. Any other field is author-named and may
  be a `color`, `i32`, `f32` or `vec2` constant or range, optionally with a
  `with` distribution. A field may read the fields written before it by
  name, so a field cannot share its name with a palette or const of the
  file. Duplicate fields and a missing `base` are errors. Errors in a
  material file are reported against that file.
- `m = alias.Name()` in a function creates an instance: every range rolls
  once, each field independently, in the order the fields are written.
  Field reads never roll. `alias.Name(field = value, ...)` replaces fields
  for that instance: each override is named, sets a field of the
  definition at most once, has the field's type, and is evaluated in the
  creating function in the field's place, so later fields read it.
- Each definition is a distinct type. Helpers declare parameters as
  `m: alias.Name`; the compiler checks field reads against that definition and
  reports unknown fields by name.
- A local keeps the type of its first assignment; a material cannot replace
  a scalar or a different material type. Parameters, inputs, palettes and
  constants remain read-only. A whole-instance replacement evaluates the
  new instance completely before updating the target: overrides read the
  old target, while default expressions read the new earlier fields.
- Material names are qualified and do not shadow unqualified helpers or
  builtins. Expressions in a material file retain their builtin bindings
  when imported; only overrides are resolved in the creating function.
- Instances are immutable, are copied field-by-field without new draws, and
  cannot be declared as host inputs. Returning a material, lists of
  materials, and `sampler`/`stepper` of materials are outside this contract.
- Standalone material files support `ipc --check` and `--format-only`,
  including semantic diagnostics and comment-preserving formatting, without
  producing an executable. Ordinary compilation requires an asset entry.

## Execution contract

Materials are a language-level value: the compiler expands an instance into
its scalar fields (colors, numbers, vec2) before code generation, in the
field order above, so the generated `.iprop` uses only the existing VM value
types and instructions. Creation is the only point that consumes RNG draws;
field reads, copies and helper calls consume none. The same source, seed and
inputs therefore produce the same pixels and outputs as the equivalent
hand-written field code, on every backend, with no format change.
