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
- Diagnostics: a debounced recompile through the native `ipc` binary on
  every edit and on external file changes; errors appear as squiggles and
  in the Problems panel with `file:line:column` locations — including
  errors in imported parts.
- The active `.ips` file is treated as the entry asset; imports resolve
  relative to its folder.
- **Live preview** (Cmd+Shift+P → *iprop: Open Preview*): a side panel with
  the pixel-exact render, seed/variant/archetype sliders, uniform editors
  for the declared inputs, the fuel readout and the structured outputs. A
  debounced compile-on-edit refreshes it; compile errors show as an overlay.
- **Atlas** (*iprop: Open Atlas*): a grid of every variant × archetype cell
  at the current seed; clicking a cell drives the preview.
- **Bytecode inspector** (*iprop: Open Bytecode Inspector*): the `ipdis
  --source` disassembly with the debug section's source map — the editor
  cursor highlights the matching instructions, and clicking an instruction
  jumps the editor to its source line.
- **Export** (*iprop: Export .iprop / PNG / Variant Sheet*): one-click
  exports — the `.iprop` is validated with `ipval` after saving, the PNG
  uses the current seed/variant/archetype, the sheet grids the variants ×
  a chosen seed count through `ipsheet`.
- **Imports** (*iprop: Open Import…*): lists the imports of the active file
  and opens the picked part; missing parts surface as compiler errors with
  the file and line.

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
| `extension.js` | activation: the diagnostic collection, the compile command, the watchers |
| `syntaxes/ips.tmLanguage.json` | the TextMate grammar |
| `language-configuration.json` | comments and brackets |
| `install.sh` | local installation via symlink |
