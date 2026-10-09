#!/bin/sh
# Syncs the website assets from the repository: the WASM module (the html5
# backend build), the home page showcase binaries from examples/showcase, the
# specifications and the recipes.
# Run from the repository root: sh website/prepare.sh [wasm-build] [tools]
set -eu
cd "$(dirname "$0")/.."
if [ "$#" -gt 2 ]; then
  echo "usage: sh website/prepare.sh [wasm-build-directory] [native-tools-directory]" >&2
  exit 1
fi
wasm_dir=${1:-build/wasm}
tools_dir=${2:-build/tools}
for asset in "$wasm_dir/iprop.js" "$wasm_dir/iprop.wasm" "$tools_dir/ipc"; do
  if [ ! -f "$asset" ]; then
    echo "website: missing $asset; build the WASM module and native compiler first" >&2
    exit 1
  fi
done
mkdir -p website/public

cp "$wasm_dir/iprop.js" "$wasm_dir/iprop.wasm" website/public/
mkdir -p website/public/showcase
cp examples/showcase/*.iprop examples/showcase/LICENSE.md website/public/showcase/
mkdir -p website/public/spec
cp spec/iprop-v0.1.md spec/iprop-v0.2.md spec/iprop-v0.3.md spec/iprop-v0.4.md spec/iprop-v0.5.md website/public/spec/
mkdir -p website/public/reference website/public/recipes
cp docs/optimizations-v0.4.md docs/optimization-acceptance-v0.4.md website/public/reference/
cp examples/recipes/*.ips examples/recipes/README.md website/public/recipes/

echo "website assets synced"
