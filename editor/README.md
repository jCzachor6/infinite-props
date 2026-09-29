# iprop studio — the .ips editor

A standalone desktop editor for `.ips` assets (Electron + CodeMirror +
xterm), with the real compiler wired in for inline diagnostics and an
**embedded terminal** where CLI agents (opencode, codex, copilot) edit the
project files directly. Files live on disk in a project folder — the studio
is an authoring surface, not a sandbox.

## Features

- **Project folders**: open a directory, edit every `.ips` file in it.
- **Editor**: CodeMirror with `.ips` syntax highlighting, tabs, dirty marks,
  Ctrl/Cmd+S saving.
- **Diagnostics**: every edit recompiles through the native `ipc` binary
  (the exact CLI compiler); errors appear as gutter squiggles with
  `file:line:column` locations, including errors in imported parts.
- **Terminal**: an xterm panel (toggle the bar at the bottom) running your
  shell in the project folder — start `opencode .` / `codex` there and watch
  the studio reload the files they change.

## Requirements

- Node.js ≥ 20, npm.
- The compiled compiler: `build/tools/ipc` in the repository root (build the
  desktop toolchain first: `cmake --build build`). Override the location
  with the `IPROP_IPC` environment variable.

## Run

```sh
cd editor
npm install
# one-time, on this machine:
npm install-scripts approve node-pty esbuild electron
npm run rebuild        # node-pty for the Electron ABI (node-gyp 11 is
                       # used, so Python 3.12+ works without distutils)
# macOS Gatekeeper blocks the npm-downloaded Electron binary; lift the
# quarantine once:
xattr -dr com.apple.quarantine node_modules/electron/dist/Electron.app
npm run dev            # build the renderer and launch the app
```

## Layout

| Path | What |
| ---- | ---- |
| `electron/main.cjs` | the main process: the window, file operations, the `ipc` bridge, the terminal (node-pty) |
| `electron/preload.cjs` | the typed `window.studio` bridge |
| `src/main.ts` | the renderer: project list, tabs, the editor, diagnostics |
| `src/language.ts` | the `.ips` CodeMirror language (mirrors the real lexer's tokens) |
| `index.html` | the layout |

## Notes

- The compile button (and the debounced auto-compile) treats the active tab
  as the entry asset; imports resolve relative to the project folder.
- The terminal runs the shell from your `SHELL`; agents editing files from
  the terminal are picked up on the next compile (the editor does not yet
  watch external changes — save or edit in the studio to refresh).
