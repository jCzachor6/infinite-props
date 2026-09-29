#!/bin/sh
# Installs the extension for the local VS Code by symlinking it into the
# extensions directory. Reload the window afterwards.
set -e
cd "$(dirname "$0")"
DEST="$HOME/.vscode/extensions/iprop.iprop-studio"
rm -f "$DEST"
ln -s "$(pwd)" "$DEST"
echo "installed -> $DEST"
echo "reload VS Code: Cmd+Shift+P → Developer: Reload Window"
