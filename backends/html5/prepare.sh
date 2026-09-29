#!/bin/sh
# Syncs the demo and conformance assets from the repository:
#   examples/portrait.iprop          -> demo/ + test/assets/
#   tests/golden/noise.ipa           -> test/assets/noise.iprop
#   tests/golden/shapes.ipa          -> test/assets/shapes.iprop
# Run from the repository root.

set -e

cd "$(dirname "$0")/../.."
mkdir -p backends/html5/demo backends/html5/test/assets

cp examples/portrait.iprop backends/html5/demo/portrait.iprop
cp examples/portrait.iprop backends/html5/test/assets/portrait.iprop

./build/tools/ipa tests/golden/noise.ipa -o backends/html5/test/assets/noise.iprop
./build/tools/ipa tests/golden/shapes.ipa -o backends/html5/test/assets/shapes.iprop

cp backends/html5/test/assets/noise.iprop backends/html5/demo/noise.iprop
cp backends/html5/test/assets/shapes.iprop backends/html5/demo/shapes.iprop

echo "assets synced"
