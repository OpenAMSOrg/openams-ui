# Flash and set up

## What it is and why

The display firmware is an ESP-IDF project in `platform/esp32`. You build it,
flash it once over USB, then enter your Wi-Fi and Moonraker address over the
serial console. Later updates can go over the air.

## What you get

- A display that finds your printer and shows the bound unit.
- A build that carries only the languages you need, so the image fits the
  flash.
- Updates with automatic rollback if a new image fails to start.

## Requirements

- **The current OpenAMS on the printer:
  [OpenAMSOrg/openams](https://github.com/OpenAMSOrg/openams).** The display
  reads the `openams_ui <unit>` objects only it publishes; with
  `klipper_openams` the display connects but never shows a unit. Moving over:
  [Moving from klipper_openams](https://github.com/OpenAMSOrg/openams/blob/main/docs/guide/install.md#moving-from-klipper_openams).
- ESP-IDF v6.0.1 (CI builds with the `espressif/idf:v6.0.1` image; other
  versions are untested). Use it as installed: do not add components to
  ESP-IDF's own `components` folder (an `lvgl` copied there for another
  project breaks this build). This project brings its own LVGL.
- On Debian and Ubuntu, ESP-IDF's install script needs the `venv` package of
  your Python first: `sudo apt install python3.14-venv` for Python 3.14 (see
  `python3 --version`). Without it the script stops at `python3 -m venv`. It
  leaves a half-made `~/.espressif/python_env/idf6.0_py3.*_env`; delete that
  before running the script again.
- A checkout with the LVGL submodule: `git submodule update --init --recursive`.
- `lib/lvgl` must exist. The ESP32 project reads LVGL from there.
- A serial terminal at 115200 baud (the ESP-IDF default console speed; the
  project does not change it).

## Install

Build and flash from the ESP32 project directory, in an ESP-IDF shell. The
easiest way is the menu:

```sh
cd platform/esp32
make menuconfig       # the board, the tag writer, its reader and tag library, the languages, the port
make flash monitor    # build, flash, then watch it start
```

`make` hands everything to `make.py`, so where there is no `make` the same
runs as `python make.py <target>`. On Windows, open the **ESP-IDF PowerShell**
from the Start menu (or, in PowerShell, run `.\export.ps1` in the ESP-IDF
folder; `export.sh` is for Linux and macOS shells), then:

```powershell
cd C:\openams-ui\platform\esp32
python make.py menuconfig
python make.py flash monitor
```

On Windows, leave **Serial port** at its default and `idf.py` finds the
board's COM port, or set it (`COM3`) in the menu.

The menu keeps your choices in `platform/esp32/make/config` and turns them
into the `-D` options below, so `make` (or `make.py`) is `idf.py` with the
options filled in.
Each board builds in its own directory, `build-<board>`. A choice on the
command line overrides the menu (`make BOARD=es3c50p flash`), and `make help`
lists the rest: `make size`, `make clean`, and `make idf-menuconfig` for
ESP-IDF's own settings of the chosen board. **Tag library** says where the
tag writer's library of genuine tags comes from: OpenAMS on the printer host
(the default), or, on the 5 and 4.3 inch boards, the board's own flash, which `make
flash` can also fill ([tag-writer.md](tag-writer.md#the-tag-library)).

A build directory that another ESP-IDF Python or another checkout set up (after
reinstalling ESP-IDF, say, or moving the checkout) cannot be reused: `make`
starts it again by itself, while `idf.py` stops and asks for
`idf.py -B <directory> fullclean`. Building one board in the other's directory
stops with a message that names the directory to use.

Or run `idf.py` yourself:

```sh
cd platform/esp32
idf.py build
idf.py flash monitor
```

`dependencies.lock` pins the target to `esp32`, and `sdkconfig.defaults`
carries the board settings (4 MB flash, no PSRAM, 240 MHz).

### The 5 inch board

The ES3C50P ([hardware.md](hardware.md#the-5-inch-board-es3c50p)) is its own
build: the board picks the chip (ESP32-S3), its settings
(`sdkconfig.defaults.es3c50p`), its partition table and the 800x480 UI. With
`make`, choose it in `make menuconfig` (or `make BOARD=es3c50p flash monitor`).
With `idf.py`, give it its own build directory:

```sh
cd platform/esp32
idf.py -B build-es3c50p -DMMU_BOARD=es3c50p build
idf.py -B build-es3c50p -DMMU_BOARD=es3c50p flash monitor
```

The 4.3 inch ES3C43P ([hardware.md](hardware.md#the-43-inch-board-es3c43p)) is
the same, with `es3c43p`: `make BOARD=es3c43p flash monitor`, or

```sh
idf.py -B build-es3c43p -DMMU_BOARD=es3c43p build flash monitor
```

After a `git pull`, or after a build that failed, start clean (`make clean`
with make):

```sh
idf.py fullclean
idf.py build
```

### Language builds

A build carries one language set, because the CJK fonts alone cost about
240 KB. `MMU_LANGUAGES` picks the set; `cmake/languages.cmake` reads it.

| `MMU_LANGUAGES` | Carries |
|-----------------|---------|
| `latin` (ESP32 default) | `en de es fr it pl pt-BR ru` |
| `ja`, `ko`, `zh-Hans` | that language plus `en` |
| `en;ja` | any `;` list of codes; `en` is always added |
| `all` | every language; too large for an ESP32 slot |

```sh
idf.py -DMMU_LANGUAGES=ja build
idf.py -DMMU_LANGUAGES=zh-Hans flash monitor
```

The app version carries the set, for example `1.0.0-ja`. Change the base with
`-DMMU_APP_VERSION=1.1.0`. Keep the full version string within 31 characters.
Switching sets is easiest in a fresh build directory, since CMake caches the
value.

A language not compiled in is not offered in Settings. A stored language that
the image lacks falls back to English without an error.

## Configure the essentials

Open the serial console. Nothing you type is echoed, so the password never
shows on screen. Type carefully; the console cannot show you your typo. Commands:

| Command | Effect |
|---------|--------|
| `wifi <ssid> [password]` | store Wi-Fi credentials; quote an SSID with spaces |
| `moonraker <url>` | store the Moonraker WebSocket URL; must start with `ws://` |
| `unit <name>` | bind to one unit by its name ([Bind the display to one unit](#bind-the-display-to-one-unit)); `unit` alone clears it, and the display shows the first unit |
| `net` | show stored settings, Wi-Fi state and IP |
| `ota status` | show the running and other OTA slot |
| `ota <url> [force]` | download and boot a firmware image |
| `ui` | print what the screen task is doing |
| `perf` | print timing since the last `perf` (refreshes, loop, screen builds, tap-to-screen latency), then reset it |
| `nav <n>` | open screen `n` (0 home, 1 load, 2 alerts, ...), to time a screen without touching it |
| `tap <x> <y> [ms]` | tap the screen at `x`,`y` (held `ms`, default 60) and print what opens on top for the next 600 ms; a tap you can repeat without a finger |
| `reboot` | restart the display |
| `help` | list the commands |

First setup:

```
wifi "My Network" mypassword
moonraker ws://<printer-address>:7125/websocket
unit ams1
reboot
```

`unit ams1` binds the display to the unit named in its `[oams ams1]` section
of `oams.cfg`; use the name your `oams.cfg` gives the unit. Leave the line out
if you don't need it: the display then shows the printer's first unit, which
on a printer with one AMS is that AMS. See
[Bind the display to one unit](#bind-the-display-to-one-unit).

Each command that saves replies `saved; type reboot to apply`. If the
credentials do not save, it prints `error: could not save to NVS`. A line
longer than 160 characters is dropped with `line too long, dropped`.

Notes:

- Nothing is compiled in. With no SSID stored, the display runs in demo mode
  with dummy data. With an SSID but no Moonraker URL, it joins Wi-Fi and stays
  in demo mode.
- The Moonraker URL is Moonraker's own port and path,
  `ws://<printer-address>:7125/websocket`, not the address you open Mainsail
  at. `ws://192.168.1.20` (port 80) reaches the Mainsail web page and never
  connects: the log then repeats `Sec-WebSocket-Accept not found`.
- Moonraker must let the display connect. Add its address to
  `trusted_clients` in Moonraker's `[authorization]` section, or use a network
  Moonraker already trusts. The display stores no API key.

On the first boot with no stored touch calibration, the display runs the
calibration screen before anything else. Tap the four targets. See
[using.md](using.md).

## Bind the display to one unit

Skip this with one AMS on the printer: the display shows it. With several, an
unbound display shows the first one; bind it to show another.

1. Find the unit's name in `oams.cfg`: it is the name of the unit's
   `[oams <name>]` section, so `[oams ams1]` is `ams1`. A name is a label, not
   the kind of AMS: the Cortex Bridge setup writes `ams1`, `ams2` and
   `ams_ht`, and `ams1_2` for a second AMS of one kind; an OpenAMS 1
   configuration often says `oams1`.
2. On the display's console, type `unit <name>`, then `reboot`.
3. Check that the home screen shows that unit's spools. Then type `net`:
   once the display has connected, it shows `unit serial:` with the unit's
   serial, 24 hex digits.

You never look up or type the serial. It is the ID the unit's controller
reports (an OpenAMS 1 board without that ID reports its CAN UUID), not a
number printed on the AMS. The display reads it from the printer on the first
connect after `unit` and stores it with the name. From then on it looks for
the serial first and the name second, so renaming the unit in `oams.cfg`, or
running the Cortex Bridge setup again, keeps the binding: the display follows
the unit and stores its new name. With two AMS of one kind, it keeps the one
you bound. `unit` with another name, or `unit` alone, forgets the serial.

To see a unit's serial on the printer, open
`http://<printer-address>:7125/printer/objects/query?openams_ui%20<name>=serial`.
The answer ends in `"serial": "<24 hex digits>"`. `null` means the unit has not
reported one yet (it is still starting, or its firmware has no ID); the display
then goes by the name alone and stores the serial once it appears.

If the bound unit is gone from the printer, a printer with one unit shows that
unit and the setting stays as it was. With several units, the display waits
and lists their names on the home screen and in the log; see
[troubleshooting.md](troubleshooting.md#what-the-screen-says-while-it-waits).

## Check that it works

```
net
```

You want a stored URL and `wifi: up, ip ...`. The log then shows
`Moonraker link ready` once the link is up. The home screen fills with the
unit's bays. If it does not, see [troubleshooting.md](troubleshooting.md).

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| `error: url must start with ws://` | `https://` or a bare host | use `ws://host:7125/websocket` |
| `unknown command "..."; type help` | typo | type `help` |
| `error: image is for "...", this display runs "..."` | wrong project's image | flash the build from this project |
| `error: image version "..." matches the running image; use "ota <url> force" to reinstall it` | same version | add `force`, or bump `MMU_APP_VERSION` |
| `error: bad url (need http:// or https://host/...)` | malformed OTA URL | fix the URL |
| `error: download failed (...)` | host unreachable or certificate rejected | check the URL; a private-CA `https://` host fails, use `http://` on the LAN |
| `no Wi-Fi credentials stored — demo mode` | no `wifi` command yet | run `wifi`, then `reboot` |
| Build: `lib/lv_conf.h:1:1: error: expected identifier or '(' before '.' token` | a checkout from before the fix for Windows (git wrote a link as a text file) | `git pull`, then `idf.py fullclean` and build |
| Build: `Missing required kconfig option after retry` while configuring `bootloader`; the log names `esp-idf/components/lvgl/idf_component.yml` | an extra `lvgl` component inside ESP-IDF's own `components` folder | move that folder out of ESP-IDF, then `idf.py fullclean` and build |
| Build (macOS): `'malloc.h' file not found` in `test_moonraker_client.c` | a checkout from before the macOS fix | `git pull` |
| Build: `Component directory .../components/mqtt does not contain a CMakeLists.txt file` | ESP-IDF v6 moved MQTT out; a harmless notice | nothing |

## Update and uninstall

Update over the air from the console:

```
ota status
ota http://<printer-address>/path/to/openams-ui_1.0.0-latin.bin
```

The display downloads into the inactive slot, logs every 10%, checks the
project name, refuses an image with the running version unless you add
`force`, then reboots into it. A new image is confirmed once the UI is up and
Wi-Fi connected once (or 60 s passed with no Wi-Fi configured). If it resets
first, the bootloader returns to the previous slot. Details:
[docs/ESP32_OTA.md](../ESP32_OTA.md).

Stored Wi-Fi and Moonraker settings survive an update. A board flashed with the
old single-app layout needs one USB flash of the new partition table first
(`idf.py flash`).

On the 4 inch board an image with the spool tag writer (the default build)
has no second app slot: it does not fit one, so update it over USB with
`idf.py flash` ([tag-writer.md](tag-writer.md)). Build with
`-DMMU_SPOOL_TAG_WRITER=OFF` for a 4 inch display that updates over the air.
The 5 and 4.3 inch boards update over the air with or without it.

There is nothing to uninstall on the printer. Reflash the board, or erase it
with `idf.py erase-flash`, which also clears the stored settings.

## Gotchas

- CI fails a build whose app image exceeds 90% of one OTA slot
  (`tools/ci/esp32_size_check.py`). A local build that grows past that will
  not update over the air.
- `ota` accepts `http://` and `https://`. HTTPS verifies against ESP-IDF's
  bundled root certificates.
- Network settings are set from the console only. The display has no on-screen
  keyboard, which suits a panel this small.

## Where next

[Use the display](using.md), or [fix a problem](troubleshooting.md).
