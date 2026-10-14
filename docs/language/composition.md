# Imports and part composition

`import alias from "path.ips"` precedes the asset. The path is relative to
the file that contains the import, and an imported asset may import parts
of its own ([nested parts](#nested-parts)). Duplicate aliases in one asset,
missing files and import cycles are errors. The compiler links every part
into one executable; generation does not load source files.
The alias `surface` is reserved for the [surface namespace](surfaces.md);
rename an existing import using that alias when recompiling.
Parts can share immutable mask/layer snapshots through
[resource outputs](surfaces.md#export-regions-from-parts). They follow each
hierarchy level's placement and D4 once; host indices still count geometry only.

<!-- example: ../../examples/language/part.ips -->

<!-- example: ../../examples/language/composition.ips -->

`alias.draw(seed = seed, v = v, a = a, at = {x, y}, uniforms...)` requires
three integer controls named `seed`, `v` and `a`. These controls may instead
be positional in that order, or mix positional and named arguments.
For example, `alias.draw(a = a, seed, v, at = {8, 4})` binds its two
positional arguments to `seed` and `v`. Named arguments do not advance the
positional cursor; binding the same control twice is an error.

Placement `at`, transform arguments and optional input overrides are named
only. Extra positional arguments, unknown names and missing controls or
placement are errors. Input names `variant` and `archetype` remain ordinary
uniform names; they do not alias the `v` and `a` controls. Unspecified
uniforms use the part's defaults. `at` positions the part's declared origin.
Call order determines paint order; the parent's RNG stream is restored
after the part executes to its state after the dynamic argument expressions.
An override containing `unit()` therefore still advances the parent stream;
random draws made by the child use its isolated stream. A part may paint and
write its own outputs, and its conservative exported effect summary includes
callee/placement/selector checks rather than assuming the child is pure.

Optional transform arguments are compile-time integers: `rot` is 0, 90, 180
or 270; `mirror_x`/`mirror_y` are 0 or 1, including boolean constants.
Each can occur once. Rotation is applied before the mirrors. Positive angles
are counterclockwise in the downward-y plane; transforms are exact D4 pixel
permutations, not filtered image rotation.

Scalar `alias.outputs.name` reads the last called part's output, including
point `.x`/`.y`. A read before that slot has been set traps. The value is in
the reading asset's coordinates: the child's transform and placement are
already applied, so it can place another part or be drawn directly.
Assigning it to a parent's output propagates it. List outputs are
host-facing and cannot be read this way. The host receives every output in
the root asset's coordinates, including points, rectangles, circles and
list elements written by part helpers; each enclosing call is applied once,
when the value is written. A singular output keeps its last written value: a
later call that does not write it neither replaces nor moves it.

## Part call order

Uniform overrides evaluate in the part's input declaration order. Then the
`seed`, `v`, `a` and placement expressions evaluate, regardless of the order
of named arguments in the source. Transform
arguments are compile-time constants. Each dynamic argument evaluates once.
See [parts and D4 transforms](../../spec/iprop-v0.3.md#105-part-transforms-v03)
for the exact geometry mapping and rectangle bounds, and
[part instances](../../spec/iprop-v0.5.md#6-part-instances) for the output
coordinates.

The current example uses named controls in a different written order from
their evaluation order. Omitted overrides use the part's resolved declaration
defaults, including signed integer and floating constants; no new runtime
input type is introduced for a signed source declaration.

<!-- example: ../../examples/language/errors/list-read.ips -->

## Nested parts

An imported asset may import parts itself, to any depth. Each import path is
resolved beside the importing file: a `hilt.ips` in `parts/hilt/` that
imports `"grip.ips"` loads `parts/hilt/grip.ips`, whichever asset imports
the hilt. `ipc` resolves paths on the file system, so `..` after a
symlinked directory leaves its target; embedding hosts resolve against
their own source store.

Aliases are local to their asset. An instance is named by its alias path
from the root, such as `hilt.grip`, and each alias path is a separate
instance with its own inputs, outputs and RNG stream, even when two branches
import the same file. That file is still loaded and parsed once. A parent
draws and reads only its direct children; to expose a grandchild's output,
the child copies it to an output of its own. At most 256 instances may
result from expanding the imports. A cycle, including one through an
equivalent path such as `"../dir/a.ips"`, is an error naming the whole
chain.

<!-- llm: skip -->
The following imports the composition example, which itself imports a part,
and draws it twice; `attached` is the grandchild's anchor in this asset's
coordinates:

<!-- example: ../../examples/language/nested-import.ips -->

<!-- llm: skip -->
`examples/colony/` is a larger three-level example: a colony of mushrooms,
each importing its own stem and cap, with a firefly drawn at every cap's
reported peak.
