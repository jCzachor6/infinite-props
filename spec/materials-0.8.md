# Materials — 0.8 language contract (draft)

Status: draft for the 0.8 release line. The frozen 0.7 format/VM contract in
`iprop-v0.7.md` is not rewritten; this note records the material semantics
that the 0.8 language adds on top of it. Backward compatibility with 0.7
binaries is not a requirement for 0.8, but every breaking change must be
listed in the release migration note (see `CHANGELOG.md`).

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
- Instances are immutable, are copied field-by-field without new draws, and
  cannot be declared as host inputs. Returning a material, lists of
  materials, and `sampler`/`stepper` of materials are outside this contract.

## Execution contract

Materials are a language-level value: the compiler expands an instance into
its scalar fields (colors, numbers, vec2) before code generation, in the
field order above, so the generated `.iprop` uses only the existing VM value
types and instructions. Creation is the only point that consumes RNG draws;
field reads, copies and helper calls consume none. The same source, seed and
inputs therefore produce the same pixels and outputs as the equivalent
hand-written field code, on every backend, with no format change.
