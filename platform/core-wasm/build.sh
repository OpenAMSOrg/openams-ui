#!/usr/bin/env bash
# Build the openams-core WebAssembly module and copy it next to the JS module:
# web/core/dist/openams-core.js + openams-core.wasm (git-ignored).
#
# Usage: platform/core-wasm/build.sh
#   BUILD_DIR=<dir>   build directory (default build-web/core-wasm, which is git-ignored)
#
# Needs an activated Emscripten SDK: `source ~/emsdk/emsdk_env.sh` first.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build-web/core-wasm}"
DIST="$REPO_ROOT/web/core/dist"

if ! command -v emcmake >/dev/null 2>&1; then
    echo "build.sh: emcmake not on PATH - source <emsdk>/emsdk_env.sh first" >&2
    exit 1
fi

emcmake cmake -S "$REPO_ROOT/platform/core-wasm" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --target openams-core -j2

mkdir -p "$DIST"
cp "$BUILD_DIR/openams-core.js" "$BUILD_DIR/openams-core.wasm" "$DIST/"
echo "Wrote $DIST/openams-core.js and openams-core.wasm"
