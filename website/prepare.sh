#!/bin/sh
# Syncs the playground assets from the repository:
#   the WASM module (the html5 backend build) and the example .iprop files.
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
cp examples/portrait.iprop website/public/portrait.iprop
cp examples/plant.iprop website/public/plant.iprop
"$tools_dir/ipc" backends/html5/demo/assets/noise.ips -o website/public/noise.iprop
mkdir -p website/public/spec
cp spec/iprop-v0.1.md spec/iprop-v0.2.md spec/iprop-v0.3.md spec/iprop-v0.4.md website/public/spec/
mkdir -p website/public/reference website/public/recipes
cp docs/language-v0.4.md docs/optimizations-v0.4.md docs/optimization-acceptance-v0.4.md website/public/reference/
cp examples/recipes/*.ips examples/recipes/README.md website/public/recipes/

echo "playground assets synced"
