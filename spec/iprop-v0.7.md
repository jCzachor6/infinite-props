# iprop binary format 0.7

Format 0.7 removes the built-in variant and archetype mechanism. It is an
intentional incompatible successor to [format 0.6](iprop-v0.6.md). The current
reader accepts major 0, minor 7 only. Older binaries must be rebuilt from
source; there is no reader adapter or automatic control-input migration.
Product/source/C API/ABI versions are independently 0.7.0/0.7/0.4/0.5.

## Metadata and entry frames

The meta section starts with `u32 size_x`, `u32 size_y`, `f32 origin_x`,
`f32 origin_y`, `u64 fuel`, `u16 flags` and six zero reserved bytes. Optional
emission data, input declarations and output declarations follow with their
existing encodings. There are no variant/archetype counts or padding fields
in their place. Canvas sizes and fuel must remain positive.

The root `draw` function has exactly one parameter per declared root input,
in declaration order: color, f32 or i32 for a u32 input. A part entry has the
same rule for its own inputs. Both entries return no values. There are no
hidden parameters or reserved local slots; the first declared input has
slot 0 and declared locals follow the input parameters. Helpers retain their
explicit parameter and return signatures.

The compiler supplies no `v` or `a` bindings. They are ordinary identifiers;
authors may declare inputs, constants, locals, outputs and helper parameters
with those names. Undeclared reads fail source checking. `variants` and
`archetypes` metadata declarations are rejected. A need for selectable styles
or classes is expressed through ordinary asset inputs.

## Part calls and generation

`part.draw` (0xA0) consumes `[uniforms..., i32 seed, f32 at_x, f32 at_y]`
and returns no value. Uniform values follow the child's declaration order.
Its aux byte retains the D4 transformation encoding. Source calls accept
`seed`, named `at`, constant `rot`, `mirror_x`, `mirror_y` and declared child
inputs. The removed control arguments have no aliases. An ordinary child
input named `v` or `a` binds through the same named-input path as any other.

Generation accepts seed, optional canvas dimensions and declared input values.
There is no implicit index selection, index range check or index reflection.
Root streams start from the supplied seed; helpers share their caller stream
and part calls retain isolated child streams and parent-stream restoration.

## Containers and retained kernels

Both plain files and compressed envelopes use version 0.7. The envelope uses
the existing bounded zlib encoding and wraps a plain 0.7 file; nested
compression remains forbidden. Debug and compressed flag bits retain their
values. Compression never produces a 0.4 wrapper, and format selection never
falls back to an earlier minor for simple assets.

Other section IDs, instruction numbers, scalar/collection/sampler/surface
representations and numeric, RNG, rasterization and mask kernels retain the
latest 0.6 rules. Lists, including stable `.distinct()`/`.sort()`, keep their
256-element bound and immutable semantics. The 64 MiB generation-data cap,
2,000,000 default fuel and 32 combined surface scopes remain in effect.

## Host contracts

C API 0.4 uses ABI line 0.5 with earliest compatible product 0.7.0. Generation
options and metadata descriptors contain no variant/archetype fields. Clients
must rebuild against the new header; there is no ABI 0.4 replacement promise.
The library reports supported format range 0.7–0.7. CLI, WASM/JS, SFML,
Godot and the editor expose seed and ordinary inputs. Atlases enumerate seeds
with fixed input values rather than implicit control axes.
