# Generating a compact .ips reference

The normal documentation build generates `LLM_REFERENCE.md` beside the full
handbook. Native SDK installation copies it into `share/iprop/`, where its
handbook/catalog/example links resolve offline. It records the product,
source-language, binary-format, C API and ABI versions separately, together
with the source commit and dirty/release status. It is generated content;
edit its source selection in `docs/llm-reference.json`.

The default document has a **12,288-byte UTF-8 budget**, with a maximum allowed
budget of **16,384 bytes**. Generation fails before replacing output if the
selected content exceeds that budget. No section, signature or example is
silently truncated. Reduce the selection when more detail would exceed it.

```sh
python3 scripts/language_docs.py build --compiler build/tools/ipc
python3 scripts/language_docs.py build --compiler build/tools/ipc \
  --output-dir build/language-docs-small \
  --llm-sections asset,types,helper,symbols --llm-symbols pixel,int,unit \
  --llm-max-bytes 8192
python3 scripts/language_docs.py check --compiler build/tools/ipc \
  --report-dir build/ci-reports/language
```

Available section ids are `asset`, `types`, `helper`, `part`, `palette`,
`output`, `symbols` and `diagnostics`. Selection preserves the supplied order;
unknown or repeated ids/names are rejected. An empty selection is explicit,
for example `--llm-symbols ''`. Compiler rules, version identity and links to
the full handbook/catalog remain in every selected document. `--llm-symbols`
accepts current builtin names from the compiler export, including dotted names
and variadic signatures. It matters when the `symbols` section is selected.

The default includes complete registered asset, helper, part/composition,
palette and structured-output programs. Authored explanations are selected
whole paragraphs from named handbook headings. Signatures, types, evaluation
orders and intrinsic effects come directly from `--describe-language`; there
is no separately maintained LLM signature catalogue.

Three default repair pairs start from registered rejected examples: a local
type mismatch, direct uniform access from a helper and an unknown named
argument. Their correction recipes apply only when the selected text occurs
exactly once. Generation checks each rejection's stable diagnostic code and
registered message through real `--check --diagnostics=json`, then checks,
compiles and renders the corrected program. The selected valid examples are
also checked, compiled and rendered with seed 7 from temporary owning copies
of the registered example tree. No executable is written beside its source.

The generated file, selection configuration and source explanations/examples
are covered by the existing documentation manifest hashes. `language_docs.py
check` writes execution evidence to its report. Archive/source verification
uses the existing export schemas; this task adds no new export-schema contract.
