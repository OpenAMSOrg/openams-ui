#!/usr/bin/env bash
# Replay the recorded fixtures through the simulator's --dump-view (the
# view builders' own JSON, through core_apply so pressure/set_point reach the
# model too - see replay_apply_via_core() in platform/sim/main.c) and compare
# each dump against a checked-in golden (tests/fixtures/recorded/<name>.view.json,
# pretty-printed). This pins the builders' output: a change to a label, a
# state, a color or an action shows up as a diff here.
#
# Run from the repo root, with the simulator at ./build/openams-ui-sim (or
# pass its path as $1). `UPDATE=1 bash tests/replay_view_goldens.sh` rewrites
# the goldens instead of comparing - read every rewritten golden by hand
# against docs/design/UNIFIED_UI.md 4 before committing it.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SIM="${1:-$REPO_ROOT/build/openams-ui-sim}"
FIXTURE_DIR="$REPO_ROOT/tests/fixtures/recorded"

if [ ! -x "$SIM" ]; then
    echo "FAIL: $SIM is missing; build first" >&2
    exit 1
fi

# The goldens were recorded from a build carrying every language, and the
# settings view lists the languages of the image, so a build configured with
# -DMMU_LANGUAGES dumps a shorter list and would fail on it alone. CTest sets
# MMU_LANGUAGES for this test; when it names a set, that one list is dropped
# from both sides before the diff and nothing else is touched.
restricted=""
if [ -n "${MMU_LANGUAGES:-}" ] && [ "${MMU_LANGUAGES}" != "all" ]; then
    restricted="yes"
fi

# $1 read, $2 written: the view JSON with the language row's options emptied.
without_languages() {
    python3 - "$1" "$2" <<'PY'
import json, sys

def strip(node):
    if isinstance(node, dict):
        if node.get("key") == "language":
            node["options"] = []
        for value in node.values():
            strip(value)
    elif isinstance(node, list):
        for value in node:
            strip(value)

with open(sys.argv[1], encoding="utf-8") as f:
    doc = json.load(f)
strip(doc)
with open(sys.argv[2], "w", encoding="utf-8") as f:
    json.dump(doc, f, indent="\t", ensure_ascii=False)
PY
}

fail=0
if [ -n "$restricted" ]; then
    echo "note: MMU_LANGUAGES=$MMU_LANGUAGES - comparing without the language list"
fi
for fixture in "$FIXTURE_DIR"/*.json; do
    # Skip our own goldens: they live in this same directory and end in
    # .json too.
    case "$fixture" in *.view.json) continue ;; esac
    name="$(basename "$fixture" .json)"
    golden="$FIXTURE_DIR/$name.view.json"
    tmp="$(mktemp)"
    if ! "$SIM" --replay "$fixture" --dump-view >"$tmp" 2>/dev/null; then
        echo "FAIL $name (simulator exited non-zero)"
        rm -f "$tmp"
        fail=1
        continue
    fi
    reference="$golden"
    actual="$tmp"
    if [ -n "$restricted" ]; then
        without_languages "$golden" "$tmp.golden"
        without_languages "$tmp" "$tmp.actual"
        reference="$tmp.golden"
        actual="$tmp.actual"
    fi
    if [ "${UPDATE:-}" = "1" ]; then
        cp "$tmp" "$golden"
        echo "UPDATE $name"
    elif [ ! -f "$golden" ]; then
        echo "FAIL $name (no golden $name.view.json; run with UPDATE=1)"
        fail=1
    elif ! diff -u "$reference" "$actual"; then
        echo "FAIL $name"
        fail=1
    else
        echo "ok $name"
    fi
    rm -f "$tmp" "$tmp.golden" "$tmp.actual"
done

exit "$fail"
