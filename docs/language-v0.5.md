# The v0.5 source language

The complete reference is the [language handbook](language/index.md).
Its chapters are the shared source for the website and release documentation.

Build the native tools, then run:

```sh
python3 scripts/language_docs.py build --compiler build/tools/ipc
```

The complete generated book is `build/language-docs/docs/language-v0.5.md`.
Its signatures and emoji table come from `ipc --describe-language`.
Native SDKs and WASM packages ship that book under `docs/`; the website
renders it as `docs/language.html` and offers the Markdown download.
The product remains 0.4.0, binary format 0.4, C API 0.3 and C ABI 0.4;
[export contracts](export-contracts.md) describes the separate version fields.
