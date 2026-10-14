# Emission and physical container metadata

`IPROP_CAP_METADATA_EXTENDED` advertises the emission and physical format
fields in API 0.4's `iprop_program_info_ex` on ABI line 0.5.
`iprop_program_query` and `iprop_program_info_ex_init` handle them. Clients
must use the current header and rebuilt layouts; `iprop_meta` is the separate
untyped metadata record.

| Fields | Meaning |
| --- | --- |
| `has_emission` | Fixed-width presence, exactly zero or one |
| `emission_color` | Exact stored RGBA8 channels |
| `emission_offset_x`, `emission_offset_y`, `emission_radius` | Exact stored binary32 bits |
| `format_major`, `format_minor` | Version of the actual loaded physical container |
| `container_flags` | Flags of that physical container, including debug/compression |

When `has_emission` is zero all inactive emission fields are zero, including
RGBA alpha and positive-zero float representations. When it is one, the
query performs no arithmetic: negative zero, subnormals and stored NaN payloads
are preserved. These fields report executable metadata; no source-only ranges
or signed type aliases are inferred.

Physical format fields come from `LoadedFile.containerHeader`, which is retained
before expanding a compressed container. Consequently a compressed debug
container reports flags 3 even when the expanded execution layout has the
compression bit cleared. Flags zero, debug flag 1 and compression flag 2
retain their binary meaning. These are output metadata, not query options.
Program format version is distinct from the library query's accepted format
range and API/product version. Every accepted physical container reports
major 0, minor 7.

Use the offsets of the current API 0.4 descriptor. Smaller allocations on
this ABI receive only complete fields in their prefix; this does not promise
compatibility with ABI 0.4 headers. Negotiate the capability before reading
the emission and physical format fields.
The initializer clears only complete known fields; query preserves size,
padding, partial fields and unknown tail bytes. Only `struct_size` is an input,
with the existing four-byte minimum. Color is one frozen four-byte leaf record
and is written only when all channels fit.

Queries use the same [checked error and alias contract](c-api-checked.md),
including optional bounded errors, success reset and complete main-output
preservation on failure. Error descriptor/message output must not overlap
any declared query output capacity, including the appended fields. Metadata
querying and reporting allocate no new memory and execute no VM instructions.
Both legacy-loaded and checked-loaded handles support these fields independently
of caller load-buffer lifetime, debug presence or compression.
