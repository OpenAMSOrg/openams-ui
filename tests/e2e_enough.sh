#!/usr/bin/env bash
# End-to-end check: job metadata → enough-filament calculation.
#
# A fixture supplies file_metadata for cube.gcode (T0 needs 200 g) and a
# component_status giving T0's only slot 100 g.  The simulator requests the
# metadata via server.files.metadata, parses filament_weights, and the model
# dump shows T0 short.  Three assertions must pass:
#
#   1. server.files.metadata call is recorded.
#   2. The dump's JOB line reports need=200,... (T0 needs 200 g).
#   3. The dump's JOB line reports short=0 (tool index 0 is short).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SIM="$REPO_ROOT/build/openams-ui-sim"
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

# Fixture: one unit (oams1), group T0 = bay oams1-0, remaining 100 g (< 200 needed).
cat >"$TMP/enough.json" <<'FIXTURE'
{
  "scenario": "enough_e2e",
  "seed": 22222222,
  "family_map": { "oams1": "ams1" },
  "file_metadata": {
    "cube.gcode": {
      "filament_weights": [200]
    }
  },
  "events": [
    {
      "t": 0.0,
      "kind": "c2_status",
      "unit": null,
      "payload": {
        "schema_version": 2,
        "api_version": 1,
        "schema": "openams.manager",
        "ready": true,
        "current_group": null,
        "lanes": [
          {
            "id": "fps",
            "state": "unloaded",
            "current_group": null,
            "current_slot": null,
            "following": false,
            "direction": 1,
            "message": null,
            "pressure": 0.0,
            "set_point": 0.5
          }
        ],
        "lanes_by_fps": {
          "fps": {
            "op": "UNLOADED",
            "group": null,
            "unit": null,
            "runout": "idle",
            "following": false,
            "since": 1500.0,
            "message": null,
            "direction": 1,
            "pressure": 0.0
          }
        },
        "units": [
          {
            "name": "oams1",
            "host_idx": 0,
            "bay_count": 4,
            "bays": [
              {"id": "oams1-0", "state": "unloaded", "spool_id": null, "filament": null,
               "group": "T0", "source": "none", "position": null},
              {"id": "oams1-1", "state": "unloaded", "spool_id": null, "filament": null,
               "group": null, "source": "none", "position": null},
              {"id": "oams1-2", "state": "unloaded", "spool_id": null, "filament": null,
               "group": null, "source": "none", "position": null},
              {"id": "oams1-3", "state": "unloaded", "spool_id": null, "filament": null,
               "group": null, "source": "none", "position": null}
            ],
            "dryer": null
          }
        ],
        "groups": [
          {"name": "T0", "bays": ["oams1-0"]},
          {"name": "T1", "bays": ["oams1-1"]}
        ],
        "version": "1.0.0"
      }
    },
    {
      "t": 0.0,
      "kind": "openams_ui",
      "unit": "oams1",
      "payload": {
        "v": 1,
        "unit": "oams1",
        "idx": 0,
        "family": "ams1",
        "online": true,
        "ready": true,
        "cal": "c",
        "lane": {
          "id": "fps", "ext": "extruder", "op": "UNLOADED",
          "group": null, "bay": null, "stage": null, "runout": "idle",
          "from": null, "to": null, "ms": true
        },
        "bays": [
          {"id": 0, "s": "r", "cal": "c", "a": "lca", "sp": {"id": 5, "src": "t"}},
          {"id": 1, "s": "e", "cal": "c", "a": "",    "sp": null},
          {"id": 2, "s": "e", "cal": "c", "a": "",    "sp": null},
          {"id": 3, "s": "e", "cal": "c", "a": "",    "sp": null}
        ],
        "groups": [
          {"n": "T0", "b": ["oams1-0"], "s": "r"},
          {"n": "T1", "b": ["oams1-1"], "s": "e"}
        ],
        "env": null,
        "dryer": null,
        "fault": null,
        "nf": 0,
        "act": []
      }
    },
    {
      "t": 0.0,
      "kind": "component_status",
      "unit": null,
      "payload": {
        "schema_version": 1,
        "spoolman_online": false,
        "queued_writes": 0,
        "bays": {
          "oams1-0": {
            "state": "linked",
            "spool_id": 5,
            "source": "tag",
            "pending_confirmation": false,
            "error": null,
            "spool": {
              "material": "PLA",
              "color_hex": "FF0000",
              "vendor": "Bambu Lab",
              "remaining_g": 100,
              "remaining_pct": 10
            }
          },
          "oams1-1": {
            "state": "unlinked", "spool_id": null, "source": "none",
            "pending_confirmation": false, "error": null, "spool": null
          },
          "oams1-2": {
            "state": "unlinked", "spool_id": null, "source": "none",
            "pending_confirmation": false, "error": null, "spool": null
          },
          "oams1-3": {
            "state": "unlinked", "spool_id": null, "source": "none",
            "pending_confirmation": false, "error": null, "spool": null
          }
        }
      }
    }
  ]
}
FIXTURE

PORT="$(python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)"

echo "mock_moonraker: enough.json, port $PORT"
(
    cd "$OPENAMS_ROOT/tools" &&
        exec python3 -m mock_moonraker \
            --fixture "$TMP/enough.json" \
            --port "$PORT" --speed 0 \
            --record "$TMP/record.jsonl"
) >"$TMP/mock.log" 2>&1 &
MOCK_PID=$!

# Wait for the mock to accept connections.
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

echo "--- live run: job metadata cube.gcode"
"$SIM" --moonraker "ws://127.0.0.1:$PORT/websocket" --dump-model-after 2 \
    --action "job metadata cube.gcode" \
    >"$TMP/live.txt" 2>"$TMP/live.err" || {
    echo "FAIL: simulator exited non-zero" >&2
    cat "$TMP/live.err" >&2
    exit 1
}

echo "--- dump output:"
cat "$TMP/live.txt"
echo "--- recorded requests:"
cat "$TMP/record.jsonl"

fail=0

# 1. server.files.metadata call recorded.
python3 - "$TMP/record.jsonl" <<'PY' || { echo "FAIL: no server.files.metadata call for cube.gcode" >&2; fail=1; }
import json, sys
found = False
with open(sys.argv[1]) as fh:
    for line in fh:
        line = line.strip()
        if not line:
            continue
        call = json.loads(line)
        if call.get("method") != "server.files.metadata":
            continue
        if call.get("params", {}).get("filename") == "cube.gcode":
            found = True
            break
sys.exit(0 if found else 1)
PY

# 2. Dump's JOB line shows need=200,... (T0's need from filament_weights).
if ! grep -q "^JOB need=200," "$TMP/live.txt"; then
    echo "FAIL: JOB line does not start with need=200," >&2
    grep "^JOB" "$TMP/live.txt" || true
    fail=1
fi

# 3. Dump's JOB line shows short=0 (tool 0 is short).
if ! grep -q "short=0" "$TMP/live.txt"; then
    echo "FAIL: JOB line does not contain short=0 (T0 not marked as short)" >&2
    grep "^JOB" "$TMP/live.txt" || true
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "e2e_enough: FAILED"
    exit 1
fi
echo "e2e_enough: PASS"
