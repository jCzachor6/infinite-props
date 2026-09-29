#!/bin/sh
# Syncs the demo and conformance assets from the repository:
#   examples/portrait.iprop          -> demo/ + test/assets/
#   examples/plant.iprop             -> demo/
#   tests/golden/noise.ipa           -> test/assets/noise.iprop
#   tests/golden/shapes.ipa          -> test/assets/shapes.iprop
# Run from the repository root.

set -e

cd "$(dirname "$0")/../.."
mkdir -p backends/html5/demo backends/html5/test/assets

# Demo assets: the portrait and the interior plant (pot + flower parts)
# from examples, plus a seed-driven noise field authored in demo/assets/.
# The .ips sources of the composed examples are mirrored into demo/assets/.
cp examples/portrait.iprop backends/html5/demo/portrait.iprop
cp examples/plant.iprop backends/html5/demo/plant.iprop
./build/tools/ipc backends/html5/demo/assets/noise.ips -o backends/html5/demo/noise.iprop

# Conformance assets stay the golden fixtures (seed-independent by design).
cp examples/portrait.iprop backends/html5/test/assets/portrait.iprop
./build/tools/ipa tests/golden/noise.ipa -o backends/html5/test/assets/noise.iprop
./build/tools/ipa tests/golden/shapes.ipa -o backends/html5/test/assets/shapes.iprop

echo "assets synced"
