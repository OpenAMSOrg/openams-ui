// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

/**
 * @file console.cpp
 * The tag writer's serial commands, run from the display's own console as
 * "tag <command>" (platform/esp32/main/net_console.c), so they stay
 * scriptable from a PC (tools/tagtool.py).
 */
#include "console.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "spool_store.h"
#include "tag_ops.h"
#include "taglib.h"
#include "tag_service.h"

namespace console {

static const uart_port_t UART = UART_NUM_0;

static const char HELP[] =
    "tag writer commands (type \"tag <command>\"):\n"
    "  status                 reader, key and spool\n"
    "  spool                  current spool as JSON\n"
    "  presets                list material presets\n"
    "  preset <n>             apply material preset n\n"
    "  set <field> <value>    type detail code color weight diameter\n"
    "                         nozzle_min nozzle_max bed dry_temp dry_hours\n"
    "                         width length density\n"
    "  date <YYYY_MM_DD_HH_MM> production date written to block 12\n"
    "  spitest                reader self-test (SPI, reset, field)\n"
    "  magic                  identify a magic tag (gen1a / gen2 / gen3 / normal)\n"
    "  capture                read a genuine tag into the image buffer\n"
    "  loadimage              load the image buffer from 64 hex lines (PC lib)\n"
    "  image                  print the image buffer (64 hex lines)\n"
    "  clone                  write the image buffer onto a magic tag\n"
    "  libadd <mat>|<color>   store a genuine tag in the on-device library\n"
    "  liblist / libcount     the on-device library\n"
    "  libclear yes           empty the library\n"
    "  scan                   the tag on the reader, if any\n"
    "  read                   decode the tag as JSON (Bambu layout)\n"
#if defined(TW_READER_UNIVERSAL)
    "  decode                 any tag type, any filament format\n"
    "  encode <format> [k=v]  write the spool in <format> to the tag\n"
#endif
    "  dump                   all 64 blocks as hex\n"
    "  write                  write the spool to the tag (waits 10 s)\n"
    "  erase yes              reset a blank/custom tag to factory keys\n"
    "  key <32 hex> | clear   store the Bambu master key in NVS\n";

static void print_json_string(const char *s) {
    putchar('"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            putchar('\\');
            putchar(c);
        } else if (c < 0x20) {
            printf("\\u%04x", c);  // control bytes: keep the reply valid JSON
        } else {
            putchar(c);
        }
    }
    putchar('"');
}

static void print_spool() {
    bambu::Spool s;
    spool::get(s);
    printf("{\"type\":");
    print_json_string(s.type);
    printf(",\"detail\":");
    print_json_string(s.detail);
    printf(",\"code\":");
    print_json_string(s.code);
    printf(",\"color_rgba\":\"%08lX\",\"weight_g\":%u,\"diameter_x100\":%u,"
           "\"nozzle_min_c\":%u,\"nozzle_max_c\":%u,\"bed_c\":%u,\"dry_c\":%u,"
           "\"dry_hours\":%u,\"density_x100\":%u,\"width_x100\":%u,\"length_m\":%u,"
           "\"date\":\"%s\"}\n",
           (unsigned long)s.rgba, s.weight_g, s.diameter_x100, s.nozzle_min_c, s.nozzle_max_c,
           s.bed_c, s.dry_c, s.dry_hours, s.density_x100, s.width_x100,
           bambu::estimate_length_m(s), spool::date());
}

static void print_card(const tagops::Card &card) {
    char uid[21];
    tagops::format_uid(card.uid, uid);
    printf("{\"uid\":\"%s\",\"sak\":%u,\"atqa\":\"%02X%02X\",\"kind\":\"%s\"}\n", uid,
           card.uid.sak, card.uid.atqa[0], card.uid.atqa[1], tagops::kind_name(card.kind));
}

static void print_result(const tagops::Result &r) {
    printf("%s %s", r.ok ? "OK" : "ERROR", r.message ? r.message : "");
    if (r.sector != 0xFF)
        printf(" sector=%u status=%s", r.sector, Reader::status_name(r.status));
    printf("\n");
}

// A tag image (1 KB) for one command, on the heap and only while the command
// runs: the display needs its RAM the rest of the time.
struct ImageBuffer {
    uint8_t *data = static_cast<uint8_t *>(malloc(1024));
    ~ImageBuffer() { free(data); }
};

// Runs one job through the reader task and waits for it to finish.
static bool run_job(tagsvc::Op op, uint32_t wait_ms, tagsvc::Snapshot &out) {
    if (!tagsvc::ready()) {
        printf("ERROR the reader could not start (not enough memory)\n");
        return false;
    }
    bambu::Spool s;
    spool::get(s);
    if (!tagsvc::submit(op, tagsvc::OWNER_CONSOLE, &s, spool::date(), wait_ms)) {
        printf("ERROR reader busy\n");
        return false;
    }
    // A deadline so a job that never reaches DONE (a stuck reader, a job the
    // service dropped) cannot hang the console task forever. The reader waits
    // up to wait_ms for a tag; allow a margin on top for the work itself.
    uint8_t last_done = 0;
    int64_t deadline = esp_timer_get_time() / 1000 + (int64_t)wait_ms + 5000;
    bool done = false;
    for (;;) {
        esp_task_wdt_reset();
        tagsvc::snapshot(out);
        if (out.owner == tagsvc::OWNER_CONSOLE && out.phase == tagsvc::DONE) {
            done = true;
            break;
        }
        if (esp_timer_get_time() / 1000 > deadline) {
            tagsvc::cancel(tagsvc::OWNER_CONSOLE);
            break;
        }
        if (out.done != last_done) {
            last_done = out.done;
            putchar(last_done % 16 ? '.' : '|');
            fflush(stdout);
        }
        vTaskDelay(pdMS_TO_TICKS(40));
    }
    if (last_done)
        putchar('\n');
    if (!done) {
        printf("ERROR timed out waiting for the reader\n");
        return false;
    }
    tagsvc::acknowledge();
    return true;
}

static void cmd_read() {
    tagsvc::Snapshot snap;
    if (!run_job(tagsvc::OP_READ, 3000, snap))
        return;
    if (!snap.result.ok) {
        if (snap.job_card.kind != tagops::NONE)
            print_card(snap.job_card);
        print_result(snap.result);
        return;
    }
    const bambu::TagInfo &info = snap.info;
    char uid[21];
    tagops::format_uid(snap.job_card.uid, uid);
    printf("{\"uid\":\"%s\",\"kind\":\"%s\",\"type\":", uid, tagops::kind_name(snap.job_card.kind));
    print_json_string(info.type);
    printf(",\"detailed\":");
    print_json_string(info.detail);
    printf(",\"code\":");
    print_json_string(info.code);
    printf(",\"variant\":");
    print_json_string(info.variant);
    printf(",\"color_rgba\":\"%08lX\",\"weight_g\":%u,\"diameter_x100\":%u,"
           "\"drying_temp_c\":%u,\"drying_time_h\":%u,\"bed_temp_c\":%u,"
           "\"hotend_max_c\":%u,\"hotend_min_c\":%u,\"width_x100\":%u,\"length_m\":%u,"
           "\"production\":",
           (unsigned long)info.rgba, info.weight_g, info.diameter_x100, info.dry_c,
           info.dry_hours, info.bed_c, info.nozzle_max_c, info.nozzle_min_c, info.width_x100,
           info.length_m);
    print_json_string(info.production);
    printf(",\"tray_uid\":\"");
    for (int i = 0; i < 16; i++)
        printf("%02x", info.tray_uid[i]);
    printf("\"}\n");
}

#if defined(TW_READER_UNIVERSAL)
static void print_json_kv_str(const char *key, const std::string &val) {
    printf(",\"%s\":", key);
    print_json_string(val.c_str());
}

static void print_json_kv_int(const char *key, long val) {
    if (val != 0)
        printf(",\"%s\":%ld", key, val);
}

// Universal decode: detect any supported tag type and report the format and
// the normalized filament fields it decoded (parity with klipper_rf).
static void cmd_decode() {
    tagsvc::Snapshot snap;
    if (!run_job(tagsvc::OP_DECODE, 3000, snap))
        return;
    DecodeResult dr;
    char tag_type[24];
    bool ok = false;
    tagsvc::get_decode(dr, tag_type, &ok);
    if (!ok) {
        printf("ERROR no tag on the reader\n");
        return;
    }
    char uid[21];
    tagops::format_uid(snap.job_card.uid, uid);
    printf("{\"uid\":\"%s\",\"tag_type\":\"%s\",\"format\":", uid, tag_type);
    print_json_string(dr.format.c_str());
    printf(",\"recognized\":%s", dr.recognized ? "true" : "false");
    const Filament &f = dr.filament;
    print_json_kv_str("brand", f.brand);
    print_json_kv_str("material", f.material);
    print_json_kv_str("name", f.name);
    if (!f.color_hex.empty())
        print_json_kv_str("color_hex", f.color_hex);
    if (f.diameter_mm != 0.0)
        printf(",\"diameter_mm\":%.2f", f.diameter_mm);
    if (f.density != 0.0)
        printf(",\"density\":%.3f", f.density);
    print_json_kv_int("weight_g", f.weight_g);
    print_json_kv_int("spool_weight_g", f.spool_weight_g);
    print_json_kv_int("length_m", f.length_m);
    print_json_kv_int("nozzle_min", f.nozzle_min);
    print_json_kv_int("nozzle_max", f.nozzle_max);
    print_json_kv_int("bed_min", f.bed_min);
    print_json_kv_int("bed_max", f.bed_max);
    print_json_kv_int("drying_temp", f.drying_temp);
    print_json_kv_int("drying_time_min", f.drying_time_min);
    if (!f.sku.empty())
        print_json_kv_str("sku", f.sku);
    if (!f.spool_id.empty())
        print_json_kv_str("spool_id", f.spool_id);
    for (const auto &kv : dr.info)
        print_json_kv_str(kv.first.c_str(), kv.second);
    printf("}\n");
}

// Map the spool currently being edited onto the universal Filament model so
// it can be re-encoded in any writable format.
static void spool_to_filament(const bambu::Spool &s, Filament &f) {
    f = Filament();
    f.material = s.type;
    if (s.detail[0]) {
        f.name = s.detail;
        f.fields["subtype"] = s.detail;
    }
    f.sku = s.code;
    char hex[7];
    snprintf(hex, sizeof(hex), "%02X%02X%02X",
             (unsigned)((s.rgba >> 24) & 0xFF),
             (unsigned)((s.rgba >> 16) & 0xFF),
             (unsigned)((s.rgba >> 8) & 0xFF));
    f.color_hex = hex;
    f.colors.push_back(hex);
    f.nozzle_min = s.nozzle_min_c;
    f.nozzle_max = s.nozzle_max_c;
    f.bed_max = s.bed_c;
    f.drying_temp = s.dry_c;
    f.drying_time_min = s.dry_hours * 60;
    f.weight_g = s.weight_g;
    f.diameter_mm = s.diameter_x100 / 100.0;
    f.density = s.density_x100 / 100.0;
    f.length_m = s.length_m;
}

// Merge one key=value pair into the Filament (so formats that need fields the
// Bambu spool model lacks, e.g. spoolman's spool_id or opentag3d's brand, can
// be supplied: `encode spoolman spool_id=42`).
static void encode_set_field(Filament &f, const char *k, const char *v) {
    std::string key(k), val(v);
    if (key == "brand") f.brand = val;
    else if (key == "spool_id") f.spool_id = val;
    else if (key == "name") f.name = val;
    else if (key == "sku") f.sku = val;
    else if (key == "material") f.material = val;
    else if (key == "color_hex") { f.color_hex = val; f.colors.clear(); f.colors.push_back(val); }
    else f.fields[key] = val;
}

// Universal write: encode the current spool in <format> and write it to the
// tag (parity with klipper_rf codec encode()). Pairs with `decode`. Extra
// key=value tokens are merged into the fields before encoding.
static void cmd_encode(char *args) {
    char fmt[24] = {0};
    if (!args || sscanf(args, "%23s", fmt) != 1) {
        printf("ERROR usage: encode <format> [field=value ...]  "
               "(openspool openprinttag spoolman opentag3d anycubic tigertag "
               "creality qidi)\n");
        return;
    }
    bambu::Spool s;
    spool::get(s);
    Filament f;
    spool_to_filament(s, f);
    // Parse optional key=value tokens after the format name.
    char *p = args;
    while (*p && !isspace((unsigned char)*p)) p++;  // skip the format token
    char *save = nullptr;
    for (char *tok = strtok_r(p, " \t", &save); tok; tok = strtok_r(nullptr, " \t", &save)) {
        char *eq = strchr(tok, '=');
        if (eq) { *eq = 0; encode_set_field(f, tok, eq + 1); }
    }
    tagsvc::set_write_request(fmt, f);
    tagsvc::Snapshot snap;
    if (!run_job(tagsvc::OP_WRITE_UNIVERSAL, 6000, snap))
        return;
    bool ok = false;
    int written = 0;
    char msg[96];
    tagsvc::get_write_result(&ok, &written, msg, sizeof(msg));
    printf("{\"ok\":%s,\"written\":%d,\"message\":", ok ? "true" : "false", written);
    print_json_string(msg);
    printf("}\n");
}
#endif

static void cmd_dump() {
    tagsvc::Snapshot snap;
    if (!run_job(tagsvc::OP_DUMP, 3000, snap))
        return;
    if (snap.job_card.kind == tagops::NONE) {
        print_result(snap.result);
        return;
    }
    print_card(snap.job_card);
    ImageBuffer buffer;
    if (!buffer.data) {
        printf("ERROR no memory\n");
        return;
    }
    uint8_t *image = buffer.data;
    bool ok[64];
    tagsvc::dump_result(image, ok);
    for (int block = 0; block < 64; block++) {
        printf("BLOCK %02d ", block);
        if (ok[block]) {
            for (int i = 0; i < 16; i++)
                printf("%02X", image[block * 16 + i]);
        } else {
            printf("????????????????????????????????");
        }
        printf("\n");
    }
    print_result(snap.result);
}

static void cmd_write_or_erase(tagsvc::Op op) {
    tagsvc::Snapshot snap;
    if (op == tagsvc::OP_WRITE)
        printf("waiting for tag...\n");
    if (!run_job(op, op == tagsvc::OP_WRITE ? 10000 : 3000, snap))
        return;
    if (snap.job_card.kind != tagops::NONE)
        print_card(snap.job_card);
    print_result(snap.result);
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void cmd_key(const char *arg) {
    if (!strcmp(arg, "clear")) {
        static const uint8_t none[bambu::MASTER_KEY_SIZE] = {0};
        spool::clear_key();
        tagsvc::set_master_key(none);
        printf("OK key cleared\n");
        return;
    }
    uint8_t key[bambu::MASTER_KEY_SIZE];
    int n = 0, high = -1;
    for (; *arg && n < (int)sizeof(key); arg++) {
        int v = hex_value(*arg);
        if (v < 0)
            continue;
        if (high < 0) {
            high = v;
        } else {
            key[n++] = (uint8_t)(high << 4 | v);
            high = -1;
        }
    }
    if (n != (int)sizeof(key)) {
        printf("ERROR key needs 32 hex characters\n");
        return;
    }
    spool::save_key(key);
    tagsvc::set_master_key(key);
    vTaskDelay(pdMS_TO_TICKS(600));
    tagsvc::Snapshot snap;
    tagsvc::snapshot(snap);
    printf("OK key stored, fingerprint %s\n", snap.key_fingerprint);
}

// Reads one newline-terminated line straight from the UART (used by
// loadimage, which consumes its payload lines itself). Returns the length, or
// -1 on timeout. Resets the watchdog while it waits.
static int read_line(char *buf, size_t max, int timeout_ms) {
    size_t length = 0;
    int64_t end = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (esp_timer_get_time() < end) {
        esp_task_wdt_reset();
        char c;
        if (uart_read_bytes(UART, &c, 1, pdMS_TO_TICKS(50)) <= 0)
            continue;
        if (c == '\r')
            continue;
        if (c == '\n') {
            buf[length] = 0;
            return (int)length;
        }
        if (length < max - 1)
            buf[length++] = c;
        end = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    }
    return -1;
}

static void cmd_magic() {
    tagsvc::Snapshot snap;
    if (!run_job(tagsvc::OP_MAGIC, 2000, snap))
        return;  // snap is not filled when the job did not run
    if (snap.job_card.kind != tagops::NONE)
        print_card(snap.job_card);
    printf("%s %s\n", snap.result.ok ? "OK" : "ERROR", snap.result.message);
}

static void cmd_capture() {
    tagsvc::Snapshot snap;
    if (!run_job(tagsvc::OP_CAPTURE, 3000, snap))
        return;
    if (snap.job_card.kind != tagops::NONE)
        print_card(snap.job_card);
    print_result(snap.result);
    if (snap.result.ok)
        printf("image loaded: %s %s (clone writes it onto a magic tag)\n",
               snap.image_info.type, snap.image_info.detail);
}

static void cmd_clone() {
    tagsvc::Snapshot snap;
    tagsvc::snapshot(snap);
    if (!snap.image_valid) {
        printf("ERROR no image loaded (use capture, or loadimage)\n");
        return;
    }
    printf("hold a magic (UID-writable) tag on the reader...\n");
    if (!run_job(tagsvc::OP_CLONE, 10000, snap))
        return;
    if (snap.job_card.kind != tagops::NONE)
        print_card(snap.job_card);
    print_result(snap.result);
}

static void cmd_image() {
    ImageBuffer buffer;
    if (!buffer.data) {
        printf("ERROR no memory\n");
        return;
    }
    uint8_t *image = buffer.data;
    bool valid = false;
    tagsvc::get_image(image, &valid);
    if (!valid) {
        printf("ERROR no image loaded\n");
        return;
    }
    printf("IMAGE %02X%02X%02X%02X\n", image[0], image[1], image[2], image[3]);
    for (int block = 0; block < 64; block++) {
        for (int i = 0; i < 16; i++)
            printf("%02X", image[block * 16 + i]);
        printf("\n");
    }
    printf("END\n");
}

// Reads 64 hex lines from the UART into `image`. Returns false on any error
// (already printed).
static bool receive_image(uint8_t image[1024]) {
    char line[40];
    for (int block = 0; block < 64; block++) {
        if (read_line(line, sizeof(line), 10000) < 0) {
            printf("ERROR timed out at block %d\n", block);
            return false;
        }
        int n = 0, high = -1;
        for (char *p = line; *p; p++) {
            int v = hex_value(*p);
            if (v < 0)
                continue;
            if (high < 0)
                high = v;
            else {
                image[block * 16 + n++] = (uint8_t)(high << 4 | v);
                high = -1;
            }
        }
        if (n != 16) {
            printf("ERROR block %d has %d bytes, need 16\n", block, n);
            return false;
        }
    }
    return true;
}

static void cmd_loadimage() {
    ImageBuffer buffer;
    if (!buffer.data) {
        printf("ERROR no memory\n");
        return;
    }
    uint8_t *image = buffer.data;
    printf("send 64 lines of 32 hex chars (one block each)\n");
    if (!receive_image(image))
        return;
    const char *why;
    if (!tagsvc::load_image(image, &why)) {
        printf("ERROR image rejected: %s\n", why);
        return;
    }
    tagsvc::Snapshot snap;
    tagsvc::snapshot(snap);
    printf("OK image loaded: %s %s (clone writes it onto a magic tag)\n", snap.image_info.type,
           snap.image_info.detail);
}

// libadd <material>|<color> then 64 image lines: store a genuine tag in the
// on-device library so it can be cloned from the touchscreen.
static void cmd_libadd(char *args) {
    char *bar = strchr(args, '|');
    if (!bar) {
        printf("ERROR usage: libadd <material>|<color>\n");
        return;
    }
    *bar = 0;
    char material[28], color[28];
    strncpy(material, args, sizeof(material) - 1);
    material[sizeof(material) - 1] = 0;
    strncpy(color, bar + 1, sizeof(color) - 1);
    color[sizeof(color) - 1] = 0;
    ImageBuffer buffer;
    if (!buffer.data) {
        printf("ERROR no memory\n");
        return;
    }
    uint8_t *image = buffer.data;
    printf("send 64 lines of 32 hex chars (one block each)\n");
    if (!receive_image(image))
        return;
    bambu::TagInfo info;
    const char *why;
    if (!tagops::image_is_valid_bambu(image, info, &why)) {
        printf("ERROR image rejected: %s\n", why);
        return;
    }
    if (!taglib::add(material, color, image)) {
        if (taglib::slot_count() == 0)
            printf("ERROR this image has no tag library partition\n");
        else
            printf("ERROR library full (%d slots)\n", taglib::slot_count());
        return;
    }
    printf("OK stored %02X%02X%02X%02X %s / %s (%d in library)\n", image[0], image[1], image[2],
           image[3], material, color, taglib::count());
}

static void copy_field(char *dst, size_t size, const char *value) {
    memset(dst, 0, size);
    strncpy(dst, value, size - 1);
}

static void cmd_set(char *args) {
    char *value = strchr(args, ' ');
    if (!value) {
        printf("ERROR usage: set <field> <value>\n");
        return;
    }
    *value++ = 0;
    char *end = nullptr;
    long number = strtol(value, &end, 10);
    bool is_num = end != value;  // value began with a number
    auto clamp = [](long v, long lo, long hi) { return v < lo ? lo : (v > hi ? hi : v); };
    bambu::Spool s;
    spool::get(s);
    if (!strcmp(args, "type")) copy_field(s.type, sizeof(s.type), value);
    else if (!strcmp(args, "detail")) copy_field(s.detail, sizeof(s.detail), value);
    else if (!strcmp(args, "code")) copy_field(s.code, sizeof(s.code), value);
    else if (!strcmp(args, "color")) {
        const char *hex = (*value == '#') ? value + 1 : value;
        size_t n = strlen(hex);
        if ((n != 6 && n != 8) || strspn(hex, "0123456789abcdefABCDEF") != n) {
            printf("ERROR color needs 6 or 8 hex digits\n");
            return;
        }
        uint32_t rgb = strtoul(hex, nullptr, 16);
        s.rgba = n > 6 ? rgb : (rgb << 8 | 0xFF);
    }
    else if (!is_num) {  // every remaining field is numeric
        printf("ERROR %s needs a number\n", args);
        return;
    }
    else if (!strcmp(args, "weight")) { s.weight_g = clamp(number, 0, 5000); s.length_m = 0; }
    else if (!strcmp(args, "diameter")) {
        long d = strchr(value, '.') ? (long)(atof(value) * 100 + 0.5) : number;
        s.diameter_x100 = clamp(d, 100, 400);
        s.length_m = 0;
    }
    else if (!strcmp(args, "nozzle_min")) s.nozzle_min_c = clamp(number, 150, 400);
    else if (!strcmp(args, "nozzle_max")) s.nozzle_max_c = clamp(number, 150, 450);
    else if (!strcmp(args, "bed")) s.bed_c = clamp(number, 0, 130);
    else if (!strcmp(args, "dry_temp")) s.dry_c = clamp(number, 0, 95);
    else if (!strcmp(args, "dry_hours")) s.dry_hours = clamp(number, 0, 48);
    else if (!strcmp(args, "width")) s.width_x100 = clamp(number, 2000, 12000);
    else if (!strcmp(args, "length")) s.length_m = clamp(number, 0, 65535);
    else if (!strcmp(args, "density")) { s.density_x100 = clamp(number, 0, 65535); s.length_m = 0; }
    else {
        printf("ERROR unknown field\n");
        return;
    }
    spool::set(s);
    print_spool();
}

static void cmd_status() {
    tagsvc::Snapshot snap;
    tagsvc::snapshot(snap);
    printf("reader " TW_READER_NAME " version=0x%02X%s irq=%d, task stack left %u bytes\n",
           snap.version, snap.reader_ok ? "" : " (not responding)", snap.irq,
           tagsvc::stack_left());
    if (snap.key_set)
        printf("master key fingerprint %s\n", snap.key_fingerprint);
    else
        printf("master key NOT SET (key <32 hex>)\n");
    if (snap.present)
        print_card(snap.card);
    else
        printf("no tag on the reader\n");
    print_spool();
}

static void cmd_presets() {
    bambu::Preset p;
    for (uint8_t i = 0; i < bambu::preset_count(); i++) {
        bambu::preset_get(i, p);
        printf("%2u %-10s %-8s %s nozzle %u-%u bed %u dry %u/%uh\n", i, p.detail, p.type, p.code,
               p.nozzle_min_c, p.nozzle_max_c, p.bed_c, p.dry_c, p.dry_hours);
    }
}

static void execute(char *command) {
    char *args = strchr(command, ' ');
    if (args)
        *args++ = 0;
    else
        args = command + strlen(command);

    if (!strcmp(command, "help")) fputs(HELP, stdout);
    else if (!strcmp(command, "status")) cmd_status();
    else if (!strcmp(command, "spool")) print_spool();
    else if (!strcmp(command, "presets")) cmd_presets();
    else if (!strcmp(command, "preset")) {
        char *end = nullptr;
        long index = strtol(args, &end, 10);
        if (end == args || index < 0 || index >= bambu::preset_count()) {
            printf("ERROR usage: preset <0..%d>\n", bambu::preset_count() - 1);
            return;
        }
        bambu::Spool s;
        spool::get(s);
        bambu::apply_preset(s, (uint8_t)index);
        spool::set(s);
        print_spool();
    }
    else if (!strcmp(command, "set")) cmd_set(args);
    else if (!strcmp(command, "date")) {
        if (!*args) {
            printf("ERROR usage: date <YYYY_MM_DD_HH_MM>\n");
            return;
        }
        spool::set_date(args);
        print_spool();
    }
    else if (!strcmp(command, "scan")) {
        tagsvc::Snapshot snap;
        tagsvc::snapshot(snap);
        if (snap.present)
            print_card(snap.card);
        else
            printf("ERROR no tag\n");
    }
    else if (!strcmp(command, "spitest")) {
        tagsvc::Snapshot snap;
        run_job(tagsvc::OP_SELFTEST, 0, snap);
    }
    else if (!strcmp(command, "read")) cmd_read();
#if defined(TW_READER_UNIVERSAL)
    else if (!strcmp(command, "decode")) cmd_decode();
    else if (!strcmp(command, "encode")) cmd_encode(args);
#endif
    else if (!strcmp(command, "magic")) cmd_magic();
    else if (!strcmp(command, "capture")) cmd_capture();
    else if (!strcmp(command, "loadimage")) cmd_loadimage();
    else if (!strcmp(command, "image")) cmd_image();
    else if (!strcmp(command, "clone")) cmd_clone();
    else if (!strcmp(command, "libadd")) cmd_libadd(args);
    else if (!strcmp(command, "liblist")) {
        char mats[16][taglib::NAME_LEN];
        int nm = taglib::list_materials(mats, 16);
        printf("library: %d entries, %d slots\n", taglib::count(), taglib::slot_count());
        for (int i = 0; i < nm; i++) {
            char cols[40][taglib::NAME_LEN];
            uint16_t counts[40];
            int nc = taglib::list_colors(mats[i], cols, counts, 40);
            for (int j = 0; j < nc; j++)
                printf("  %-20s %-20s x%u\n", mats[i], cols[j], counts[j]);
        }
    }
    else if (!strcmp(command, "libcount")) printf("OK %d entries\n", taglib::count());
    else if (!strcmp(command, "libclear")) {
        if (strcmp(args, "yes"))
            printf("ERROR confirm with: libclear yes\n");
        else {
            taglib::clear();
            printf("OK library cleared\n");
        }
    }
    else if (!strcmp(command, "dump")) cmd_dump();
    else if (!strcmp(command, "write")) cmd_write_or_erase(tagsvc::OP_WRITE);
    else if (!strcmp(command, "erase")) {
        if (strcmp(args, "yes"))
            printf("ERROR confirm with: erase yes\n");
        else
            cmd_write_or_erase(tagsvc::OP_ERASE);
    }
    else if (!strcmp(command, "key")) cmd_key(args);
    else if (*command) printf("ERROR unknown command (try help)\n");
}

void run(char *line) {
    execute(line);
    fflush(stdout);
}

}  // namespace console
