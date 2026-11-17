#!/bin/sh
# Syncs the website assets from the repository: the WASM modules (the html5
# backend build), the home page showcase binaries from examples/showcase, the
# chair of the models section, the specifications and the recipes.
# Run from the repository root: sh website/prepare.sh [wasm-build] [tools]
set -eu
cd "$(dirname "$0")/.."
if [ "$#" -gt 2 ]; then
  echo "usage: sh website/prepare.sh [wasm-build-directory] [native-tools-directory]" >&2
  exit 1
fi
wasm_dir=${1:-build/wasm}
tools_dir=${2:-build/tools}
for asset in "$wasm_dir/iprop.js" "$wasm_dir/iprop.wasm" "$wasm_dir/iprop3d.js" "$wasm_dir/iprop3d.wasm" "$tools_dir/ipc"; do
  if [ ! -f "$asset" ]; then
    echo "website: missing $asset; build the WASM module and native compiler first" >&2
    exit 1
  fi
done
mkdir -p website/public

cp "$wasm_dir/iprop.js" "$wasm_dir/iprop.wasm" website/public/
# The models section (a preview): the model module with its API and worker,
# the chair's sources for the page, and the chair compiled here, because the
# model format may still change between releases.
cp "$wasm_dir/iprop3d.js" "$wasm_dir/iprop3d.wasm" website/public/
cp backends/html5/js/api3d.js backends/html5/js/model-worker.js website/public/
mkdir -p website/public/models/skins
cp examples/models/chair.ips website/public/models/
cp examples/models/skins/oak.ips examples/models/skins/weave.ips website/public/models/skins/
"$tools_dir/ipc" examples/models/chair.ips -o website/public/models/chair.iprop3d > /dev/null
mkdir -p website/public/showcase
cp examples/showcase/*.iprop examples/showcase/LICENSE.md website/public/showcase/
cp examples/showcase/butterfly-benchmark.json examples/showcase/showcase-manifest.json examples/showcase/catemon.json website/public/showcase/
mkdir -p website/public/spec
for old in website/public/spec/iprop-v*.md website/public/reference/optimizations*.md website/public/reference/optimization-acceptance*.md; do
  if [ -f "$old" ]; then rm -f "$old"; fi
done
cp spec/*.md website/public/spec/
mkdir -p website/public/recipes
cp examples/recipes/*.ips examples/recipes/README.md website/public/recipes/

echo "website assets synced"
