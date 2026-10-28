# Spool tag writer

## What it is and why

The display can also write the RFID tags your spools carry. With a reader
wired to the board, **Menu → Tag writer** opens a screen that:

- composes a spool (material preset, colour, weight, temperatures, drying)
  and writes it to a tag in the Bambu Lab layout, keyed so OpenAMS reads it;
- reads any filament tag the reader can see and shows what it says, with
  "Use these settings" to copy it into the spool you are composing;
- resets a tag it wrote back to factory keys, copies a genuine Bambu tag and
  clones it onto a magic tag (for a stock AMS), from the tag itself or from a
  library of genuine tags that OpenAMS serves ([the tag library](#the-tag-library)).

The display keeps running underneath: its link to the printer stays up, and
the reader only runs while the tag writer screen is open (or a console command
uses it). Leaving the screen goes back to the display's home screen.

It is built into the ESP32 image by default. An image built without it
(`-DMMU_SPOOL_TAG_WRITER=OFF`) is the display alone.

## Readers

Two readers are supported; the image is built for one of them.

| | PN5180 (default) | MFRC522 |
|---|---|---|
| Build option | none | `-DMMU_TAG_READER=MFRC522` |
| MIFARE Classic 1K | read, write | read, write |
| NFC Forum Type 2 (NTAG213/215/216, Ultralight) | read, write | no |
| NFC Forum Type 5 (ICODE SLIX, ISO 15693) | read, write | no |
| Formats read | Bambu, OpenSpool, Spoolman, OpenPrintTag, OpenTag3D, TigerTag, Anycubic, Elegoo, SpoolEase, QIDI, Creality, Snapmaker | Bambu |
| Formats written | Bambu, OpenSpool, Spoolman, OpenPrintTag, OpenTag3D, TigerTag, Anycubic, QIDI | Bambu |
| Magic tags (clone) | gen1a, gen2, gen3 | gen1a, gen2, gen3, gen4 |

Creality and Snapmaker tags are keyed with their makers' own keys, which this
build does not set, so those two read as unrecognised.

## Wiring

### The 4 inch board (E32R40T)

The reader goes on the board's SPI connector (the VSPI bus), with reset and
one status line on the other connectors. The panel and touch use the other SPI
bus, so nothing else on the board shares these pins.

| Reader pin | Board connector | GPIO |
|---|---|---|
| NSS / SDA (CS) | SPI connector pin 4 | IO21 |
| SCK | SPI connector pin 3 | IO18 |
| MOSI | SPI connector pin 1 | IO23 |
| MISO | SPI connector pin 2 | IO19 |
| 3.3V | I2C connector pin 1 | 3V3 |
| GND | I2C connector pin 4 | GND |
| RST | I2C connector pin 2 | IO32 (a 10k pull-up holds the reader on) |
| PN5180: BUSY / MFRC522: IRQ | input-only connector | IO35 |

- The SPI connector shares its lines with the microSD slot. **Leave the slot
  empty.**
- Most PN5180 breakout boards also need 5 V for their RF driver (a pin marked
  `5V`). Check your module's silkscreen.
- The pins are fixed in `platform/esp32/tag_writer/src/tag_writer.cpp`.

### The 5 inch board (ES3C50P)

Every GPIO of this board but two goes to the panel, the PSRAM, touch or USB.
The two, IO17 and IO18, are on the "Expand" connectors, and IO18 also reaches
the touch controller's interrupt through **R16, a 0 Ω link that has to come
off** first. Touch does not need it: the firmware reads touches by polling and
finds the touch controller at either of its addresses. The SPI bus is the one
on the "SPI" connector, but that connector's chip select, IO19, is the touch
controller's I2C data, so the chip select is IO17.

The PN5180's RF driver also needs 5 V, and the only 5 V on a connector, UART0
pin 1, comes through **R12, 10 kΩ, which has to become 0 Ω**. Through 10 kΩ the
PN5180 answers on SPI and reports its field on, but no tag answers it.

Two resistors change, both on the back:

1. Unplug the board. R16 is just left of the touch panel's flex connector
   (P2), next to R15. Leave R15 (a 10 kΩ pull-up) in place. With a meter, R16
   reads about 0 Ω across its ends.
2. Heat both ends of R16 at once (a blob of solder across it, or tweezers) and
   lift it off. A solder bridge across its pads puts it back.
3. R12 is next to the UART0 connector (P1), with R10 and R11, and reads about
   10 kΩ. Replace it with a 0 Ω resistor of the same size (R16 fits) or bridge
   its pads.
4. Power the board before wiring the reader: touch still works, and the log
   says `touch ready: GT911 at 0x14` (or `0x5D`, whichever the controller
   picks). UART0 pin 1 now reads close to 5 V (the USB 5 V less a diode).

With R12 bridged, UART0 pin 1 is the USB 5 V with nothing to limit it: wire it
with care, and do not connect a USB-to-serial adapter's 5V to that connector
while the board is powered over USB-C.

All four connectors are 1.25 mm, 4 pins. Pin 1 is the leftmost, looking at
the back with the connectors along the bottom edge, and the silkscreen names
each pin.

| Reader pin | Board connector | GPIO |
|---|---|---|
| NSS / SDA (CS) | Expand (P5) pin 3 | IO17 |
| SCK | SPI (P3) pin 3 | IO12 |
| MOSI | SPI (P3) pin 2 | IO11 |
| MISO | SPI (P3) pin 4 | IO13 |
| PN5180: BUSY / MFRC522: RST | Expand (P5) pin 4 | IO18 |
| 3.3V | Expand (P5) pin 2 | 3V3 |
| GND | Expand (P5) pin 1 | GND |
| PN5180: 5V | UART0 (P1) pin 1, with R12 at 0 Ω | 5V |
| PN5180: RST | tied to 3.3V on the module | |
| IRQ (and the PN5180's GPIO, AUX, REQ) | not connected | |

- Leave the SPI connector's pin 1 (IO19, touch) unconnected, and the microSD
  slot empty: a card would answer on the same lines.
- Tie the PN5180's RST to its own 3.3V pin (a solder bridge or a short wire),
  not to 5 V. The chip starts on power-up; with no reset line the firmware
  cannot restart it, so a reader that ever stops answering needs a power
  cycle, not just the board's RESET button.
- The second Expand connector (P6) carries the same GND, 3.3V, IO17 and IO18.
- `tag status` says `reader PN5180 version=0x04` (or 0x03) when the SPI lines
  are right. `version=0x00 (not responding)` with the PN5180 powered is most
  often SCK and MISO swapped.

## Build and flash

The default build has the tag writer with a PN5180:

```sh
cd platform/esp32
idf.py build flash monitor
```

| Option | Default | Effect |
|---|---|---|
| `MMU_SPOOL_TAG_WRITER` | `ON` | `OFF` builds the display alone |
| `MMU_TAG_READER` | `PN5180` | `MFRC522` for the original reader |
| `MMU_TAG_LIBRARY` | `openams` | `board` keeps the [tag library](#the-tag-library) in the board's own flash (5 inch only) |

`MMU_LANGUAGES` works as for every image (see the [README](../../README.md)).
`make menuconfig` has the same choices in a menu
([flash-and-setup.md](flash-and-setup.md#install)).

On the 4 inch board an image with the tag writer has its own partition table,
`platform/esp32/partitions_tagwriter.csv`: one app partition the size of the
flash instead of the two OTA slots, which the image does not fit. So **a 4
inch image with the tag writer is updated over USB**, not with `ota`. Its
settings live in a separate generated `sdkconfig.tagwriter`, so switching the
option needs no clean-up. Flashing over USB from one layout to the other keeps
your Wi-Fi, Moonraker and touch settings (NVS does not move). The 5 inch board
has one layout for every image, with its two OTA slots.

## First use

1. Store the Bambu master key over the serial console. It is never compiled
   in; the display keeps it in its own flash:

   ```
   tag key <32 hex digits>
   ```

   `tag status` shows its fingerprint. Without the key the tag writer still
   reads and writes every other format, but cannot write or read the Bambu
   layout.
2. Tap **Menu → Tag writer**.

## Using it

The home page shows the spool you are composing and what is on the reader.

| Button | Does |
|---|---|
| Material / Color / Details | edit the spool |
| Read | read the tag on the reader (any type and format with a PN5180) |
| Write | write the spool to a blank or custom MIFARE Classic 1K tag, then verify it |
| Clone | clone a tag from the [tag library](#the-tag-library) onto a magic tag; each clone takes a UID of that colour not cloned before, so two spools never share one |
| Gear | copy a physical tag, clone from the library, reset a tag to blank |
| Back arrow | return to the display |

## Console

Every tag writer command runs from the display's serial console with a `tag`
prefix; `tag help` lists them.

| Command | Does |
|---|---|
| `tag status` | reader, key fingerprint, the tag on the reader, the spool |
| `tag read` / `tag decode` | the tag as JSON: Bambu layout / any format (PN5180) |
| `tag encode <format> [field=value ...]` | write the spool in another format (PN5180), e.g. `tag encode spoolman spool_id=42` |
| `tag write` / `tag erase yes` | write the spool (Bambu layout) / reset a tag to factory keys |
| `tag dump`, `tag magic` | all 64 blocks as hex; identify a magic tag |
| `tag capture`, `tag loadimage`, `tag image`, `tag clone` | the clone image buffer |
| `tag libadd`, `tag liblist`, `tag libcount` | add one tag to the library, list it, count it |
| `tag libclear yes` | erase the library, when it is on the board |
| `tag key <32 hex>` / `tag key clear` | store or forget the Bambu master key |

`tools/tagtool.py` drives these from a PC: `clonefrom` clones one tag from a
folder of dumps, `cmd` sends any command.

## The tag library

The library holds genuine tag dumps to clone from the touchscreen: every dump
in the
[Bambu-Lab-RFID-Library](https://github.com/queengooborg/Bambu-Lab-RFID-Library)
(7,026 distinct tags in 272 colours as of 27 October 2026).

### From OpenAMS (the default)

OpenAMS serves the library from the printer host, through Moonraker: its
`openams_taglib` component, which the OpenAMS installer sets up. Nothing is
stored on the board, so the 4 inch board gets the whole library too.

- The display finds OpenAMS at the Moonraker address it already uses
  (`moonraker ws://host:port/websocket`).
- The first time the library is opened, OpenAMS downloads it (about 200 MB);
  the display says so meanwhile. OpenAMS keeps it there and updates it on
  request (`POST /server/openams/taglib/update`).
- OpenAMS remembers which tags were cloned, so every display on the printer
  offers a UID not cloned before.
- `tag libadd` (and `tools/tagtool.py libupload`) add a tag to the library on
  OpenAMS.
- Each page of the library is one request to the printer: the display waits
  for it, a few seconds at most when the printer does not answer, and then
  says why the list is empty.

### On the board (5 inch only)

The 5 inch board can keep the library in its own flash (7,475 tags, enough
for all of it), to clone without the printer: **Tag library → On the board** in
`make menuconfig`, or `-DMMU_TAG_LIBRARY=board`. Fill it from a checkout of
the library over USB:

```sh
python tools/tagtool.py libflash --lib ~/Bambu-Lab-RFID-Library
```

From `platform/esp32`, `make` can do it for you: turn on **Write the Bambu Lab
tag library with make flash** in `make menuconfig` and say where the checkout
is (`make` clones it there the first time, about 200 MB). `make flash` then
writes the library after the firmware, but only when it changed since the last
flash, which saves a minute. `make libupdate` pulls the newest tags into the
checkout, and `make libflash` writes the library now.

`libflash` reads the display's partition table, builds the whole library and
flashes it in about a minute (through the bootloader, so the display restarts;
run it from an ESP-IDF shell, which has esptool). It replaces what was there.
When everything does not fit, it takes every colour first, then a second UID
of each, and so on. A query (`libflash --lib DIR "PLA Matte"`), `--per-color N`
and `--limit N` take less.

`libupload` adds tags one at a time over the console instead (about two
seconds each), and keeps what is there. A library written by an older firmware
(one tag per 4 KB) reads as empty: fill it again with `libflash`, or run
`tag libclear yes` before `libupload`.

## License

The tag writer (`platform/esp32/tag_writer/`) is under the GNU GPLv3: several
of its format codecs are based on GPLv3 projects (OpenRFID and others). The
rest of the display stays under the repository's MIT license. An image built
with the tag writer is therefore a GPLv3 work as a whole, and one built with
`-DMMU_SPOOL_TAG_WRITER=OFF` is the MIT display alone.
`platform/esp32/tag_writer/THIRD_PARTY.md` lists each codec's sources and the
lookup tables' origins.
