# Generating the .ips LLM reference

The normal documentation build generates `LLM_REFERENCE.md` beside the full
handbook, and `LLM_REFERENCE_MODELS.md` for [render assets](#the-models-reference). Native SDK installation copies it into `share/iprop/`, where its
handbook/catalog/example links resolve offline. It records the product,
source-language, binary-format, C API and ABI versions separately, together
with the source commit and dirty/release status. It is generated content;
edit its source selection in `docs/llm-reference.json`.

The reference teaches the whole language and pushes one style. It opens with
the idioms chapter ([Writing idiomatic props](language/idioms.md)) and its
examples, then before/after pairs that each fix one broken rule, then the
compiler rules in plain sentences. The handbook follows: assets, syntax,
types, control flow, helpers, randomness, colors, palettes, materials,
collections, samplers, parts, surfaces, geometry and masks, execution limits
and the grammar. Every builtin is listed, and checked repairs close the
document. For source language 1.0 the default document has a **131,072-byte
UTF-8 budget (128 KiB)**, with the same maximum allowed budget, and covers
materials, surfaces, geometry, masks and typed parts. The 0.7 release used
102,400 bytes (100 KiB).
Generation fails before replacing
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
`control`, `helper`, `random`, `colors`, `palette`, `materials`, `collections`,
`samplers`, `part`, `surfaces`, `procedural`, `limits` and `grammar`; `style`, `builtins`, `symbols` and `diagnostics` are generated.
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
Material libraries registered with `check_expect: pass` are checked directly
and included as source; their importing asset is compiled and rendered. They
never require a standalone executable.

The generated file, selection configuration and source explanations/examples
are covered by the existing documentation manifest hashes. `language_docs.py
check` writes execution evidence to its report. Archive/source verification
uses the existing export schemas; this task adds no new export-schema contract.

## The models reference

`LLM_REFERENCE_MODELS.md` teaches [models](language/models.md), the assets
with `render`. It is a second document with its own selection,
`docs/llm-reference-models.json`, and its own budget of **65,536 bytes**
(64 KiB), so the first reference keeps its size and an agent that writes
only images reads nothing about 3D. It builds on the first one: the language
itself, and skins, which are ordinary draw assets, are taught there.

Both documents come from the same renderer and the same checks. A selection
file may carry a `document` object that adapts it:

| Field | Meaning |
| --- | --- |
| `title` | The heading of the document |
| `preface` | The opening paragraphs |
| `links` | Further links beside the handbook and the catalog, each a `label` and a `path` inside the documentation tree |
| `asset` | `draw` (the default) or `render`: whose builtins the `builtins` and `symbols` sections list and whose compiler rules are generated |
| `lead` | The section ids that come before the compiler rules; `idioms` and `style` by default |

With `"asset": "render"` the builtin list holds the builtins that only a
render asset has, followed by the overloads it adds to 2D builtins, and the
compiler rules are sentences generated from `source_rules.models`: the entry
and its declarations, the axes, the two types, the arguments and order of
`place`, the skin call, the defaults of a new skin, the limits and the 2D
builtins a render asset cannot call. The first reference lists no builtin of
render assets.

The models reference opens with the chapter's introduction, its complete
example and the rules of style with their before/after pairs. Its sections
are `models`, `writing`, `vectors`, `skins`, `primitives`, `placing`,
`limits` and `tools`. The `--llm-sections`, `--llm-symbols` and
`--llm-max-bytes` options select the first reference only; the second is
always built from its file's defaults. Its examples, repairs and style pairs
are checked, compiled and generated like the others, and the manifest owns
both documents.
