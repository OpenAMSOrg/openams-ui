# Spool tag writer

## What it is and why

The display can also write the RFID tags your spools carry. With a reader
wired to the board, **Menu → Tag writer** opens a screen that:

- composes a spool (material preset, colour, weight, temperatures, drying)
  and writes it to a tag in the Bambu Lab layout, keyed so OpenAMS reads it;
- reads any filament tag the reader can see and shows what it says, with
  "Use these settings" to copy it into the spool you are composing;
- resets a tag it wrote back to factory keys, copies a genuine Bambu tag and
  clones it onto a magic tag (for a stock AMS), from the tag itself or from an
  on-device library of captured tags.

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

`MMU_LANGUAGES` works as for every image (see the [README](../../README.md)).
`make menuconfig` has the same choices in a menu
([flash-and-setup.md](flash-and-setup.md#install)).

An image with the tag writer has its own partition table,
`platform/esp32/partitions_tagwriter.csv`: one app partition and the tag
library (1,599 tags), instead of the two OTA slots. A 4 MB flash holds
one or the other, so **an image with the tag writer is updated over USB**,
not with `ota`. Its settings live in a separate generated `sdkconfig.tagwriter`,
so switching the option needs no clean-up. Flashing over USB from one layout to
the other keeps your Wi-Fi, Moonraker and touch settings (NVS does not move).

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
| Clone | clone a tag from the on-device library onto a magic tag; each clone takes a UID of that colour not cloned before, so two spools never share one |
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
| `tag libadd`, `tag liblist`, `tag libclear yes` | add one tag to the on-device library, list it, erase it |
| `tag key <32 hex>` / `tag key clear` | store or forget the Bambu master key |

`tools/tagtool.py` drives these from a PC: `clonefrom` clones one tag from a
folder of dumps, `cmd` sends any command.

## The on-device library

The library holds genuine tag dumps to clone from the touchscreen: 1,599 on
the 4 inch board, 7,475 on the 5 inch one, which holds every dump in the
[Bambu-Lab-RFID-Library](https://github.com/queengooborg/Bambu-Lab-RFID-Library)
(7,026 distinct tags in 272 colours as of 27 October 2026). Fill it from a
checkout of that library over USB:

```sh
python tools/tagtool.py libflash --lib ~/Bambu-Lab-RFID-Library
```

From `platform/esp32`, `make` can do it for you: turn on **Bambu Lab tag
library** in `make menuconfig` and say where the checkout is (`make` clones it
there the first time, about 200 MB). `make flash` then writes the library after
the firmware, but only when it changed since the last flash, which saves a
minute. `make libupdate` pulls the newest tags into the checkout, and
`make libflash` writes the library now.

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
