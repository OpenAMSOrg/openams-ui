// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

/**
 * @file tag_writer.h
 * The spool tag writer, built into the ESP32 image when MMU_SPOOL_TAG_WRITER
 * is on (the default): compose a filament spool and write it to an RFID tag,
 * read any filament tag, reset, copy and clone Bambu tags. The reader is a
 * PN5180 (default) or an MFRC522 (-DMMU_TAG_READER=MFRC522) on the board's
 * VSPI connector; see docs/guide/tag-writer.md.
 *
 * The display reaches it from the menu (screen_tag_writer.c) and from its
 * serial console ("tag <command>").
 */
#ifndef TAG_WRITER_H
#define TAG_WRITER_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Loads the spool being edited and the Bambu master key from flash, and the
 * tag library index when the image keeps its own. NVS must be initialised.
 * Touches no hardware: the reader comes up when the screen opens or a console
 * command needs it. */
void tag_writer_init(void);

/** The display's Moonraker WebSocket URL ("ws://host:port/websocket", or ""
 * when none is stored): the tag library is served by OpenAMS there, unless the
 * image keeps its own (MMU_TAG_LIBRARY=board). After tag_writer_init(). */
void tag_writer_set_moonraker(const char *ws_url);

/** Builds the tag writer into @p root, a screen object the display creates and
 * loads. @p on_exit runs, on the LVGL task, when the user leaves; the display
 * then deletes @p root, and the tag writer lets go of the reader. LVGL task
 * only. */
void tag_writer_open(lv_obj_t *root, void (*on_exit)(void));

/** Runs one console command ("status", "decode", "key <hex>", "help", ...;
 * the line after "tag ") and prints its output. Blocks until it is done. It
 * runs on a short-lived task of its own, so the caller needs no extra stack.
 * Not from the LVGL task. */
void tag_writer_console(const char *line);

/** The reader this image drives: "PN5180" or "MFRC522". */
const char *tag_writer_reader_name(void);

/** True while the reader's task is alive: its stack, session buffers and
 * format codecs are still on the heap. It goes away soon after the screen
 * closes or a console command ends; the printer link waits for it before it
 * rebuilds its own buffers. Any task. */
bool tag_writer_reader_active(void);

#ifdef __cplusplus
}
#endif

#endif /* TAG_WRITER_H */
