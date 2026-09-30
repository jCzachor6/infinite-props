#!/bin/sh
# Build/tests must already be complete. Usage:
#   scripts/release.sh [version] [build-directory] [--check]
# --check validates an installed package without creating release archives.
set -eu
cd "$(dirname "$0")/.."
exec python3 scripts/release.py "$@"
