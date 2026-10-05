# Maintaining this documentation

Edit these Markdown chapters for explanations, limitations and examples.
`book.json` fixes their reading order. The site, downloadable book, function
signatures, contents and search index are generated during the build.

An `example` directive includes a real source file; keep its relative path
valid from the chapter. Register each example in
[cases.json](../../examples/language/cases.json) as a successful generation,
an expected compile error or an expected runtime trap. CI requires every
example to be registered and referenced by the book, and exercises every
documented builtin in the successful examples.

Each public builtin has exactly one `builtin` directive followed by its
handwritten behavior and limitations. Signatures/types/named parameter order
come from `ipc --describe-language`, including variadic forms. The emoji
table comes from the same export and the lexer registry. Adding a builtin
requires an explanation and executable example; missing/duplicate descriptions
fail documentation checks.

```sh
python3 scripts/language_docs.py check --compiler build/tools/ipc
python3 scripts/language_docs.py build --compiler build/tools/ipc
npm --prefix website run docs
```

Checks validate relative links and fragments, compile and execute examples,
verify expected failures, and compare generated content with the current
compiler/version. HTML build checks validate rendered anchors and search entries.
The native install, WASM archive and website include the generated documentation
and its source/version manifest; packaging verifies their extracted copies.

Release documentation is built from the exact release tag. Development builds
are labeled as development/candidate documentation and identify their source
commit. Historical versions remain in their Git tags, rather than copies
updated accidentally alongside the current chapters.
