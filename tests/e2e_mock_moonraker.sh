#!/usr/bin/env bash
# End-to-end check: the simulator's WebSocket client against the real
# mock_moonraker server (from the openams checkout), replaying the same
# `loading` fixture that --replay applies locally.
#
# The point is the transport: the mock speaks Moonraker's JSON-RPC over a real
# TCP WebSocket, so a passing run means ws_client.c's handshake, framing and
# reassembly all work against something that is not our own reader.
#
# Needs the openams checkout (for mock_moonraker) and aiohttp. Skips with exit
# 0 when either is missing, so it is safe on a machine that only has the
# simulator.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SIM="$REPO_ROOT/build/openams-ui-sim"
FIXTURE="$REPO_ROOT/tests/fixtures/loading.json"
OPENAMS_ROOT="${OPENAMS_ROOT:-$HOME/Projects/openams}"

if [ ! -d "$OPENAMS_ROOT/tools/mock_moonraker" ]; then
    echo "SKIP: no mock_moonraker under $OPENAMS_ROOT/tools (set OPENAMS_ROOT)"
    exit 0
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

echo "mock_moonraker: $OPENAMS_ROOT/tools, loading.json at --speed 0, port $PORT"
(
    cd "$OPENAMS_ROOT/tools" &&
        exec python3 -m mock_moonraker \
            --fixture mock_moonraker/fixtures/loading.json \
            --port "$PORT" --speed 0
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

echo "--- live run: --moonraker ws://127.0.0.1:$PORT/websocket --dump-model-after 2"
"$SIM" --moonraker "ws://127.0.0.1:$PORT/websocket" --dump-model-after 2 \
    >"$TMP/live.txt" 2>"$TMP/live.err" || {
    echo "FAIL: simulator exited non-zero" >&2
    cat "$TMP/live.err" >&2
    exit 1
}

echo "--- replay run: --replay tests/fixtures/loading.json --dump-model"
"$SIM" --replay "$FIXTURE" --dump-model >"$TMP/replay.txt" 2>"$TMP/replay.err" || {
    echo "FAIL: replay run exited non-zero" >&2
    cat "$TMP/replay.err" >&2
    exit 1
}

# Compare only the model facts, not the progress/step noise: the live link
# resolves timers against the wall clock, so intermediate fields are allowed
# to differ, but what the loading fixture describes must be identical.
filter() {
    grep -E '^(MODEL|UNIT|SLOT|GROUP) ' "$1"
}

filter "$TMP/live.txt" >"$TMP/live.key"
filter "$TMP/replay.txt" >"$TMP/replay.key"

echo "--- live model:"
cat "$TMP/live.key"

fail=0

if ! grep -q 'busy=load' "$TMP/live.key"; then
    echo "FAIL: live model does not report busy=load" >&2
    fail=1
fi
if ! grep -q '^UNIT 0 name=oams1' "$TMP/live.key"; then
    echo "FAIL: live model has no UNIT 0 name=oams1" >&2
    fail=1
fi

# The loading fixture's oams_manager.settings are all-defaults-true except
# apply_pa_on_load; the live run must mirror them once the subscribe result
# arrives.
if ! grep -q 'SETTINGS known=1 rti=1 rts=1 aro=1 acs=1 pa=0' "$TMP/live.txt"; then
    echo "FAIL: live model settings do not match fixture (expected SETTINGS known=1 rti=1 rts=1 aro=1 acs=1 pa=0)" >&2
    grep '^SETTINGS' "$TMP/live.txt" || echo "(no SETTINGS line in output)" >&2
    fail=1
else
    echo "OK: live model settings match fixture (known=1 rti=1 rts=1 aro=1 acs=1 pa=0)"
fi

if ! diff -u "$TMP/replay.key" "$TMP/live.key" >"$TMP/diff.txt"; then
    echo "MISMATCH: live model differs from the replayed fixture:" >&2
    cat "$TMP/diff.txt" >&2
    fail=1
else
    echo "OK: live model matches the replayed fixture on MODEL/UNIT/SLOT/GROUP"
fi

if [ "$fail" -ne 0 ]; then
    echo "e2e_mock_moonraker: FAILED"
    exit 1
fi
echo "e2e_mock_moonraker: PASS"
