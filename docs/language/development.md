# Maintaining this documentation

Edit these Markdown chapters for explanations, limitations and examples.
`book.json` fixes their reading order. The site, downloadable book, function
signatures and search index are generated during the build. The overview
must link every chapter in book order; CI checks the contents and fragments.

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

The schema 1 language export also supplies `evaluation_order` on every
overload and variadic signature. Its entries name signature slots, including
compile-time palette and transform slots; `poly` lists coordinates before
color. `call_rules.helper_evaluation_order` is `declaration`. The part rule
uses `$uniform` as a template expanded once per imported input, in declaration
order. Each part parameter records `required`, `positional`, and an optional
`default`: integer zero for transforms or `input_declaration` for uniforms.
Uniform defaults are validated with their declarations and retain their exact
stored values. These additive fields come from the same call signatures used
by binding, typing, lowering and source formatting.

`source_rules` records actual input/value types, widening, legacy local scope,
visibility and restrictions. Every signature also has intrinsic `effects`;
`effect_rules` describes helper/part summaries, output writes and resource
failures. Read these from the compiler export instead of maintaining a second
signature/effect list. A callee or argument may have effects beyond an outer
operation's intrinsic summary. The language-description contract explains
the fields and links executable acceptance checks.

Keep current cases consistent with source language 0.5. `negative-default.ips`
and `signed-input.ips` are successful examples now; their old errors remain
only in the immutable 0.4 baseline. Add restricted-constant or signed-range
failures under separate filenames rather than relabeling those valid programs.

```sh
python3 scripts/language_docs.py check --compiler build/tools/ipc
python3 scripts/language_docs.py build --compiler build/tools/ipc
npm --prefix website run docs
npm --prefix website run test:docs
```

Checks validate relative links and fragments, compile and execute examples,
verify expected failures, and compare generated content with the current
compiler/version. HTML build checks validate rendered anchors and search entries.
The native install, WASM archive and website include the generated documentation
and its source/version manifest; packaging verifies their extracted copies.
Full native builds generate `build/language-docs/` when Python 3.10 or newer
is available. Installed SDKs contain it under `share/iprop/`; WASM archives
and the website use the same `docs/`, `examples/` and `spec/` layout.
The manifest binds all handbook chapters, numerical contracts and examples
to their file digests, product version and source commit. Release collection
rejects missing files, changed inputs, foreign commits and documentation
generated from uncommitted changes. Native install checks also compare the
catalog with `ipc --describe-language` from the installed compiler.

Release documentation is built from the exact release tag. Development builds
are labeled as development/candidate documentation and identify their source
commit. Historical versions remain in their Git tags, rather than copies
updated accidentally alongside the current chapters.
