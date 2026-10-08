# The v0.6 source language

The complete reference is the [language handbook](language/index.md).
Its chapters are the shared source for the website and release documentation.

Build the native tools, then run:

```sh
python3 scripts/language_docs.py build --compiler build/tools/ipc
```

The complete generated book is `build/language-docs/docs/language-v0.6.md`.
Its signatures come from `ipc --describe-language`.
Native SDKs and WASM packages ship that book under `docs/`; the website
renders it as `docs/language.html` and offers the Markdown download.
Source language 0.6 is released with product 0.6.0. It emits binary format
0.5 ([specification](../spec/iprop-v0.5.md)) when a file uses a 0.5
instruction or has parts, otherwise 0.4; C API 0.3 and C ABI 0.4 are unchanged. [Export contracts](export-contracts.md)
describes the separate version fields.
