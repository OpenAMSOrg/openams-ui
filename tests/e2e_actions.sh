#!/usr/bin/env bash
# End-to-end check: a UI line becomes host G-code.
#
# The simulator's --action injects a line exactly the way a screen logs it. In
# live mode platform_action() maps it with actions_map() and sends the G-code
# over the Moonraker WebSocket, so a passing run means the whole path works:
# screen line -> map -> request -> wire.
#
# The mock writes every request it receives to --record, so the assertions run
# against what actually left the display, not against its own stdout.
#
# Needs the openams checkout (for mock_moonraker) and aiohttp. Skips with exit
# 0 when either is missing, so it is safe on a machine that only has the
# simulator.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SIM="$REPO_ROOT/build/openams-ui-sim"
OPENAMS_ROOT="${OPENAMS_ROOT:-$HOME/Projects/openams}"

# The idle fixture has one unit (oams1, host idx 0), two groups (T0, T1) and a
# ready bay in T0, so a load can be addressed by group and a dryer by unit.
FIXTURE="mock_moonraker/fixtures/idle.json"
SLOT_ID="oams11"                        # <unit name><bay + 1>, C2 6.2
WANT_LOAD="OPENAMS_LOAD GROUP=T0 SLOT=0"   # bay 0 of oams1, host id 0
WANT_DRY="OAMS_DRYER_START OAMS=0 TARGET=55 DURATION=14400"  # oams1's host idx; 4 h
# (A stop would be refused here: the fixture's dryer is idle, and the one
# dry-run predicate dims "dry <unit> stop" until a dryer runs.)

if [ ! -d "$OPENAMS_ROOT/tools/mock_moonraker" ]; then
    echo "SKIP: no mock_moonraker under $OPENAMS_ROOT/tools (set OPENAMS_ROOT)"
    exit 0
fi
if [ ! -f "$OPENAMS_ROOT/tools/$FIXTURE" ]; then
    echo "FAIL: the mock has no $FIXTURE to drive; the fixture is what makes" >&2
    echo "      '$SLOT_ID' and '$WANT_LOAD' the right expectations" >&2
    exit 1
fi
if ! python3 -c 'import aiohttp' >/dev/null 2>&1; then
    echo "SKIP: python3 has no aiohttp"
    exit 0
fi
if [ ! -x "$SIM" ]; then
    echo "FAIL: $SIM is missing; build first" >&2
    exit 1
fi

TMP="$(mktemp -d)"
MOCK_PID=""

cleanup() {
    if [ -n "$MOCK_PID" ] && kill -0 "$MOCK_PID" 2>/dev/null; then
        kill "$MOCK_PID" 2>/dev/null || true
        wait "$MOCK_PID" 2>/dev/null || true
    fi
    rm -rf "$TMP"
}
trap cleanup EXIT

# Let the kernel pick a free port, then hand it to the mock.
PORT="$(python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)"

echo "mock_moonraker: $OPENAMS_ROOT/tools, idle.json at --speed 0, port $PORT"
(
    cd "$OPENAMS_ROOT/tools" &&
        exec python3 -m mock_moonraker \
            --fixture "$FIXTURE" \
            --port "$PORT" --speed 0 \
            --record "$TMP/record.jsonl"
) >"$TMP/mock.log" 2>&1 &
MOCK_PID=$!

# Wait for the listen socket to accept, so the client never races the bind.
ready=""
for _ in $(seq 1 100); do
    if ! kill -0 "$MOCK_PID" 2>/dev/null; then
        echo "FAIL: mock_moonraker exited during startup:" >&2
        cat "$TMP/mock.log" >&2
        exit 1
    fi
    if python3 -c "
import socket, sys
s = socket.socket()
s.settimeout(0.2)
sys.exit(0 if s.connect_ex(('127.0.0.1', $PORT)) == 0 else 1)
" 2>/dev/null; then
        ready=1
        break
    fi
    sleep 0.1
done
if [ -z "$ready" ]; then
    echo "FAIL: mock_moonraker never accepted on port $PORT:" >&2
    cat "$TMP/mock.log" >&2
    exit 1
fi

echo "--- live run: --action 'load $SLOT_ID' --action 'dry oams1 start 55C 4h'"
"$SIM" --moonraker "ws://127.0.0.1:$PORT/websocket" --dump-model-after 2 \
    --action "load $SLOT_ID" --action "dry oams1 start 55C 4h" \
    >"$TMP/live.txt" 2>"$TMP/live.err" || {
    echo "FAIL: simulator exited non-zero" >&2
    cat "$TMP/live.err" >&2
    exit 1
}

if [ ! -s "$TMP/record.jsonl" ]; then
    echo "FAIL: the mock recorded no requests at all" >&2
    cat "$TMP/live.txt" >&2
    exit 1
fi

# The record is one JSON object per line, in request order. Print the scripts
# the display asked for, one per line.
python3 - "$TMP/record.jsonl" >"$TMP/scripts.txt" <<'PY'
import json, sys

with open(sys.argv[1]) as fh:
    for line in fh:
        line = line.strip()
        if not line:
            continue
        call = json.loads(line)
        if call.get("method") != "printer.gcode.script":
            continue
        script = call.get("params", {}).get("script")
        print(script if isinstance(script, str) else "?")
PY

echo "--- gcode the display sent:"
cat "$TMP/scripts.txt"

fail=0

if ! grep -qxF "$WANT_LOAD" "$TMP/scripts.txt"; then
    echo "FAIL: no printer.gcode.script request carried exactly '$WANT_LOAD'" >&2
    fail=1
fi
if ! grep -qxF "$WANT_DRY" "$TMP/scripts.txt"; then
    echo "FAIL: no printer.gcode.script request carried exactly '$WANT_DRY'" >&2
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "e2e_actions: FAILED"
    exit 1
fi
echo "e2e_actions: PASS"
