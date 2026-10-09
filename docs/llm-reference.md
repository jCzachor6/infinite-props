# Generating the .ips LLM reference

The normal documentation build generates `LLM_REFERENCE.md` beside the full
handbook. Native SDK installation copies it into `share/iprop/`, where its
handbook/catalog/example links resolve offline. It records the product,
source-language, binary-format, C API and ABI versions separately, together
with the source commit and dirty/release status. It is generated content;
edit its source selection in `docs/llm-reference.json`.

The reference teaches the whole language and pushes one style. It opens with
the idioms chapter ([Writing idiomatic props](language/idioms.md)) and its
examples, then before/after pairs that each fix one broken rule, then the
compiler rules in plain sentences. The handbook follows: assets, syntax,
types, control flow, helpers, randomness, colors, palettes, parts, execution
limits and the grammar. Every builtin is listed, and checked repairs close the
document. The default document has an **81,920-byte UTF-8 budget**, with a
maximum allowed budget of **98,304 bytes**. Generation fails before replacing
output if the selected content exceeds that budget. No section, signature or
example is silently truncated. Reduce the selection when more detail would
exceed it.

```sh
python3 scripts/language_docs.py build --compiler build/tools/ipc
python3 scripts/language_docs.py build --compiler build/tools/ipc \
  --output-dir build/language-docs-small \
  --llm-sections idioms,style,types,symbols --llm-symbols pixel,int,unit \
  --llm-max-bytes 24576
python3 scripts/language_docs.py check --compiler build/tools/ipc \
  --report-dir build/ci-reports/language
```

Configured section ids are `idioms`, `asset`, `assets`, `syntax`, `types`,
`control`, `helper`, `random`, `colors`, `palette`, `part`, `limits` and
`grammar`; `style`, `builtins`, `symbols` and `diagnostics` are generated.
The compiler rules follow `idioms` and `style` and appear in every document,
even with an empty selection. Each configured section names a handbook chapter
and either `"parts": "chapter"` (its introduction and every subsection with
prose) or a list of parts, each a heading with optional `paragraphs` (default:
all), `skip` and `fences` (fenced block languages kept verbatim, such as the
EBNF grammar). A paragraph marked `<!-- llm: skip -->` in the handbook is left
out. `examples` are shown as models of the style; `demos` are labelled API
demos that show a section's calls on a tiny canvas, not the style of a prop.

`style` renders the `style_pairs`: registered passing examples that break one
idiom, each with an explanation and a corrected form produced by exact
replacements. `builtins` lists every builtin in handbook order with its
exported signatures, a `Style:` line from `hints` where one is configured and
the first paragraph of its description. `symbols` gives the full effect
summary of the builtins named by `--llm-symbols`. Selection preserves the
supplied order; unknown or repeated ids/names are rejected. An empty selection
is explicit, for example `--llm-symbols ''`.

Prose and examples come from the handbook and the registered examples; there
is no separately maintained LLM text or signature catalogue apart from the
short `hints`. Signatures, types, evaluation orders, intrinsic effects and the
compiler rules come directly from `--describe-language`.

Three default repair pairs start from registered rejected examples: a local
type mismatch, direct uniform access from a helper and an unknown named
argument. Their correction recipes apply only when the selected text occurs
exactly once. Generation checks each rejection's stable diagnostic code and
registered message through real `--check --diagnostics=json`, then checks,
compiles and renders the corrected program. Both sides of every style pair, and the
selected examples and demos, are also checked, compiled and rendered with seed 7 from temporary owning copies
of the registered example tree. No executable is written beside its source.

The generated file, selection configuration and source explanations/examples
are covered by the existing documentation manifest hashes. `language_docs.py
check` writes execution evidence to its report. Archive/source verification
uses the existing export schemas; this task adds no new export-schema contract.
