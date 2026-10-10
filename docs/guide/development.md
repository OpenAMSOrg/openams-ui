# Development

## What it is and why

You can work on the display without a board. The simulator runs the same C
core and the same LVGL screens in a desktop window, and can mirror a real
printer over Moonraker. Tests keep the C core, its TypeScript twin and the web
panel in step.

## What it adds

- A 480x320 window that behaves like the device.
- Headless dumps of the model and the view, for scripts and tests.
- A live mode that talks to a real Moonraker.

## Requirements

- CMake and a C compiler; SDL2 (`libsdl2-dev`, or `brew install sdl2`).
- The LVGL submodule: `git submodule update --init --recursive`.
- Node.js for the generators, the TypeScript logic and the web panel.
- ESP-IDF v6.0.1 for the firmware ([flash-and-setup.md](flash-and-setup.md)).
- Emscripten, only to build the wasm core and the browser demo.

## Install

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

A separate directory takes another language set:
`cmake -S . -B build-ja -DMMU_LANGUAGES="en;ja"`.

## The simulator

```sh
./build/openams-ui-sim                          # interactive, 4x zoom
./build/openams-ui-sim --scenario drying --unit A --lang de --zoom 2
```

Scenarios: `idle`, `loading`, `error`, `drying`, `printing`, `runout`, `dual`.
Long-press a header title for the debug screen, which switches scenario.

| Flag | Effect |
|------|--------|
| `--scenario NAME` | start on a scenario (default `idle`) |
| `--unit NAME` | bind to a unit, as on a mounted display |
| `--lang CODE` | draw in a language; a code the build lacks falls back to English |
| `--zoom N` | window zoom |
| `--smoke` | walk every scenario, unit, screen and modal; prints `SMOKE OK ...`, exit 1 on failure |
| `--audit` | smoke plus a layout audit; CI wants `AUDIT TOTAL issues=0` |
| `--screenshots DIR` | the same walk, one BMP per screen and modal |
| `--gallery DIR` | render the component gallery; prints `GALLERY OK issues=<n>` |
| `--replay FILE`, `--replay-speed X` | replay a recorded fixture from `tests/fixtures` |

`--smoke`, `--screenshots` and `--gallery` drive LVGL's clock themselves, so
they run fast. On a machine with no display, prefix `SDL_VIDEODRIVER=dummy`.

### Live mode

`--moonraker URL` mirrors a real printer instead of a scenario:

```sh
./build/openams-ui-sim --moonraker ws://<host>:7125/websocket
./build/openams-ui-sim --moonraker ws://<host>:7125/websocket --bind-unit oams1
```

`--bind-unit NAME` binds to that `openams_ui` object instead of the first one.

### Dumps and injected actions

These print text and exit, with no window:

| Flag | Needs | Prints |
|------|-------|--------|
| `--dump-model` | `--replay FILE` | key model fields after the whole fixture |
| `--dump-view` | `--replay FILE` | the view builders' JSON, through the core |
| `--dump-model-after S` | `--moonraker URL` | the model dump after S seconds of live data |
| `--dump-view-after S` | `--moonraker URL` | the view JSON after S seconds |
| `--action LINE` | `--moonraker`, and a `--dump-*-after` | injects a UI line once the link is ready; repeat up to 8 times |

```sh
./build/openams-ui-sim --replay tests/fixtures/idle.json --dump-view
./build/openams-ui-sim --moonraker ws://<host>:7125/websocket \
    --dump-model-after 5 --action "load oams1"
```

`--dump-model` and `--dump-view` are different dumps; pick one. `--moonraker`
and `--replay` are different sources; pick one. A line is exactly what a screen
logs, for example `load oams1` or `spool edit ...`. The action is mapped and
sent to the host as G-code or an RPC.

## Tests

| What | Command |
|------|---------|
| C unit tests | `ctest --test-dir build --output-on-failure` |
| Smoke and layout audit | `SDL_VIDEODRIVER=dummy ./build/openams-ui-sim --smoke --audit` |
| Generated files are current | `node tools/gen_strings.mjs --check`, `node tools/gen_tokens.mjs --check`, `python3 tools/export_actions_vectors.py --check` |
| No stray UI literals | `node tools/check_literals.mjs` |
| Device fonts cover every language | `node tools/check_glyphs.mjs --check` |
| TypeScript typecheck | `cd web/logic && npx tsc -p tsconfig.json` (after `npm install`) |
| Parity: the C core as wasm against the TypeScript twin | `bash platform/core-wasm/build.sh`, then `node --test web/core/test/*.test.mjs web/logic/test/*.test.ts` |
| Generator tests | `node --test tools/test/*.test.mjs` |
| Web panel (Playwright) | `cd web/vue && npm ci && npx playwright install chromium && npm test` |
| End-to-end against a mock Moonraker | `tests/e2e_mock_moonraker.sh`, `tests/e2e_actions.sh` |

The end-to-end scripts need the OpenAMS plugin checkout (`OPENAMS_ROOT`) for
its mock Moonraker and the `aiohttp` Python package. They print `SKIP` and exit
0 when either is missing. CI treats a skip as a failure.

The parity tests are the point of the twin: a change to the C core that the
TypeScript does not follow fails them. Playwright drives the web panel's
gallery (`npm run gallery`), which builds states by feeding synthetic Moonraker
objects through the real core.

## Design principles

The UI follows [docs/design/PRINCIPLES.md](../design/PRINCIPLES.md), a
grammar both renderers speak. Read it before changing a screen. The ones that
bite most often:

- State changes content, never geometry. Heights do not move.
- Position is meaning. A fact appears at one level only.
- The core composes every string; a renderer never writes its own.

[docs/design/UNIFIED_UI.md](../design/UNIFIED_UI.md) records the decisions.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| `pkg-config` cannot find `sdl2` | SDL2 not installed | install `libsdl2-dev` |
| Smoke exits 1 | a screen failed to build, or LVGL's pool peaked over 90% | read the last line before the failure |
| `usage: ... --moonraker URL --dump-model-after SECONDS --action LINE` | `--action` outside live headless mode | add `--moonraker` and a `--dump-*-after` |
| `--action takes at most 8 lines` | too many `--action` | trim them |
| `unknown host: ...` from the generator | typo | `node tools/gen_host.mjs --list` |
| `SKIP: no mock_moonraker ...` | OpenAMS checkout missing | set `OPENAMS_ROOT` |

## Update and uninstall

Pull and rebuild. Remove the `build*` directories to uninstall.

## Gotchas

- The simulator, the tests and the wasm core build every language by default.
  Only the ESP32 build defaults to `latin`.
- Do not hand-edit generated files (`strings_table.gen.h`, the tokens, the host
  panel); change the source and regenerate.
- Build with `-j2` on a shared machine.

## Where next

[Back to the guide index](README.md).
