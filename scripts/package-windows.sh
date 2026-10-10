#!/usr/bin/env bash
# Cross-compiles the SDL simulator for 64-bit Windows with MinGW-w64 and
# zips it into dist/openams-ui-windows.zip for a non-developer tester:
# the .exe, SDL2.dll, a README.txt aimed at them, and start-*.bat shortcuts.
#
# Usage: scripts/package-windows.sh
#   SDL2_MINGW_DIR=<dir>   the SDL2 mingw devel package's own root (the dir
#                          holding include/, lib/, bin/SDL2.dll — default:
#                          third_party/SDL2-2.30.9/x86_64-w64-mingw32).
#   JOBS=<n>               build parallelism (default 3).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

JOBS="${JOBS:-3}"
SDL2_MINGW_DIR="${SDL2_MINGW_DIR:-$REPO_ROOT/third_party/SDL2-2.30.9/x86_64-w64-mingw32}"

if [ ! -f "$SDL2_MINGW_DIR/lib/cmake/SDL2/SDL2Config.cmake" ]; then
    echo "package-windows.sh: no SDL2 CMake package under $SDL2_MINGW_DIR" >&2
    echo "  set SDL2_MINGW_DIR to the .../x86_64-w64-mingw32 devel package root" >&2
    exit 1
fi

BUILD_DIR="$REPO_ROOT/build-win"
DIST_DIR="$REPO_ROOT/dist"

cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$REPO_ROOT/cmake/mingw-w64-x86_64.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$SDL2_MINGW_DIR"

cmake --build "$BUILD_DIR" -j"$JOBS"

EXE="$BUILD_DIR/openams-ui-sim.exe"
DLL="$SDL2_MINGW_DIR/bin/SDL2.dll"

[ -f "$EXE" ] || { echo "package-windows.sh: build did not produce $EXE" >&2; exit 1; }
[ -f "$DLL" ] || { echo "package-windows.sh: no SDL2.dll at $DLL" >&2; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

cp "$EXE" "$STAGE/openams-ui-sim.exe"
cp "$DLL" "$STAGE/SDL2.dll"

cat > "$STAGE/README.txt" <<'EOF'
MMU display simulator - Windows tester build
=============================================

WHAT THIS IS
------------
A desktop simulator of the touchscreen mounted on a filament unit (an AMS-style
multi-material system). It is the real UI code running in a window instead of
on the physical screen. Your mouse click is a finger tap; there is no keyboard
interaction needed.

HOW TO START
------------
Double-click openams-ui-sim.exe, or one of the start-*.bat shortcuts below.
A console window opens behind the simulator window - that is expected, it is
where log lines print, and closing it closes the app. Windows may show a
SmartScreen warning because the .exe is unsigned; choose "More info" ->
"Run anyway".

COMMAND-LINE FLAGS
------------------
  --scenario NAME     start on one scenario (default: idle). NAME is one of:
                         idle       nothing running; two slots show filament
                                    info read one way (tag) and one way
                                    (typed in manually)
                         loading    a fresh spool is being loaded into a slot
                                    while another slot still holds filament
                         error      a load is paused on a failure (clogged
                                    extruder) with a retry/details choice
                         drying     a unit is running its heater on a timed
                                    dry cycle
                         printing   printing is under way with a filament
                                    change running at the same time
                         runout     a spool ran out mid-print and auto-refill
                                    switched the print to a matching spool on
                                    another unit
                         dual       a dual-extruder setup: one unit on each
                                    side, one external spool per side
                         remote     the display is mounted on one unit while
                                    a DIFFERENT unit is loading, printing and
                                    sitting on a paused error at the same time
  --unit NAME         bind the display to one unit, as if it were physically
                      mounted there (default: A). NAME is one of:
                         A          AMS 2 Pro - 4 slots, humidity %, has a
                                    heater/drier
                         B          AMS - 4 slots, 5-step humidity indicator
                                    only, no heater
                         HT-A       AMS HT - 1 high-temperature slot with its
                                    own drier
  --zoom N            window scale (default 3 = 960x720). Turn it down if the
                      window does not fit your screen, up if it is too small
                      to read comfortably.

SWITCHING SCENARIO OR UNIT WITHOUT RESTARTING
----------------------------------------------
  Scenario:  long-press (press and hold) the title text at the top of the
             screen, then pick a scenario from the list.
  Unit:      open the Menu (bottom tab bar) -> "This unit", then pick A, B or
             HT-A.

WHAT TO SEND BACK
------------------
Please send back:
  1. Your notes - what you clicked, what you expected, what happened instead,
     ideally with the scenario and unit you were on.
  2. The file openams-ui-actions.log, written next to the .exe. It is a plain
     text log of every action the app recognized while you were using it, in
     the order you did them - useful even without a screenshot.

INCLUDED SHORTCUTS
-------------------
  start-unit-A.bat            starts bound to unit A (AMS 2 Pro)
  start-unit-B.bat            starts bound to unit B (AMS)
  start-unit-HT-A.bat         starts bound to unit HT-A (AMS HT)
  start-remote-scenario.bat   starts on the "remote" scenario (see above)
EOF

bat_crlf() {
    # printf with explicit \r\n: a plain heredoc would leave bare \n, which
    # Notepad (still how most testers would open one to check it) renders as
    # one unbroken line.
    printf '%s\r\n' "@echo off" "openams-ui-sim.exe $1" "pause" > "$STAGE/$2"
}

bat_crlf "--unit A"                     "start-unit-A.bat"
bat_crlf "--unit B"                     "start-unit-B.bat"
bat_crlf "--unit HT-A"                  "start-unit-HT-A.bat"
bat_crlf "--scenario remote"            "start-remote-scenario.bat"

mkdir -p "$DIST_DIR"
rm -f "$DIST_DIR/openams-ui-windows.zip"
( cd "$STAGE" && zip -q -r "$DIST_DIR/openams-ui-windows.zip" . )

echo "Wrote $DIST_DIR/openams-ui-windows.zip"
