# Imports and part composition

`import alias from "path.ips"` precedes the asset. The current compiler
supports direct parts only: an imported asset cannot declare imports of its
own. Duplicate aliases, missing files, cycles and nested part imports are
errors. The CLI resolves import paths against the entry source's directory.
Keep imported files together using paths relative to that entry directory.
The compiler links parts into one executable; generation does not load
source files.

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
point `.x`/`.y`. A read before that slot has been set traps. Assigning that
value to a parent's output propagates it. List outputs are host-facing and
cannot be read this way. Part-owned points, circles, rectangles and appended
lists receive placement/transforms, including writes in part helpers. Parent
outputs retain parent coordinates.

## Part call order

Uniform overrides evaluate in the part's input declaration order. Then the
`seed`, `v`, `a` and placement expressions evaluate, regardless of the order
of named arguments in the source. Transform
arguments are compile-time constants. Each dynamic argument evaluates once.
See [parts and D4 transforms](../../spec/iprop-v0.3.md#105-part-transforms-v03)
for the exact geometry mapping and rectangle bounds.

The current example uses named controls in a different written order from
their evaluation order. Omitted overrides use the part's resolved declaration
defaults, including signed integer and floating constants; no new runtime
input type is introduced for a signed source declaration.

<!-- example: ../../examples/language/errors/list-read.ips -->

The following imports the composition example, which itself imports a part,
and is therefore deliberately rejected:

<!-- example: ../../examples/language/nested-import.ips -->
