#!/usr/bin/env bash
# End-to-end check: spool list, spool confirm, and link actions are wired.
#
# A fixture with spoolman_responses drives the mock so server.spoolman.proxy
# returns a real spool array. The sim is run with three --action flags:
#
#   spool list             → server.spoolman.proxy (GET /v1/spool)
#   spool confirm oams11 750 → server.openams_spoolman.confirm (bay oams1-0)
#   link oams11 42         → printer.gcode.script (OAMSM_SET_BAY_SPOOL ...)
#
# All three requests must appear in the mock's record.
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

# Write a fixture with spoolman_responses and a pending_confirmation bay.
cat >"$TMP/spoolman.json" <<'FIXTURE'
{
  "scenario": "spoolman_e2e",
  "seed": 11111111,
  "family_map": { "oams1": "ams1" },
  "spoolman_responses": {
    "GET /v1/spool": [
      {
        "id": 42,
        "archived": false,
        "remaining_weight": 800.0,
        "filament": {
          "material": "PLA",
          "color_hex": "FF0000",
          "vendor": { "name": "Bambu Lab" }
        }
      },
      {
        "id": 99,
        "archived": true,
        "remaining_weight": 100.0,
        "filament": {
          "material": "ABS",
          "color_hex": "000000",
          "vendor": { "name": "eSUN" }
        }
      }
    ]
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
               "group": null, "source": "none", "position": null},
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
        "schema_version": 1,
        "name": "oams1",
        "bays": [
          {"id": "oams1-0", "state": "unloaded", "spool_id": null, "filament": null},
          {"id": "oams1-1", "state": "unloaded", "spool_id": null, "filament": null},
          {"id": "oams1-2", "state": "unloaded", "spool_id": null, "filament": null},
          {"id": "oams1-3", "state": "unloaded", "spool_id": null, "filament": null}
        ]
      }
    },
    {
      "t": 0.0,
      "kind": "component_status",
      "unit": null,
      "payload": {
        "schema_version": 1,
        "spoolman_online": true,
        "queued_writes": 0,
        "bays": {
          "oams1-0": {
            "state": "unlinked",
            "spool_id": null,
            "source": "none",
            "pending_confirmation": true,
            "error": null,
            "spool": null
          },
          "oams1-1": {
            "state": "unlinked",
            "spool_id": null,
            "source": "none",
            "pending_confirmation": false,
            "error": null,
            "spool": null
          },
          "oams1-2": {
            "state": "unlinked",
            "spool_id": null,
            "source": "none",
            "pending_confirmation": false,
            "error": null,
            "spool": null
          },
          "oams1-3": {
            "state": "unlinked",
            "spool_id": null,
            "source": "none",
            "pending_confirmation": false,
            "error": null,
            "spool": null
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

echo "mock_moonraker: spoolman.json, port $PORT"
(
    cd "$OPENAMS_ROOT/tools" &&
        exec python3 -m mock_moonraker \
            --fixture "$TMP/spoolman.json" \
            --port "$PORT" --speed 0 \
            --record "$TMP/record.jsonl"
) >"$TMP/mock.log" 2>&1 &
MOCK_PID=$!

# Wait for the mock to start accepting connections.
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

echo "--- live run: spool list / spool confirm / link"
"$SIM" --moonraker "ws://127.0.0.1:$PORT/websocket" --dump-model-after 2 \
    --action "spool list" \
    --action "spool confirm oams11 750" \
    --action "link oams11 42" \
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

echo "--- requests recorded by mock:"
cat "$TMP/record.jsonl"

fail=0

# 1. spool list → server.spoolman.proxy with GET /v1/spool
python3 - "$TMP/record.jsonl" <<'PY' || { echo "FAIL: no server.spoolman.proxy call with GET /v1/spool" >&2; fail=1; }
import json, sys
found = False
with open(sys.argv[1]) as fh:
    for line in fh:
        line = line.strip()
        if not line:
            continue
        call = json.loads(line)
        if call.get("method") != "server.spoolman.proxy":
            continue
        p = call.get("params", {})
        if p.get("request_method", "").upper() == "GET" and p.get("path") == "/v1/spool":
            found = True
            break
sys.exit(0 if found else 1)
PY

# 2. spool confirm → server.openams_spoolman.confirm with bay oams1-0 and remaining_g 750
python3 - "$TMP/record.jsonl" <<'PY' || { echo "FAIL: no server.openams_spoolman.confirm call with bay=oams1-0 remaining_g=750" >&2; fail=1; }
import json, sys
found = False
with open(sys.argv[1]) as fh:
    for line in fh:
        line = line.strip()
        if not line:
            continue
        call = json.loads(line)
        if call.get("method") != "server.openams_spoolman.confirm":
            continue
        p = call.get("params", {})
        if p.get("bay") == "oams1-0" and p.get("remaining_g") == 750:
            found = True
            break
sys.exit(0 if found else 1)
PY

# 3. link oams11 42 → printer.gcode.script with OAMSM_SET_BAY_SPOOL
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

WANT_LINK="OAMSM_SET_BAY_SPOOL OAMS=oams1 BAY=0 SPOOL=42 SOURCE=manual"
if ! grep -qxF "$WANT_LINK" "$TMP/scripts.txt"; then
    echo "FAIL: no printer.gcode.script carried exactly '$WANT_LINK'" >&2
    echo "--- scripts seen:" >&2
    cat "$TMP/scripts.txt" >&2
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "e2e_spoolman: FAILED"
    exit 1
fi
echo "e2e_spoolman: PASS"
