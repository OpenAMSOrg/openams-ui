# Hardware

## What it is and why

The firmware targets a "Cheap Yellow Display" style board: an ESP32 with a
4.0 inch resistive touch panel on one board. It is cheap, needs one USB cable
and has everything the display needs. The code in `platform/esp32/main` is the
authority for what follows; if a board differs, the code wins.

## Board

| Part | Value |
|------|-------|
| Module | ESP32-WROOM-32E (the code comments name the LCDWiki "4.0inch ESP32-32E display", E32R40T) |
| Memory | 520 KB SRAM, no PSRAM |
| Flash | 4 MB, DIO mode, 40 MHz (`sdkconfig.defaults`) |
| Panel | ST7796S controller, 320x480 native, driven as 480x320 landscape |
| Touch | XPT2046 resistive controller |
| Backlight | PWM on GPIO27, 5 kHz, 10-bit (LEDC) |

## Wiring as the code defines it

The panel and the touch controller share one SPI bus (`SPI2_HOST`, also called
HSPI). Each has its own chip select.

| Signal | GPIO | Notes |
|--------|------|-------|
| SCK | 14 | shared by panel and touch |
| MOSI | 13 | shared |
| MISO | 12 | shared |
| Panel CS | 15 | panel runs at 40 MHz (`PANEL_SPI_HZ`) |
| Panel DC | 2 | data/command select |
| Backlight | 27 | LEDC PWM |
| Touch CS | 33 | touch runs at 1 MHz |
| Touch IRQ | 36 | read as a plain input |

The panel has no reset GPIO. The driver resets it with the software reset
command (`SWRESET`, 0x01) and then reads its ID (`RDID4`, 0xD3). A healthy
ST7796S answers `0x007796`.

These pins are fixed in `display.c` and `touch.c`. Moving them means editing
the source and rebuilding.

The spool tag writer's reader (a PN5180 or an MFRC522) goes on the other SPI
bus, VSPI: SCK 18, MOSI 23, MISO 19, CS 21, reset 32, and the PN5180's BUSY
(or the MFRC522's IRQ) on 35. Those lines also reach the microSD slot, so leave
it empty. Wiring per connector pin: [tag-writer.md](tag-writer.md#wiring).

## Panel options

`platform/esp32/main/panel_config.h` holds compile-time overrides for a panel
that shows wrong colors, a mirrored image or garbage:

| Macro | Default | What it does |
|-------|---------|--------------|
| `PANEL_FORCE_INVERT` | `0` | `-1` tries inversion on first; `0` or `1` forces it off or on |
| `PANEL_FORCE_BGR` | `-1` | `-1` is BGR on; `0` or `1` forces the MADCTL BGR bit |
| `PANEL_ROTATION_BITS` | `0x20` | MADCTL bits (MY 0x80, MX 0x40, MV 0x20) for landscape |
| `PANEL_SPI_HZ` | 40 MHz | lower to `20000000` if the image smears |

Override one with a compiler define, or edit the header. See
[troubleshooting.md](troubleshooting.md) for which to try.

## The 5 inch board (ES3C50P)

The firmware also builds for a 5 inch board, the LCDWiki "5.0inch ESP32-S3
display" ES3C50P (sold as Hosyond), capacitive version, with
`-DMMU_BOARD=es3c50p` ([flash-and-setup.md](flash-and-setup.md#the-5-inch-board)).
Its code is `display_rgb.c` and `touch_gt911.c`; `main/board.h` says what each
board has. The UI is the same, drawn at 800x480 ([VISUAL_SPEC_800.md](../VISUAL_SPEC_800.md)).

| Part | Value |
|------|-------|
| Module | ESP32-S3 (QFN56), dual core, 240 MHz |
| Memory | 512 KB SRAM, 8 MB octal PSRAM (the frame buffer, LVGL's pool, the program) |
| Flash | 16 MB, quad I/O, 80 MHz: two OTA slots and the tag library (`partitions_es3c50p.csv`) |
| Panel | ST7282E, 800x480, 16-bit RGB, no command interface |
| Touch | GT911 capacitive controller on I2C, address 0x5D: no calibration |
| Backlight | PWM on GPIO2 (an SY7200 boost driver) |
| USB | CH340 serial (`/dev/ttyUSB*`), auto reset into the bootloader |

| Signal | GPIO |
|--------|------|
| DE, VSYNC, HSYNC, PCLK | 40, 41, 39, 42 |
| R0-R4 | 45, 48, 47, 21, 14 |
| G0-G5 | 5, 6, 7, 15, 16, 4 |
| B0-B4 | 8, 3, 46, 9, 1 |
| Touch SDA, SCL, INT, RST | 19, 20, 18, 38 |
| microSD CS, MOSI, MISO, SCK | 10, 11, 13, 12 |
| Speaker I2S LRCLK, DATA, BCLK | 18, 17, 0 (not used) |

The image is turned 180 degrees from the panel's own scan order
(`BOARD_ROTATED_180`); the display settings' flip turns it from there.

The board has two free GPIOs, 17 and 18, and 18 is the touch controller's
interrupt. So the tag writer's reader shares the microSD slot's SPI bus (SCK 12,
MOSI 11, MISO 13, CS 10; leave the slot empty), and GPIO17, on the "Expand"
connector, is the PN5180's BUSY or the MFRC522's reset. The PN5180's reset is
tied high. This wiring is not yet tried on hardware.

## Requirements

- A USB cable and a computer with a USB serial driver for the board.
- A 2.4 GHz Wi-Fi network the printer's host can also reach.
- A printer running Klipper, Moonraker and the OpenAMS plugin.

## Gotchas

- The touch controller shares the display's bus. Do not wire another device to
  GPIO12, 13 or 14 without checking the bus load.
- Some boards of this family carry a different panel (an ILI9341 at 320x240).
  This firmware supports the ST7796S only; the log says so when the ID does not
  match.
- The firmware has no Bluetooth (`CONFIG_BT_ENABLED=n`).

## Where next

[Flash and set it up](flash-and-setup.md).
