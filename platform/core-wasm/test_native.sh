#!/usr/bin/env bash
# Build core_wasm.c natively, with the same sources as the wasm target, and run
# test_native.c against it (nothing on the shim's path may read g_mmu).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${TMPDIR:-/tmp}/openams-core-test-native.$$"
trap 'rm -f "$OUT"' EXIT

cc -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wno-format-truncation -g -O1 \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -DFIXTURE_DIR="\"$ROOT/tests/fixtures/recorded\"" \
    -I"$ROOT" -I"$ROOT/src" \
    "$ROOT/platform/core-wasm/test_native.c" \
    "$ROOT/platform/core-wasm/core_wasm.c" \
    "$ROOT/src/core/core.c" \
    "$ROOT/src/model/mmu_model.c" \
    "$ROOT/src/backend/status_map.c" \
    "$ROOT/src/backend/actions_map.c" \
    "$ROOT/src/view/view.c" \
    "$ROOT/src/view/view_json.c" \
    "$ROOT/src/view/strings.c" \
    "$ROOT/src/lib/cjson/cJSON.c" \
    -lm -o "$OUT"
"$OUT"
