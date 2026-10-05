# Imports and part composition

`import alias from "path.ips"` precedes the asset. Imports form an acyclic
graph; duplicate aliases, missing files and cycles are errors. The CLI resolves
import paths against the entry source's directory, including paths in nested
imported files. Keep imported files together using paths relative to that
entry directory. The compiler links parts into one executable; generation
does not load source files.

<!-- example: ../../examples/language/part.ips -->

<!-- example: ../../examples/language/composition.ips -->

`alias.draw(seed, variant, archetype, at = {x, y}, uniforms...)` supplies
exactly three positional integers, a required named placement and optional
named input overrides. Unspecified uniforms use the part's defaults. `at`
positions the part's declared origin. Call order determines paint order;
the parent's RNG stream is restored after the part executes.

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
seed, variant, archetype and placement expressions evaluate. Transform
arguments are compile-time constants. Each dynamic argument evaluates once.
See [parts and D4 transforms](../../spec/iprop-v0.3.md#105-part-transforms-v03)
for the exact geometry mapping and rectangle bounds.

<!-- example: ../../examples/language/errors/list-read.ips -->
