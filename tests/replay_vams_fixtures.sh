#!/usr/bin/env bash
# Replay the recorded fixtures (real host runs)
# through the simulator and compare each model dump against a checked-in
# golden. This pins the openams_ui -> model mapping: a change that shifts a
# state, a slot or a group shows up as a diff here.
#
# Run from the repo root, with the simulator at ./build/openams-ui-sim (or
# pass its path as $1). `UPDATE=1 bash tests/replay_vams_fixtures.sh` rewrites
# the goldens instead of comparing.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SIM="${1:-$REPO_ROOT/build/openams-ui-sim}"
FIXTURE_DIR="$REPO_ROOT/tests/fixtures/recorded"

if [ ! -x "$SIM" ]; then
    echo "FAIL: $SIM is missing; build first" >&2
    exit 1
fi

# Model facts only, the same filter tests/e2e_mock_moonraker.sh uses; progress
# and step lines are allowed to differ from run to run.
filter() {
    grep -E '^(MODEL|UNIT|SLOT|GROUP) ' "$1" || true
}

fail=0
for fixture in "$FIXTURE_DIR"/*.json; do
    # U1c: tests/replay_view_goldens.sh's own goldens (<name>.view.json) sit
    # in this same directory and would otherwise be picked up as fixtures
    # (they end in .json too).
    case "$fixture" in *.view.json) continue ;; esac
    name="$(basename "$fixture" .json)"
    golden="$FIXTURE_DIR/$name.dump"
    tmp="$(mktemp)"
    if ! "$SIM" --replay "$fixture" --dump-model >"$tmp" 2>/dev/null; then
        echo "FAIL $name (simulator exited non-zero)"
        rm -f "$tmp"
        fail=1
        continue
    fi
    filter "$tmp" >"$tmp.key"
    if [ "${UPDATE:-}" = "1" ]; then
        cp "$tmp.key" "$golden"
        echo "UPDATE $name"
    elif [ ! -f "$golden" ]; then
        echo "FAIL $name (no golden $name.dump; run with UPDATE=1)"
        fail=1
    elif ! diff -u "$golden" "$tmp.key"; then
        echo "FAIL $name"
        fail=1
    else
        echo "ok $name"
    fi
    rm -f "$tmp" "$tmp.key"
done

exit "$fail"
