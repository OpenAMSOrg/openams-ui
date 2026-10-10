/**
 * @file panel_config.h
 * Compile-time overrides for the CYD-style board's panel, so a bring-up
 * problem (wrong colours, mirrored image, failed ID read) can be worked
 * around by rebuilding with one of these redefined, without touching
 * display.c.
 *
 * Confirmed hardware (LCDWiki "4.0inch ESP32-32E display", E32R40T):
 * ST7796S controller, 320x480 native panel, XPT2046 resistive touch.
 */
#ifndef PANEL_CONFIG_H
#define PANEL_CONFIG_H

/* -1 = try inversion ON first (the usual default for this IPS-style
 * ST7796S panel); 0/1 forces INVOFF/INVON regardless. */
#ifndef PANEL_FORCE_INVERT
#define PANEL_FORCE_INVERT 0 /* owner confirmed: OFF looks right on this panel */
#endif

/* -1 = BGR on (this board's usual panel wiring); 0/1 forces the MADCTL
 * BGR bit. */
#ifndef PANEL_FORCE_BGR
#define PANEL_FORCE_BGR (-1)
#endif

/* ---- orientation --------------------------------------------------------
 * MADCTL rotation bits (MY=0x80, MX=0x40, MV=0x20) to turn the panel's
 * native 320x480 (portrait) addressing into 480x320 (landscape). 0x20 (MV
 * only) is the first guess. If the image is portrait, MV is wrong for this
 * panel's native orientation. If it is landscape but mirrored or upside
 * down, try 0x60 (MV|MX), 0xA0 (MV|MY), or 0xE0 (MV|MX|MY) next. */
#ifndef PANEL_ROTATION_BITS
#define PANEL_ROTATION_BITS 0x20
#endif

/* ---- SPI clock ----------------------------------------------------------
 * 80 MHz: the panel's pins (SCK14 MOSI13 CS15) are HSPI's own IOMUX pins, so
 * the bus can run at the APB clock; the ESP32 divides 80 MHz by a whole
 * number, so the steps below it are 40 and 26.7. A full frame takes about
 * 31 ms at 80 (61 at 40). If the image smears or tears, go back to 40. */
#ifndef PANEL_SPI_HZ
#define PANEL_SPI_HZ (80 * 1000 * 1000)
#endif

#endif /* PANEL_CONFIG_H */
