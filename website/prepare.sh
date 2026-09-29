#!/bin/sh
# Syncs the playground assets from the repository:
#   the WASM module (the html5 backend build) and the example .iprop files.
# Run from the repository root: website/prepare.sh
set -e
cd "$(dirname "$0")/.."
mkdir -p website/public

cp backends/html5/demo/iprop.js backends/html5/demo/iprop.wasm website/public/
cp examples/portrait.iprop website/public/portrait.iprop
cp examples/plant.iprop website/public/plant.iprop
cp backends/html5/demo/noise.iprop website/public/noise.iprop
mkdir -p website/public/spec
cp spec/iprop-v0.1.md website/public/spec/iprop-v0.1.md

echo "playground assets synced"
