# third_party

Vendored single-file dependencies, pinned to exact revisions. Nothing here is
modified; update policy: bump the revision, re-run the full test suite.

| Dependency | File(s) | Revision | License | Used by |
| ---------- | ------- | -------- | ------- | ------- |
| stb | `stb/stb_image.h`, `stb/stb_image_write.h` | `master` @ 2026-09 (see header comments) | MIT / public domain | PNG I/O for `ipgen` and golden tests |
| Catch2 | fetched via FetchContent (not vendored) | v3.7.1 | BSL-1.0 | test framework |
