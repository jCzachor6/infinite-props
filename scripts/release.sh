#!/bin/sh
# Packages a release: a source tarball plus the built CLI binaries (when the
# toolchain is built). Usage:
#
#   scripts/release.sh [version]
#
# Produces dist/iprop-<version>.tar.gz and, when present, the binaries.

set -e
cd "$(dirname "$0")/.."

VERSION="${1:-0.1.0}"
mkdir -p dist

echo "== source tarball"
git archive --format=tar.gz --prefix="iprop-${VERSION}/" -o "dist/iprop-${VERSION}.tar.gz" HEAD
ls -la "dist/iprop-${VERSION}.tar.gz"

echo "== CLI binaries"
for tool in ipc ipgen ipsheet ipdis ipval ipa; do
    if [ -x "build/tools/$tool" ]; then
        cp "build/tools/$tool" "dist/$tool-${VERSION}"
        echo "  dist/$tool-${VERSION}"
    fi
done

echo "== done — release artifacts in dist/"
