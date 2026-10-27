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
  expressions may use them.
- Assets import material files with the ordinary `import alias from "path"`
  syntax. The alias is not a part and cannot be called or drawn.
- Fields: `base: color` is required (constant or range, optionally with a
  `with` distribution). `edge: color` defaults to `base`; `taper: f32`
  defaults to `1.0`; `edge_width: i32` (pixels) defaults to `0`. Any other
  field is author-named and may be a `color`, `i32`, `f32` or `vec2` constant
  or range. Duplicate fields and a missing `base` are errors.
- `m = Name()` in a function creates an instance: every range rolls once, each
  field independently, in definition order. Field reads never roll.
- Each definition is a distinct type. Helpers declare parameters as
  `m: Name`; the compiler checks field reads against that definition and
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
