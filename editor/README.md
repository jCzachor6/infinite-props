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

## Install

```sh
cd editor
sh install.sh          # symlinks the extension into ~/.vscode/extensions/
```

Then reload the window (Cmd+Shift+P → *Developer: Reload Window*). Open any
`.ips` file — the highlighting and diagnostics activate automatically.

## Requirements

- The compiled compiler: `build/tools/ipc` in the workspace root (build the
  desktop toolchain: `cmake --build build`). Override the location with the
  `iprop.compilerPath` setting.

## Layout

| Path | What |
| ---- | ---- |
| `extension.js` | activation: the diagnostic collection, the compile command, the watchers |
| `syntaxes/ips.tmLanguage.json` | the TextMate grammar |
| `language-configuration.json` | comments and brackets |
| `install.sh` | local installation via symlink |
