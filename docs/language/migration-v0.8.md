# Migrating from 0.7 to 0.8

Product and source language 0.8.0 add reusable [materials](materials.md).
The binary format and runtime behavior are unchanged. Existing `.iprop`
files from 0.7 and existing host clients remain compatible; adopting
materials does not require a host integration update or an asset rebuild.
Compile new material source with the 0.8 compiler and use matching authoring
tools and documentation.

## Independent versions

| Contract | 0.7.2 | 0.8.0 |
| --- | --- | --- |
| Product, editor, website, Unity and Python packages | 0.7.2 | 0.8.0 |
| Source language | 0.7.2 | 0.8.0 |
| Binary format | unversioned | unversioned, unchanged |
| C API | 0.4 | 0.4 |
| ABI / SONAME | 0.5 | 0.5 |
| Language-description JSON schema | 4 | 4, additive material capability |
| Diagnostic JSON schema | 1 | 1 |
| Documentation-manifest JSON schema | 3 | 3 |

The [0.8 specification](../../spec/iprop-v0.8.md) freezes the same wire
encodings and VM rules for the new product line. The runtime library keeps
the `ipropc` ABI 0.5 loader identity and the 0.7.0 compatibility floor.
No material input type, material output type, host struct or instruction is added.

## Move shared appearance into a library

Put one or more definitions in a separate `.ips` file with no asset:

```ips
material Bark {
  base = #5a4230 to #7a5c40
  shadow = color.mix(base, #000000, 0.45)
  taper = 0.5 to 0.8
}
```

Import that file with `import forest from "materials/forest.ips"`, create
`bark = forest.Bark()` and read `bark.base`. Helpers declare the nominal
type, such as `paint(m: forest.Bark)`. Material definitions cannot be placed
inside or beside an asset. A library's own `palette` and `const` declarations
are private; declarations in the asset remain inside the asset block.

Each range rolls once at construction, in field order. A later default can
read the new earlier fields. Named overrides keep the field's type:
`burnt = forest.Bark(base = #2a1a10)`. A replacement such as
`bark = forest.Bark(taper = bark.taper * 0.9)` evaluates the whole new
instance before replacing `bark`; the override reads the old value.
Copies and field reads do not roll. Moving existing ranges into a material
can change where draws occur, so compare the seeds relevant to your asset
when making that source change. The language does not reserve `material`
as a general identifier; the declaration is contextual.

Only `base: color` is required. Other fields are ordinary author-named
`color`, `i32`, `f32` or `vec2` values. `edge`, `edge_width` and `taper` have
no special behavior or implicit defaults. Materials provide fields; helpers
still decide how geometry is drawn.

## Keep the supported value boundary

Material locals keep their first type; a scalar or another material type
cannot replace them. Fields and helper parameters are read-only. Whole
instances can be copied or replaced by the same type. Returning a material,
material lists, material samplers/steppers, material constants and passing a
material directly into a part are unsupported. Pass scalar inputs or a
numeric selector to a part, then create its material inside that part.

## Check libraries and assets separately

```sh
ipc examples/language/materials/forest.ips --check --diagnostics=json
ipc examples/language/materials/forest.ips --format-only --format formatted-forest.ips
ipc examples/language/material-tree.ips --check --diagnostics=json
ipc examples/language/material-tree.ips -o material-tree.iprop
ipval material-tree.iprop
ipgen material-tree.iprop --seed 7 -o material-tree.png
```

Standalone `--check` and `--format-only` validate a material library and
preserve source-owned diagnostics and comments without producing a binary.
Ordinary compilation still requires an asset entry. The editor uses these
same paths for diagnostics and formatting. Its preview panel continues to
render asset entries.

`ipc --describe-language` adds `source_rules.materials`; protocol readers
accept the optional capability with schema 4. Generated books are now
`docs/language-v0.8.0.md`; use the bundled manifest instead of hard-coding
the previous filename. Existing schema numbers and binary capabilities remain
unchanged. See [export contracts](../export-contracts.md).
