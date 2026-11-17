# Imports and part composition

`import alias from "path.ips"` precedes the asset and resolves relative to
its source file. Parts may import [nested parts](#nested-parts). Duplicate
aliases, missing files and cycles are errors. All parts link into one
executable; generation needs no source files. Aliases share the asset namespace
and cannot match builtin namespaces (`surface`, `noise`, `part`). Shared
[resource outputs](surfaces.md#export-regions-from-parts) apply each hierarchy
level's placement/D4 once; host indices still count geometry only.

<!-- example: ../../examples/language/part.ips -->

<!-- example: ../../examples/language/composition.ips -->

<!-- llm: skip -->
In an asset with `render` an import is not a part: `alias.draw(seed)` is an
expression that returns the imported asset as a [skin](models.md#skins), a
texture for a 3D model.

`alias.draw(seed = seed, at = {x, y}, uniforms...)` requires an `i32` or
[`seed`](random.md#named-seeds) (positional or named) and named placement.
Inside the part, `seed.initial()` returns that call's seed. Transforms/input overrides are
optional and named-only. `at` positions the part's origin.

Extra positional arguments, unknown names and missing seed/placement are
errors. `variant`/`archetype` are ordinary input names. Omitted uniforms keep
the part's defaults. Call order is paint order. Argument effects such as
`unit()` advance the parent stream; child draws use an isolated stream and
leave the parent's post-argument state intact. Parts can paint/write outputs;
exported effects include callee and placement checks.

Transforms are compile-time integers: `rot` is 0, 90, 180 or 270;
`mirror_x`/`mirror_y` are 0/1, including booleans. Each appears at most once.
Rotation precedes mirrors; positive angles are counterclockwise in downward-y
coordinates. D4 transforms permute pixels without filtering.

`alias.outputs.name` reads scalar outputs of the last-called part, including
point `.x`/`.y`; unset reads trap. Values already include the child's placement
and transform in caller coordinates. Assign them to parent outputs to propagate.
List outputs are host-only. Host outputs use root coordinates, with each
hierarchy level applied once at write time. Calls without a write retain
the slot's previous value and placement.

## Part call order

Uniform overrides evaluate once in input declaration order, then seed and
placement, regardless of named order. Transforms are compile-time constants.
See [parts and D4 transforms](../../spec/iprop-v0.8.md#105-part-transforms)
for geometry/bounds and [part instances](../../spec/iprop-v0.8.md#106-part-instances-section)
for output coordinates.

The example reorders named arguments without changing evaluation. Omitted
overrides use typed defaults, including signed integers/floats; signed source
declarations add no runtime input type.

<!-- example: ../../examples/language/errors/list-read.ips -->

## Nested parts

Nested imports resolve beside the importing file: `parts/hilt/hilt.ips`
importing `"grip.ips"` loads `parts/hilt/grip.ips`. `ipc` follows filesystem
symlinks, so `..` leaves the symlink target; embedding hosts use their source store.

Aliases are asset-local. Each root alias path, such as `hilt.grip`, defines
an instance with separate inputs, outputs and RNG, even for repeated imports
of one file. The file is parsed once. Parents draw/read direct children only;
children must propagate grandchild outputs. Imports may expand to at most
256 instances. Cycles through equivalent paths such as `"../dir/a.ips"`
report the whole chain.

<!-- llm: skip -->
The following imports the composition example, which itself imports a part,
and draws it twice; `attached` is the grandchild's anchor in this asset's
coordinates:

<!-- example: ../../examples/language/nested-import.ips -->

<!-- llm: skip -->
`examples/colony/` is a larger three-level example: a colony of mushrooms,
each importing its own stem and cap, with a firefly drawn at every cap's
reported peak.
