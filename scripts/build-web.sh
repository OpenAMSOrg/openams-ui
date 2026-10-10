#!/usr/bin/env bash
# Builds the Emscripten/WebAssembly preview and assembles a static site
# under build-web/site/: index.html, the wasm module's glue .js, the .wasm
# itself, and .nojekyll (so GitHub Pages serves the underscore-prefixed
# Emscripten runtime files, if any, without treating them as Jekyll input).
#
# Usage: scripts/build-web.sh
#   JOBS=<n>   build parallelism (default: nproc, capped at 8).
#
# Requires an activated Emscripten SDK: `source <emsdk>/emsdk_env.sh` first
# (emcmake/emcc/em++ must resolve). Install one with
# `emsdk install latest && emsdk activate latest` if you have not already.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

if ! command -v emcmake >/dev/null 2>&1; then
    echo "build-web.sh: emcmake not on PATH — source <emsdk>/emsdk_env.sh first" >&2
    exit 1
fi

JOBS="${JOBS:-$(( $(nproc 2>/dev/null || echo 4) > 8 ? 8 : $(nproc 2>/dev/null || echo 4) ))}"

BUILD_DIR="$REPO_ROOT/build-web"
SITE_DIR="$BUILD_DIR/site"

emcmake cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --target openams-ui-web -j"$JOBS"

JS="$BUILD_DIR/openams-ui-web.js"
WASM="$BUILD_DIR/openams-ui-web.wasm"

[ -f "$JS" ]   || { echo "build-web.sh: build did not produce $JS" >&2; exit 1; }
[ -f "$WASM" ] || { echo "build-web.sh: build did not produce $WASM" >&2; exit 1; }

mkdir -p "$SITE_DIR"
cp "$JS" "$WASM" "$SITE_DIR/"
cp "$REPO_ROOT/platform/web/index.html" "$SITE_DIR/index.html"
touch "$SITE_DIR/.nojekyll"

echo "Wrote $SITE_DIR"
echo "  $(du -h "$SITE_DIR/openams-ui-web.js"   | cut -f1)  openams-ui-web.js"
echo "  $(du -h "$SITE_DIR/openams-ui-web.wasm" | cut -f1)  openams-ui-web.wasm"
