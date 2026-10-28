// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

/**
 * @file tag_writer.cpp
 * The C entry points of tag_writer.h: boot-time state, the screen, and console
 * commands on a task of their own.
 */
#include "tag_writer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "reader.h"
#include "spool_store.h"
#include "tag_service.h"
#include "taglib.h"
#include "ui.h"

static const char *TAG = "tag_writer";

#if defined(MMU_BOARD_ES3C50P)
// The ES3C50P (5" ESP32-S3) has two free GPIOs, 17 and 18, on its "Expand"
// connectors, and 18 also reaches the touch controller's interrupt through
// R16, a 0R link removed for the reader (docs/guide/tag-writer.md, which also
// has R12 to 0R for the PN5180's 5 V; main/touch_gt911.c leaves IO18 alone). The bus is the one on the "SPI"
// connector, shared with the microSD slot (SCK IO12, MOSI IO11, MISO IO13; the
// slot must stay empty), whose own chip select, IO19, is the touch
// controller's I2C data. So IO17 is the reader's chip select and IO18 the
// PN5180's BUSY or the MFRC522's reset. The PN5180's reset is tied high (it
// starts on power-up) and the MFRC522's IRQ is not used.
#if defined(TW_READER_MFRC522)
static const Reader::Pins READER_PINS = {
    SPI3_HOST, /*sck*/ 12, /*mosi*/ 11, /*miso*/ 13, /*cs*/ 17, /*rst*/ 18, /*irq*/ -1,
    /*clock_hz*/ 4 * 1000 * 1000,
};
#else
static const Reader::Pins READER_PINS = {
    SPI3_HOST, /*sck*/ 12, /*mosi*/ 11, /*miso*/ 13, /*cs*/ 17, /*busy*/ 18, /*rst*/ -1,
    /*clock_hz*/ 4 * 1000 * 1000,
};
#endif
#else
// The reader on the board's VSPI connector (P3: MOSI IO23, MISO IO19, SCK IO18,
// CS IO21; shared with the microSD slot, which must stay empty) plus reset on
// IO32 and the PN5180's BUSY (or the MFRC522's IRQ) on IO35, input only. The
// panel and touch are on HSPI; nothing else uses these pins.
#if defined(TW_READER_MFRC522)
static const Reader::Pins READER_PINS = {
    SPI3_HOST, /*sck*/ 18, /*mosi*/ 23, /*miso*/ 19, /*cs*/ 21, /*rst*/ 32, /*irq*/ 35,
    /*clock_hz*/ 4 * 1000 * 1000,
};
#else
static const Reader::Pins READER_PINS = {
    SPI3_HOST, /*sck*/ 18, /*mosi*/ 23, /*miso*/ 19, /*cs*/ 21, /*busy*/ 35, /*rst*/ 32,
    /*clock_hz*/ 4 * 1000 * 1000,
};
#endif
#endif

/* A console command runs on its own task: the display's console task has 3 KB
 * of stack, a command needs about 5. The stack is borrowed from the heap for
 * the length of the command only. */
#define COMMAND_STACK 6144
#define COMMAND_PRIORITY 3

namespace {

struct Command {
    char line[160];
    SemaphoreHandle_t done;
};

void command_task(void *arg) {
    Command *cmd = static_cast<Command *>(arg);
    esp_task_wdt_add(nullptr);
    if (tagsvc::acquire())
        tagsvc::wait_first_poll(1500);  // so `status` / `scan` see a tag that is there
    console::run(cmd->line);
    printf("> \n");  // the end of the reply, for tools/tagtool.py
    fflush(stdout);
    tagsvc::release();
    esp_task_wdt_delete(nullptr);
    xSemaphoreGive(cmd->done);
    vTaskDelete(nullptr);
}

}  // namespace

extern "C" void tag_writer_init(void) {
    spool::init();
    taglib::init();
    uint8_t key[bambu::MASTER_KEY_SIZE] = {0};
    // The Bambu master key is never compiled in: it is stored by "tag key <hex>".
    if (!spool::load_key(key))
        ESP_LOGI(TAG, "no Bambu master key stored (tag key <32 hex>)");
    tagsvc::init(READER_PINS, key);
    if (!taglib::has_partition())
        // No taglib partition: this image is running on a display still using
        // the two-OTA-slot table (an over-the-air update across layouts). The
        // writer works, but capture/clone from the on-device library do not;
        // reflash over USB to get the writer's partition table.
        ESP_LOGW(TAG, "no tag-library partition: flash the tag-writer image over USB "
                      "(this looks like the display's two-OTA-slot layout)");
    ESP_LOGI(TAG, "spool tag writer ready (" TW_READER_NAME "), library %d of %d slots",
             taglib::count(), taglib::slot_count());
}

extern "C" void tag_writer_open(lv_obj_t *root, void (*on_exit)(void)) {
    ui::open(root, on_exit);
}

extern "C" void tag_writer_console(const char *line) {
    Command *cmd = static_cast<Command *>(calloc(1, sizeof(Command)));
    if (!cmd) {
        printf("ERROR no memory for the command\n");
        return;
    }
    snprintf(cmd->line, sizeof(cmd->line), "%s", line ? line : "");
    cmd->done = xSemaphoreCreateBinary();
    if (!cmd->done ||
        xTaskCreatePinnedToCore(command_task, "tagcmd", COMMAND_STACK, cmd, COMMAND_PRIORITY,
                                nullptr, 0) != pdPASS) {
        printf("ERROR no memory for the command\n");
        if (cmd->done)
            vSemaphoreDelete(cmd->done);
        free(cmd);
        return;
    }
    xSemaphoreTake(cmd->done, portMAX_DELAY);
    vSemaphoreDelete(cmd->done);
    free(cmd);
}

extern "C" const char *tag_writer_reader_name(void) {
    return TW_READER_NAME;
}

extern "C" bool tag_writer_reader_active(void) {
    return tagsvc::running();
}
