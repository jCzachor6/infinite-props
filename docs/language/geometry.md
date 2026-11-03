# Vectors and distance fields

Vectors store binary32 components. Most geometry calculations use binary32;
normalization uses scaled binary64 intermediates before rounding its result.
Signed distances are
negative inside closed shapes, zero on their boundary and positive outside.
Segment/Bézier distances are unsigned. Distance queries do not paint pixels;
combine them with `fill` and a color operation to author coverage.
A random point of a segment is `a to b` between two `vec2` values; see
[segments](random.md#gradients-and-segments).

<!-- example: ../../examples/language/geometry.ips -->

## Geometry handles on develop

The 0.9 foundation adds five distinct immutable handle types: `diagram2d`,
`contours2d`, `regions2d`, `part2d` and `parts2d`. They can be inferred locals,
explicitly typed locals, helper parameters and helper return values. Copying
or passing a handle shares its data. These type spellings are contextual:
`diagram2d = 1` remains an ordinary numeric local assignment.
Existing `pick` and `choose` can select matching handles with their usual
evaluation and RNG rules, sharing the selected data.

Handles cannot be host inputs, asset constants, palette elements, or elements
of `list<T>`, `sampler<T>` or `stepper<T>`. Reading a handle whose initializer
did not execute traps; it does not produce an empty geometry value. Locals
retain the language's existing function scope.

Stage 04 supplies storage, typing and ownership; stages 05–06 add Voronoi and
contours below. Region and dynamic-part operations follow in subsequent stages.
See the [geometry core contract](../../spec/geometry-core-v0.9.md).

<!-- example: ../../examples/language/geometry-handles.ips -->

## voronoi
<!-- builtin: voronoi -->

Creates a bounded diagram in local asset coordinates, independently of the
target, clipping and Worley noise. Supply either a count (1–256) and explicit
seed, or a `list<vec2>` of distinct finite sites inside the closed rectangle.
Sites retain their indices. Positive finite size and representable endpoints
are required; geometry that cannot retain positive f32 cells traps.

The generated overloads take 4, 7, 9 or 12 arguments. The short form uses
`voronoi.random` (0), jitter 1 and no relaxation. The seven-argument form adds
method, jitter (0–1) and Lloyd iterations (0–4). `voronoi.grid` (1) creates a
row-major grid; its incomplete last row spans the width evenly. Jitter 0
keeps grid centers, jitter 1 uses each whole grid slot. Random validates
jitter but ignores it. Neither method advances the current RNG stream.

The nine-argument form adds anisotropy (0.25–4) and a finite nonzero direction.
Distance is squared parallel displacement divided by anisotropy squared,
plus squared perpendicular displacement. Values above 1 extend cells along
the direction. The twelve-argument form adds `row_bias`, `row_jitter` and
`row_offset`, all in 0–1. Bias mixes base sites with rows along direction;
jitter moves each row across that direction, and offset shifts alternate
rows by a fraction of one slot. Defaults are 0, 0 and 0.5. Bias 0 skips the
row layout; anisotropy 1 is exactly the ordinary metric.

Explicit sites accept an optional fourth argument, `relax`. Each iteration
moves sites to clipped-cell centroids and rebuilds the diagram. The
[Voronoi contract](../../spec/voronoi-v0.9.md) pins distribution, numeric
degeneracies, canonical topology, limits and fuel.

<!-- example: ../../examples/language/voronoi.ips -->

## diagram.count
<!-- builtin: diagram.count -->

Returns the number of sites and cells. Copying a diagram or reading its
count and sites shares geometry without allocating a new store.

## diagram.site
<!-- builtin: diagram.site -->

Returns the f32 site at an index in 0 through `diagram.count(d)-1`.
An invalid index traps. After Lloyd this is the relaxed site.

## diagram.neighbors
<!-- builtin: diagram.neighbors -->

Returns an ordinary `list<i32>` of sorted unique indices sharing a positive
length edge. Contact at a single vertex is excluded. The returned list
uses the ordinary list quota; the immutable diagram is shared.

## contour
<!-- builtin: contour -->

Extracts immutable closed geometry from a mask's alpha samples, retaining its
stored placement, raster scale and D4. It scans the complete source, including
pixels outside the active target. It does not draw or advance the RNG.
The two-argument call is `contour(source, threshold)`. The five-argument call
adds `simplify`, `min_area` and `connectivity`, in that order. Defaults are
0, 0 and `contour.four` (0); use `contour.eight` (1) to join diagonal foreground
samples. Both modes keep explicit holes and independent islands inside holes.

Threshold must be finite in 0–1. At zero, alpha greater than zero is inside;
otherwise alpha at least threshold is inside. An empty mask returns a valid
empty set, and a full mask closes exactly on its rectangle. Alpha is linearly
interpolated between pixel centers; the
[contour contract](../../spec/contours-v0.9.md) pins the endpoint degeneracy rule
and all sixteen Marching Squares cases.

Simplify is a finite nonnegative distance in local units. It may retain extra
points to preserve orientation, all holes, nesting and freedom from
intersections. Min_area is a finite nonnegative net component area, after
subtracting immediate holes and after simplification. It keeps or removes a
whole component with its holes; islands inside holes are tested separately.
Components have stable decreasing-area order with bounding-box and discovery
tie breaks. Geometry limits and fuel also apply to data later filtered out.
Contours can be passed through helpers and converted with `regions`.

<!-- example: ../../examples/language/contours.ips -->

## regions
<!-- builtin: regions -->

Converts a diagram or contours to separately addressable components. The short
call shares immutable geometry and preserves holes and source order. The full
call adds `inset` and `min_area`; both arguments must be supplied. Positive
inset shrinks the filled shape and negative inset grows it, with round joins.
An offset can remove a component or split it into several fragments with the
same ID. Min_area filters net fragment area after the offset. Different source
regions remain independent when they grow into each other. It creates
no dynamic part. See the [regions contract](../../spec/regions-v0.9.md).

<!-- example: ../../examples/language/regions.ips -->

## regions.count
<!-- builtin: regions.count -->

Returns the number of current components. An empty set returns zero.

## regions.id
<!-- builtin: regions.id -->

Returns the original site index or canonical input contour component index.
Result indices address current components; source IDs survive filtering and
can repeat when a component is split by an offset.

## regions.area
<!-- builtin: regions.area -->

Returns cached local area with holes subtracted. An invalid index traps.

## regions.center
<!-- builtin: regions.center -->

Returns the cached centroid, with hole moments subtracted.

## regions.neighbors
<!-- builtin: regions.neighbors -->

Returns sorted unique current indices whose original diagram edges establish
adjacency. Missing components are excluded; contour sources return an empty
list. These are provenance relationships, so a later gap does not erase them.

## regions.points
<!-- builtin: regions.points -->

Returns an ordinary `list<vec2>` in canonical ring order for a region without
holes and with at most 256 vertices. Regions with holes or more vertices trap;
no points or holes are discarded. Use `regions.mask` for these shapes.

## regions.mask
<!-- builtin: regions.mask -->

Creates an A8 mask for one indexed region, or for the geometric union when
called with only the set. It preserves holes, samples at the active target's
raster scale, retains off-target geometry and ignores active clipping while
creating the resource. Paint or clip the returned mask explicitly.

Coverage uses a fixed 4 by 4 subpixel grid and half-open shared boundaries.
The union is formed before A8 quantization, so overlapping regions do not add
alpha. Empty sets return a transparent 1 by 1 mask at origin zero.

## part.create
<!-- builtin: part.create -->

Create an independent runtime instance from one region (`source, index`), an
A8 mask or an RGBA layer (`source`). Sources are shared. Region holes and source
IDs survive; rasters keep soft alpha and have source ID -1. An entirely
transparent raster traps. Creation does not draw. The generation can create at
most 256 instances, even if earlier handles have been discarded.
See the [dynamic part contract](../../spec/dynamic-parts-v0.9.md).

<!-- example: ../../examples/language/dynamic-parts.ips -->

## parts.from_regions
<!-- builtin: parts.from_regions -->

Create one instance for each region in order. Empty input returns an empty
collection. The whole creation budget is checked before publication.

## parts.count
<!-- builtin: parts.count -->

Return the number of instances in a collection without rebuilding its source.

## parts.get
<!-- builtin: parts.get -->

Read an immutable instance by zero-based index. Invalid indices trap; reading
does not allocate an ID or copy the source.

## part.id
<!-- builtin: part.id -->

Return the instance's generation-local ID. IDs start at zero and are shared by
helper calls and statically imported components.

## part.source_id
<!-- builtin: part.source_id -->

Return the source region's ID, or -1 for a raster instance. Two creations from
the same source have distinct instance IDs and the same source ID.

## part.fill
<!-- builtin: part.fill -->

Return a new view with a solid tint, replacing a previous texture. The shape
and its original alpha, source and ID remain shared. Earlier views keep their
appearance. Tint alpha multiplies shape coverage when drawing.

## part.texture
<!-- builtin: part.texture -->

Return a view using a shared RGBA layer in the original source coordinates.
This replaces a solid tint. Pixels outside the texture are transparent. The
texture's mapped raster scale must match the original source scale.

## part.place
<!-- builtin: part.place -->

Place the original anchor at `at`, applying runtime `rot` (0, 90, 180, 270),
then `mirror_x`/`mirror_y` (0 or 1). Each call replaces placement relative to
the original; it does not accumulate earlier placements. Translation rounds
to target pixels with `floor(t+0.5)`, including negative positions. No implicit
raster scaling occurs; rotations that swap axes also swap required scales.

## part.center
<!-- builtin: part.center -->

Read the effective anchor after placement rounding. The original anchor is
the region centroid with holes removed, or the alpha-positive pixel bbox center.

## part.bounds_origin
<!-- builtin: part.bounds_origin -->

Read the transformed shape bbox origin, independent of appearance and clipping.
This uses cached metadata without rescanning the source.

## part.bounds_size
<!-- builtin: part.bounds_size -->

Read the transformed shape bbox size. Quarter turns exchange width and height;
fills and textures do not change it.

## part.draw
<!-- builtin: part.draw -->

Draw one runtime instance in the current composition mode and clip. Region and
mask sources need `fill` or `texture`; a captured layer keeps its original RGBA
appearance by default, applying alpha once. Drawing does not create another ID
or export a snapshot. A static import aliased `part` continues to use
`part.draw(seed, at=...)`; a single argument selects the dynamic operation.

## part.mask
<!-- builtin: part.mask -->

Return the transformed A8 shape without drawing. Appearance and the active
clip do not affect it. Raster pixels keep their exact alpha; region holes use
the shared 4×4 rasterizer. Existing A8 buffers remain shared; RGBA alpha is
materialized only within its nonempty bbox. Later painting applies clipping.

## regions.gap
<!-- builtin: regions.gap -->

Shrinks every current boundary by half the requested gap width, including
outer boundaries and holes. Width must be finite and nonnegative. Zero
shares the original geometry. Repeated calls accumulate on the current
geometry; fragments preserve IDs and ancestry. It uses the same round-join
offset kernel and limits as `regions` with an inset, and does not draw.
The four-argument form adds finite `variation` in 0–1 and an explicit integer
seed. Zero variation is exactly the constant gap. Otherwise a bounded linear
profile varies the width around its requested value. Both sides of an original
shared edge read the same profile; independent contour islands need not match.
Repeated calls use the current geometry while retaining original-edge profiles.
See the [gap and bevel contract](../../spec/gap-bevel-v0.9.md).

<!-- example: ../../examples/language/gap-bevel.ips -->

## regions.bevel
<!-- builtin: regions.bevel -->

Creates a separate RGBA8 shading layer inside region boundaries, including
hole boundaries, without changing geometry or painting the current target.
Width must be finite and positive; light must be finite and nonzero and points
toward the light in local coordinates. The short call uses white highlight,
black shadow and strength 0.5. The six-argument form adds explicit colors and
finite strength in 0–1, preserving each color's alpha.

The closest edge's outward normal determines highlight or shadow. The effect
fades linearly to zero at the requested width and is sampled on the same 4 by 4
grid as region masks. Equal distances choose the original edge index, then
the current traversal index. At overlaps the first current region owns the
sample. Alpha and premultiplied color are averaged before RGBA8 quantization.
Paint the result explicitly with `surface.paint(shade, blend=surface.source_over)`
or the existing legacy mode. Target scale sets resolution; clipping applies
when the layer is painted. Off-target bounds are retained.

<!-- example: ../../examples/language/wall-bevel.ips -->

## vec2.len
<!-- builtin: vec2.len -->

Returns vector length using the pinned multiply/add/square-root order.

## vec2.dot
<!-- builtin: vec2.dot -->

Returns the scalar dot product of two vectors.

## vec2.normalize
<!-- builtin: vec2.normalize -->

Returns a unit direction from a finite vector. Zero, including signed zero,
returns `{+0, +0}`. NaN or infinity in either component traps. The scaled
binary64 algorithm handles maximum and subnormal f32 components without
overflowing the length; it draws no randomness. See the
[normalization contract](../../spec/iprop-v0.8.md#22-stable-vector-normalization).

<!-- example: ../../examples/language/errors/normalize-nonfinite.ips -->

<!-- example: ../../examples/language/math-bounds.ips -->

## vec2.rot
<!-- builtin: vec2.rot -->

Rotates a vector by a floating angle in radians using pinned sine/cosine.
Positive angles are counterclockwise in the downward-y plane. This continuous
vector rotation is distinct from the exact discrete part transforms.

## dist.circle
<!-- builtin: dist.circle -->

Signed distance to a circle defined by a center and radius.

## dist.rect
<!-- builtin: dist.rect -->

Signed distance to a centered axis-aligned rectangle. `half_size` contains
half-width and half-height, not its full dimensions.

## dist.roundrect
<!-- builtin: dist.roundrect -->

Signed distance to a centered rounded rectangle with half-dimensions and a
corner radius. Supply dimensions/radius suitable for the shape being authored.

## dist.line
<!-- builtin: dist.line -->

Distance to a finite segment. Degenerate segments and nonfinite inputs follow
the exact [distance-field contract](../../spec/iprop-v0.8.md#129-signed-distance-fields).

## dist.bezier
<!-- builtin: dist.bezier -->

Distance estimate to a quadratic/cubic Bézier curve on `[0, 1]`, selected by
the control-point count. The kernel performs a fixed bounded search rather
than an arbitrary host-library approximation. Nonfinite geometry traps.

## fill
<!-- builtin: fill -->

Converts signed distance and a transition width to floating coverage.
The exact zero/nonpositive width and NaN rules are in the distance-field spec.

## fold
<!-- builtin: fold -->

Folds a vector into an n-sector kaleidoscope domain, returning a `vec2`.
The sector count is an integer. Valid counts and angular folding follow
[fold and polar](../../spec/iprop-v0.8.md#1213-the-dither-the-fold-and-the-polar).

## polar
<!-- builtin: polar -->

Returns `{radius, angle}` for a vector; angle is in radians under the same
downward-y convention as vector rotation. Signed-zero behavior is pinned.
