# Troubleshooting

## What it is and why

Most display problems fall into four groups: no connection, a blank screen,
touch that lands in the wrong place, and wrong colors. The serial console
(115200 baud) shows the log and answers `net` and `ui`, which are enough to
tell them apart. This page uses the messages the firmware really prints.

## Requirements

A USB serial connection to the display and a terminal. Open it with
`idf.py monitor`, or any terminal program.

## First look

```
net
ui
```

`net` prints the stored settings and the Wi-Fi state:

```
ssid: <name>
password: set
moonraker: ws://<host>:7125/websocket
unit: (first unit)
wifi: up, ip <address>
```

`ui` prints six lines. Read them in this order:

| Line | Meaning |
|------|---------|
| `ui: flushes=... pixels=... screen_children=... nav_screen=... top=... sys=...` | how many times the panel was drawn, how many pixels were sent, and how many objects the active screen holds |
| `ui: model units=... toolheads=... live=...` | what the core knows; `live=1` means a Moonraker link feeds it |
| `ui: lvgl mem used=...% free=... max_used=... frag=...%` | LVGL's own pool |
| `ui: heap free=... largest=...` | the ESP32 heap, total and largest block |
| `link: state=... objects=... not_applied=... last=... syncs=... shadow units=... toolheads=...` | the Moonraker link: its state, objects received, objects the core refused, the last one, and the units it holds |
| `ui: stack left: ui task=... bytes, startup task=... bytes` | stack headroom |

The link state is `down`, `connecting`, `klippy-wait` or `ready`.

## Troubleshooting

### No connection

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| `net` shows `moonraker: (none)` | no URL stored | `moonraker ws://<host>:7125/websocket`, then `reboot` |
| `net` shows `ssid: (none)` or the log says `no Wi-Fi credentials stored — demo mode` | no Wi-Fi stored | `wifi <ssid> [password]`, then `reboot` |
| `net` shows `wifi: down`; log shows `disconnected (reason N)` and `reconnecting in Ns` | wrong password, out of range, or 5 GHz only | recheck `wifi`; the display needs 2.4 GHz and retries with backoff |
| `wifi: up` but `link: state=connecting` or `down`; log shows `Moonraker link down` | wrong host or port, or Moonraker refuses this client | check the URL; add the display to Moonraker's `trusted_clients` |
| `link: state=klippy-wait` | Moonraker is up, Klipper is not | start or restart Klipper |
| Link `ready` but the screen is empty and `shadow units=0` | `unit` names a unit that does not exist | `net` shows the stored name; run `unit <name>` with the real one, or `unit` alone |
| Log shows `send failed; the request will time out and retry` | the socket dropped mid-request | it retries; if it repeats, check Wi-Fi signal |
| Log shows `dropping a WebSocket message over N bytes` | a status message larger than the receive limit | report it with the log line |
| Display shows demo data | Wi-Fi or URL missing, see `live=0` in `ui` | store both, `reboot` |
| `no memory for the link — staying in demo mode` or `no memory for the core — staying in demo mode` | the heap was too small at startup | see "Blank screen" |

### Blank screen with the backlight on

The backlight is on but nothing draws. The usual cause is a starved heap: the
core could not copy the printer's state, so there is nothing to draw.

1. Type `ui`.
2. Read `heap free=... largest=...`. A small `largest` is the problem.
3. Read `link: ... not_applied=...`. A nonzero count means the core refused
   objects. Each refusal is logged:

```
the core did not take <kind> <name> (heap free N, largest block N)
```

   A refused status object leaves the home screen empty.
4. Run `ui` twice. A `flushes=` count that does not grow while the link is
   `ready` suggests the screen task is not drawing.
5. A log line `rejected a N-byte message (free heap N, largest block N)` is the
   same cause one step earlier.

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| `the core did not take ...` | heap too small for the object's copy | reboot; if it repeats, keep the `ui` output and the log line and report it |
| `rejected a N-byte message` | no block big enough for the message | same as above; reboot first |
| `failed to allocate a N-byte DMA draw buffer` | not enough contiguous internal RAM for the draw buffer | the display halts; reflash a smaller build |
| `spi_bus_initialize(SPI2_HOST) failed` or `spi_bus_add_device (panel) failed` | bus setup failed | check wiring, then the board |
| White or smeared image | SPI clock too fast | `PANEL_SPI_HZ` to `20000000` ([hardware.md](hardware.md)) |

### Touch offset

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| Taps land away from the finger | stale or wrong calibration | Setup, "Calibrate touch", then four targets |
| The log shows `stored calibration failed the sanity check, ignoring it` | stored values are bad | the calibration screen runs on its own at the next boot |
| The log shows `calibration fit was degenerate (collinear samples?) — keeping the previous mapping` | targets pressed in a line or at the same place | calibrate again, pressing each target firmly |
| `target N result implausible: ... retrying` | a target was too close to the last | press the next target where shown |
| `touch rejected: ... raw=... z1=... R=...` | a light or noisy press | press harder, with a fingertip or stylus |

Without a touch screen, the log says `touch_init() failed — continuing with no
touch input`. Check the touch wiring ([hardware.md](hardware.md)).

### Panel colors or garbage

The comments in `platform/esp32/main/panel_config.h` carry the list. In short:

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| Colors inverted (white looks black) | inversion setting | set `PANEL_FORCE_INVERT` to `1`, rebuild |
| Red and blue swapped | BGR bit | set `PANEL_FORCE_BGR` to `0` (or `1`), rebuild |
| Portrait image, mirrored or upside down | rotation bits | try `PANEL_ROTATION_BITS` `0x60`, `0xA0`, then `0xE0` |
| Smeared image or ID mismatch | SPI too fast | `PANEL_SPI_HZ` to `20000000` |
| Log says `ID read did not match ST7796S` | a different panel, or a bad SPI link | check the board; this build drives the ST7796S only |

The boot log prints what the panel is using:
`panel config: inversion=... bgr=... madctl=0x.. spi_hz=...`. The ID line reads
`panel id: RDID4(0xD3)=0x... (expect 0x007796) match=...`.

## Update and uninstall

Fixes that need a rebuild go through [flash-and-setup.md](flash-and-setup.md).
Erase stored settings with `idf.py erase-flash`.

## Gotchas

- `ui` asks the screen task to print on its next loop. If nothing prints, that
  task is stuck, which is itself the answer.
- Changing a `panel_config.h` macro needs a rebuild. Nothing changes at runtime.
- A heap problem often shows up first as a refused object, not a crash. It is a polite way to fail.

## Where next

[Development](development.md) if you want to reproduce it in the simulator.
