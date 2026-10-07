# third_party

Vendored pure-Python packages (`python/`, added to `sys.path` by the scripts that
use them) and the external dependencies' license notices, pinned to exact
revisions. Nothing here is modified; update policy: bump the revision, re-run the
full test suite.

| Dependency | File(s) | Revision | License | Used by |
| ---------- | ------- | -------- | ------- | ------- |
| markdown-it-py | `python/markdown_it/`, `python/markdown_it-LICENSE`, `python/markdown_it-LICENSE.markdown-it` | 4.2.0, wheel `markdown_it_py-4.2.0-py3-none-any.whl` SHA-256 `9f7ebbcd14fe59494226453aed97c1070d83f8d24b6fc3a3bcf9a38092641c4a` (port of markdown-it 14.1.0) | MIT | handbook and `LLM_REFERENCE.md` Markdown structure in `scripts/markdown_tokens.py`, parsed like the website's markdown-it |
| mdurl | `python/mdurl/`, `python/mdurl-LICENSE` | 0.1.2, wheel `mdurl-0.1.2-py3-none-any.whl` SHA-256 `84008a41e51615a49fc9966191ff91509e3c40b939176e643fd50a5c2196b8f8` | MIT | URL normalization required by markdown-it-py |
| Catch2 | fetched via FetchContent (not vendored) | v3.7.1 | BSL-1.0 | test framework |
| zlib | native system dependency or vcpkg; Emscripten port (not vendored) | native >= 1.2.9; WASM SDK port | zlib license (`zlib-LICENSE.txt`, upstream v1.3.2 notice) | lossless v0.4 DEFLATE containers |
