# iprop studio — a VS Code extension

The `.ips` authoring tools as a VS Code extension: syntax highlighting and
**inline diagnostics from the real compiler**. The integrated terminal is
your console — run `opencode`/`codex` there; they edit the `.ips` files on
disk and the extension recompiles on every change (a file watcher picks up
external edits).

## Features

- `.ips` syntax highlighting (a TextMate grammar mirroring the lexer's
  tokens: keywords, type names, colour/number literals, comments, strings,
  operators).
- Diagnostics: a debounced full check through the native `ipc` binary on
  edits, saves and external file changes. JSON diagnostics retain exclusive
  source ranges, stable error codes, severity and related locations, including
  imported parts. Checks with compiler 0.5 write no artifacts.
- The active `.ips` file is treated as the entry asset; imports resolve
  relative to its folder.
- **Format Document** uses the real 0.5 `ipc --format-only` pipeline and returns
  one canonical text edit. Save IPS buffers first; formatting leaves the entry
  on disk unchanged and writes no binary.
- **Live preview** (Cmd+Shift+P → *iprop: Open Preview*): a side panel with
  the pixel-exact render, seed/variant/archetype sliders, uniform editors
  for the declared inputs, the fuel readout and the structured outputs. A
  debounced compile-on-edit refreshes it; compile errors show as an overlay.
- **Atlas** (*iprop: Open Atlas*): a grid of every variant × archetype cell
  at the current seed; clicking a cell drives the preview.
- **Bytecode inspector** (*iprop: Open Bytecode Inspector*): the `ipdis
  --source` disassembly with the debug section's source map — the editor
  cursor highlights the matching instructions, and clicking an instruction
  jumps the editor to its source line. The inspector explicitly compiles
  with `--debug`; binary exports use the stripped production default.
- **Export** (*iprop: Export .iprop / PNG / Variant Sheet*): one-click
  exports — the `.iprop` is validated with `ipval` after saving, the PNG
  uses the current seed/variant/archetype, the sheet grids the variants ×
  a chosen seed count through `ipsheet`.
- **Imports** (*iprop: Open Import…*): lists the imports of the active file
  and opens the picked part; missing parts surface as compiler errors with
  the file and line.

## Compiler diagnostics and quick fixes

The editor requests `ipc <entry> --check --diagnostics=json` and reads schema 1
from stdout. Compiler positions remain UTF-8 byte offsets and one-based Unicode
codepoint columns until they are mapped to VS Code's UTF-16 positions. Tabs count
as one column; CRLF and astral characters retain their exact source bytes.
The boundary also handles VS Code's BOM removal and mixed-line-ending
normalization while keeping the compiler's hash tied to the original disk bytes.
Invalid UTF-8 remains a native lexer error shown at its point location, since its
raw byte range cannot be represented by the decoded editor text.
Errors, warnings and notes have their own Problems severity, and related
locations link to their owning source file. Unlocated CLI or I/O errors appear
at the entry's beginning.

The compiler reads saved files. Save an edited buffer to check its new contents;
the extension clears old diagnostics rather than attaching disk errors to a
different buffer. Source identities are SHA-256 of the exact UTF-8 bytes,
including line endings. A completed run cannot replace a newer run, and edit,
save, close, create and delete events invalidate pending results immediately.
Successful checks clear earlier errors in imported files too.

Quick fixes use the compiler's explicit `fix_its`, grouped into one transaction.
The extension checks the source hash when showing the action and again when
executing it. Overlapping edits, inconsistent ranges or changed source are
rejected. Fixes touching a BOM are unsupported because it is encoding metadata
outside the text model. `TextEditor.edit` binds the edit to the document version, so a concurrent
edit rejects the transaction. A fix changes the buffer and does not save it.
Suggestions without an exact fix are informational actions; the editor does not
infer replacements from diagnostic messages.

Diagnostics require a compiler with `--check --diagnostics=json` (language 0.5 or newer).
An older compiler, a timeout, invalid JSON or a mismatched JSON status is
reported in the **iprop** output channel; the editor never retries another way.

Run the editor tests with the current native tools:

```sh
IPROP_COMPILER="$PWD/build/tools/ipc" node --test editor/test/*.test.js
```

## Canonical formatting

Use VS Code's **Format Document** action with compiler 0.5. The formatter reads
saved files, so the entry and other open IPS buffers must be saved. It returns
an edit for the buffer; it does not save the edit. A compiler without
`--format-only` reports an error and the buffer is left unchanged.

The temporary source output is exclusively reserved in the entry's physical
parent directory, preserving the meaning of relative imports when the edit is
applied back to the entry. Its suffix is `.tmp`; the watcher also ignores the
compiler's reserved `.ipc-format-<number>-<number>` staging directories. It
continues to react to ordinary IPS changes, including imported files.

Cancellation and source changes invalidate pending results. The provider checks
document versions/text, exact saved-file hashes, the compiler setting and the
executable's path/hash before returning an edit. It waits for process close
before removing its own temporary output, including on failure or cancellation;
it never removes other processes' staging directories. A compiler killed during
its atomic write may leave its own staging directory. Each request uses a
15-second compiler timeout and a 4 MiB formatted-source/log ceiling.

## Keyboard shortcuts

| Keys | Action |
| ---- | ------ |
| `Alt+P` | open the preview |
| `Alt+A` | open the atlas |
| `Alt+I` | open the bytecode inspector |
| `Alt+E` | export the `.iprop` |
| `Alt+Shift+E` | export the variant sheet |
| `Cmd/Ctrl+S` | save (native) |

## Install

```sh
cd editor
sh install.sh          # symlinks the extension into ~/.vscode/extensions/
```

Then reload the window (Cmd+Shift+P → *Developer: Reload Window*). Open any
`.ips` file — the highlighting and diagnostics activate automatically.

## Requirements

- The compiled tools: `build/tools/ipc`, `ipgen`, `ipdis`, `ipval` and
  `ipsheet` in the workspace root (build the desktop toolchain:
  `cmake --build build`). Override the locations with the `iprop.*Path`
  settings.

## Layout

| Path | What |
| ---- | ---- |
| `extension.js` | activation, diagnostic mapping, guarded code actions and watchers |
| `diagnostics.js` | JSON validation, compiler transport and source-bound edit checks |
| `formatting.js` | compiler identity and owned temporary source output for formatting |
| `syntaxes/ips.tmLanguage.json` | the TextMate grammar |
| `language-configuration.json` | comments and brackets |
| `install.sh` | local installation via symlink |

JSON diagnostics use schema 1 and the shared `contracts.js` reader. Unknown
additive fields are allowed; missing fields, unknown schema versions and
status/exit mismatches are rejected. UTF-8/source-id checks still guard every
applied fix.
The same real diagnostic mutation corpus is checked by Python, the editor and
the formal schema in `compiler_export_contracts`.
