// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

/**
 * @file ui.cpp
 * The tag writer's pages: compose a spool (material, colour, details), read
 * any tag, write a Bambu-layout tag, reset, copy and clone. The display opens
 * it as one of its screens (screen_tag_writer.c): every page is built into
 * the root object the display hands over, one page at a time, and the
 * display's back-to-home navigation takes it away again.
 */
#include "ui.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <new>
#include <string.h>

#include "bambu_view.h"
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "filament_view.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "spool_store.h"
#include "tag_service.h"
#include "taglib.h"

namespace ui {

// ------------------------------------------------------------- look

static const uint32_t C_BG = 0x0B0E12;
static const uint32_t C_PANEL = 0x171C22;
static const uint32_t C_PANEL2 = 0x222932;
static const uint32_t C_BUTTON = 0x2C343E;
static const uint32_t C_BORDER = 0x3A434E;
static const uint32_t C_TEXT = 0xFFFFFF;
static const uint32_t C_MUTED = 0x9AA4AE;
static const uint32_t C_ACCENT = 0x00AE42;
static const uint32_t C_WARN = 0xF5A524;
static const uint32_t C_ERROR = 0xE5484D;

#define BULLET "  \xE2\x80\xA2  "
#define DEG "\xC2\xB0"

// Bambu-like basic palette, 0xRRGGBB.
static const uint32_t PALETTE[24] = {
    0xFFFFFF, 0xD1D3D5, 0x8E9089, 0x545454, 0x000000, 0xF7E6DE, 0xE4BD68, 0x9D432C,
    0xC12E1F, 0xFF6A13, 0xF4EE2A, 0xBECF00, 0x00AE42, 0x164B35, 0x56B7E6, 0x0086D6,
    0x0A2989, 0x5E43B7, 0xF55A74, 0xEC008C, 0x9D2235, 0x847D48, 0xA6A9AA, 0x68724D,
};

// Onboard RGB LED, active low.
static const gpio_num_t LED_R = GPIO_NUM_22, LED_G = GPIO_NUM_16, LED_B = GPIO_NUM_17;

// ------------------------------------------------------------- state

enum ScreenId { NONE, HOME, MATERIAL, COLOR, DETAILS, JOB, TAG_VIEW, SETTINGS,
                LIBRARY, LIBRARY_COLORS, LIBRARY_CONFIRM };
enum JobUi { J_WAIT, J_RUN, J_DONE, J_REMOVE };

static lv_obj_t *root;            // the display's screen object the pages live in
static lv_timer_t *service_timer;
static void (*exit_cb)(void);
static bool exit_wanted;
static ScreenId current = NONE;
static ScreenId pending = NONE;
static bambu::Spool edit;  // the copy an editor screen works on
static uint32_t shown_seq, shown_spool_version;
// The home card is a write/compose editor by default, but displays the tag on
// the reader when a different, decodable one is placed. These track that mode.
static char home_card_uid[21];  // tag UID now shown on the card ("" = write preset)
static char home_try_uid[21];   // last UID we auto-decoded, so we decode once per tag
static bool home_decoding;       // a silent home-card decode job is in flight
static int lib_pending_color = -1;  // colour chosen, awaiting the write confirm
static int64_t led_off_at;

static tagsvc::Op job_op;
static JobUi job_ui;
static tagops::Card job_card;
static tagops::Result job_result;
static bambu::TagInfo job_info;
static bool job_info_ok;
// Universal decode (OP_DECODE): the decoded tag of any format.
static DecodeResult job_decode;
static char job_tagtype[24];
static bool job_decode_ok;

// Widgets of the current screen (rebuilt with it).
static lv_obj_t *w_swatch, *w_hex, *w_title, *w_line[3];
static lv_obj_t *w_status_dot, *w_status_label, *w_tag_dot, *w_tag_label;
static lv_obj_t *w_slider[3], *w_slider_value[3], *w_preview, *w_preview_hex;
static lv_obj_t *w_field_value[8];
static lv_obj_t *w_spinner, *w_message, *w_sub, *w_bar, *w_progress, *w_icon, *w_icon_label;
static lv_obj_t *w_result_title, *w_result_detail, *w_left, *w_right, *w_cancel;

static tagsvc::Op pending_job = tagsvc::OP_NONE;

// Screen changes and job starts wait for the LVGL loop (service()), so an
// event callback never deletes the screen its own button lives on.
static void navigate(ScreenId id) { pending = id; }
static void request_job(tagsvc::Op op) { pending_job = op; }

// ------------------------------------------------------------- helpers

static lv_color_t hex(uint32_t c) { return lv_color_hex(c); }

static bool is_light(uint32_t rgba) {
    uint32_t r = rgba >> 24, g = (rgba >> 16) & 0xFF, b = (rgba >> 8) & 0xFF;
    return (r * 299 + g * 587 + b * 114) / 1000 > 140;
}

static void led(bool r, bool g, bool b, uint32_t ms) {
    gpio_set_level(LED_R, !r);
    gpio_set_level(LED_G, !g);
    gpio_set_level(LED_B, !b);
    led_off_at = ms ? esp_timer_get_time() + (int64_t)ms * 1000 : 0;
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_scrollable(o, false);
    return o;
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *s) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, hex(color), 0);
    lv_label_set_text(l, s);
    return l;
}

static lv_obj_t *button(lv_obj_t *parent, int x, int y, int w, int h, const char *label,
                        uint32_t color, lv_event_cb_t cb, void *user = nullptr) {
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, hex(color), 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = text(b, &lv_font_montserrat_16, C_TEXT, label);
    lv_obj_center(l);
    if (cb)
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

static void set_swatch(lv_obj_t *sw, lv_obj_t *hex_label, uint32_t rgba) {
    lv_obj_set_style_bg_color(sw, hex(rgba >> 8), 0);
    if (hex_label) {
        lv_label_set_text_fmt(hex_label, "#%06lX", (unsigned long)(rgba >> 8));
        lv_obj_set_style_text_color(hex_label, hex(is_light(rgba) ? 0x000000 : 0xFFFFFF), 0);
    }
}

static lv_obj_t *swatch(lv_obj_t *parent, int x, int y, int size, uint32_t rgba,
                        lv_obj_t **hex_label) {
    lv_obj_t *sw = box(parent, x, y, size, size, rgba >> 8, 12);
    lv_obj_set_style_border_width(sw, 2, 0);
    lv_obj_set_style_border_color(sw, hex(C_BORDER), 0);
    if (hex_label) {
        *hex_label = text(sw, &lv_font_montserrat_14, 0xFFFFFF, "");
        lv_obj_align(*hex_label, LV_ALIGN_BOTTOM_MID, 0, -8);
    }
    set_swatch(sw, hex_label ? *hex_label : nullptr, rgba);
    return sw;
}

// A fresh page in the root: the previous page goes first, so only one page
// holds LVGL pool at a time (the display's own screens work the same way).
// Only called from service() or the timer, never from a widget's own event.
static lv_obj_t *new_screen() {
    lv_obj_clean(root);
    lv_obj_t *s = lv_obj_create(root);
    lv_obj_remove_style_all(s);
    lv_obj_set_pos(s, 0, 0);
    lv_obj_set_size(s, 480, 320);
    lv_obj_set_style_bg_color(s, hex(C_BG), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(s, false);
    return s;
}

static lv_obj_t *header(lv_obj_t *scr, const char *title) {
    lv_obj_t *bar = box(scr, 0, 0, 480, 48, C_PANEL, 0);
    lv_obj_t *l = text(bar, &lv_font_montserrat_20, C_TEXT, title);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 16, 0);
    return bar;
}

static void show(lv_obj_t *scr, ScreenId id) {
    (void)scr;  // already in the root
    current = id;
}

// Spool summary next to a swatch: name, then three detail lines.
static void spool_text(lv_obj_t *parent, int x, const char *detail, const char *type,
                       uint16_t diameter_x100, uint16_t weight_g, uint16_t length_m,
                       uint16_t nozzle_min, uint16_t nozzle_max, uint16_t bed, uint16_t dry_c,
                       uint16_t dry_h, const char *code) {
    if (!w_title) {
        w_title = text(parent, &lv_font_montserrat_28, C_TEXT, "");
        lv_obj_set_pos(w_title, x, 12);
        lv_obj_set_width(w_title, 456 - x - 12);
        lv_label_set_long_mode(w_title, LV_LABEL_LONG_MODE_DOTS);
        for (int i = 0; i < 3; i++) {
            w_line[i] = text(parent, &lv_font_montserrat_16, C_MUTED, "");
            lv_obj_set_pos(w_line[i], x, 56 + i * 28);
        }
    }
    lv_label_set_text(w_title, detail[0] ? detail : "(no name)");
    lv_label_set_text_fmt(w_line[0], "%s" BULLET "%u.%02u mm" BULLET "%u g", type,
                          diameter_x100 / 100, diameter_x100 % 100, weight_g);
    lv_label_set_text_fmt(w_line[1], "Nozzle %u-%u " DEG "C" BULLET "Bed %u " DEG "C", nozzle_min,
                          nozzle_max, bed);
    lv_label_set_text_fmt(w_line[2], "Dry %u " DEG "C, %u h" BULLET "~%u m" BULLET "%s", dry_c,
                          dry_h, length_m, code);
}

// ------------------------------------------------------------- home

static void start_job(tagsvc::Op op);

static void on_home_button(lv_event_t *e) {
    switch ((intptr_t)lv_event_get_user_data(e)) {
    case 0: navigate(MATERIAL); break;
    case 1: navigate(COLOR); break;
    case 2: navigate(DETAILS); break;
#if defined(TW_READER_UNIVERSAL)
    case 3: request_job(tagsvc::OP_DECODE); break;  // any tag, any format
#else
    case 3: request_job(tagsvc::OP_READ); break;    // MIFARE Classic, Bambu layout
#endif
    case 4: request_job(tagsvc::OP_WRITE); break;
    case 5: navigate(LIBRARY); break;
    case 6: navigate(SETTINGS); break;
    case 7: exit_wanted = true; break;  // back to the display
    }
}

static void render_card(const bambu::Spool &s) {
    set_swatch(w_swatch, w_hex, s.rgba);
    spool_text(nullptr, 0, s.detail, s.type, s.diameter_x100, s.weight_g,
               bambu::estimate_length_m(s), s.nozzle_min_c, s.nozzle_max_c, s.bed_c, s.dry_c,
               s.dry_hours, s.code);
}

static void refresh_home_spool() {
    bambu::Spool s;
    spool::get(s);
    shown_spool_version = spool::version();
    render_card(s);
}

static uint32_t kind_color(tagops::Kind kind) {
    switch (kind) {
    case tagops::BLANK:
    case tagops::CUSTOM: return C_ACCENT;
    case tagops::GENUINE:
    case tagops::LOCKED: return C_WARN;
    case tagops::NONE: return C_MUTED;
    default: return C_ERROR;
    }
}

static const char *kind_hint(tagops::Kind kind) {
    switch (kind) {
    case tagops::BLANK: return "ready to write";
    case tagops::CUSTOM: return "can be rewritten";
    case tagops::GENUINE: return "read-only";
    case tagops::LOCKED: return "read-only";
    case tagops::UNSUPPORTED: return "use MIFARE Classic 1K";
    default: return "cannot use";
    }
}

static void refresh_home_tag(const tagsvc::Snapshot &snap) {
    shown_seq = snap.seq;
    uint32_t status_color = C_ACCENT;
    const char *status = "Ready";
    if (!snap.reader_ok) {
        status_color = C_ERROR;
        status = "No reader";
    } else if (!snap.key_set) {
        status_color = C_WARN;
        status = "No key";
    }
    lv_obj_set_style_bg_color(w_status_dot, hex(status_color), 0);
    lv_label_set_text(w_status_label, status);

    tagops::Kind kind = snap.present ? snap.card.kind : tagops::NONE;
    lv_obj_set_style_bg_color(w_tag_dot, hex(kind_color(kind)), 0);
    if (!snap.reader_ok) {
        lv_label_set_text(w_tag_label, TW_READER_NAME " not responding - check the wiring");
    } else if (!snap.present) {
        lv_label_set_text(w_tag_label, "Place a tag on the reader");
    } else {
        char uid[21];
        tagops::format_uid(snap.card.uid, uid);
        lv_label_set_text_fmt(w_tag_label, "%s" BULLET "%s" BULLET "%s", uid,
                              tagops::kind_name(kind), kind_hint(kind));
    }
    lv_obj_set_style_text_color(w_tag_label, hex(snap.present ? C_TEXT : C_MUTED), 0);
}

static void build_home() {
    lv_obj_t *scr = new_screen();
    w_title = nullptr;
    // Start in write-preset mode; tick() re-decodes any tag already present.
    home_card_uid[0] = 0;
    home_try_uid[0] = 0;
    home_decoding = false;
    lv_obj_t *bar = header(scr, "Spool Tag Writer");
    lv_obj_align(lv_obj_get_child(bar, 0), LV_ALIGN_LEFT_MID, 58, 0);
    button(bar, 6, 6, 44, 36, LV_SYMBOL_LEFT, C_BUTTON, on_home_button, (void *)7);
    w_status_dot = box(bar, 316, 19, 10, 10, C_MUTED, LV_RADIUS_CIRCLE);
    w_status_label = text(bar, &lv_font_montserrat_16, C_MUTED, "");
    lv_obj_set_pos(w_status_label, 332, 14);
    button(bar, 424, 6, 48, 36, LV_SYMBOL_SETTINGS, C_BUTTON, on_home_button, (void *)6);

    lv_obj_t *card = box(scr, 12, 56, 456, 150, C_PANEL, 12);
    w_swatch = swatch(card, 12, 12, 126, 0, &w_hex);
    bambu::Spool s;
    spool::get(s);
    spool_text(card, 154, s.detail, s.type, s.diameter_x100, s.weight_g,
               bambu::estimate_length_m(s), s.nozzle_min_c, s.nozzle_max_c, s.bed_c, s.dry_c,
               s.dry_hours, s.code);

    lv_obj_t *tag_bar = box(scr, 12, 214, 456, 40, C_PANEL2, 10);
    w_tag_dot = box(tag_bar, 14, 15, 10, 10, C_MUTED, LV_RADIUS_CIRCLE);
    w_tag_label = text(tag_bar, &lv_font_montserrat_16, C_MUTED, "");
    lv_obj_set_pos(w_tag_label, 34, 10);
    lv_obj_set_width(w_tag_label, 410);
    lv_label_set_long_mode(w_tag_label, LV_LABEL_LONG_MODE_DOTS);

    // Six actions: edit the spool (Material/Color/Details), then the tag
    // actions Read / Write (custom tag, for OpenAMS) / Clone (for stock AMS).
    static const char *labels[6] = {"Material", "Color", "Details", "Read", "Write", "Clone"};
    for (int i = 0; i < 6; i++) {
        uint32_t color = i == 4 ? C_ACCENT : (i == 5 ? 0x2C5AA0 : C_BUTTON);
        lv_obj_t *b = button(scr, 10 + i * 77, 262, 73, 50, labels[i], color, on_home_button,
                             (void *)(intptr_t)i);
        lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &lv_font_montserrat_14, 0);
    }

    show(scr, HOME);
    refresh_home_spool();
    tagsvc::Snapshot snap;
    tagsvc::snapshot(snap);
    refresh_home_tag(snap);
}

// ------------------------------------------------------------- material

static void on_material(lv_event_t *e) {
    intptr_t index = (intptr_t)lv_event_get_user_data(e);
    if (index >= 0) {
        bambu::Spool s;
        spool::get(s);
        bambu::apply_preset(s, (uint8_t)index);
        spool::set(s);
    }
    navigate(HOME);
}

static void build_material() {
    lv_obj_t *scr = new_screen();
    lv_obj_t *bar = header(scr, "Material");
    button(bar, 384, 6, 88, 36, "Back", C_BUTTON, on_material, (void *)-1);
    bambu::Spool s;
    spool::get(s);
    uint8_t selected = bambu::preset_match(s);
    for (uint8_t i = 0; i < bambu::preset_count(); i++) {
        bambu::Preset p;
        bambu::preset_get(i, p);
        int x = 12 + (i % 4) * 116, y = 56 + (i / 4) * 64;
        lv_obj_t *b = button(scr, x, y, 108, 56, "", i == selected ? C_PANEL2 : C_BUTTON,
                             on_material, (void *)(intptr_t)i);
        if (i == selected) {
            lv_obj_set_style_border_width(b, 2, 0);
            lv_obj_set_style_border_color(b, hex(C_ACCENT), 0);
        }
        lv_obj_t *placeholder = lv_obj_get_child(b, 0);
        lv_obj_delete(placeholder);
        const char *variant = p.detail;
        size_t type_length = strlen(p.type);
        if (!strncmp(p.detail, p.type, type_length))
            variant += type_length;
        while (*variant == ' ')
            variant++;
        lv_obj_set_style_pad_all(b, 4, 0);
        lv_obj_t *t = text(b, &lv_font_montserrat_20, C_TEXT, p.type);
        if (*variant) {
            lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 1);
            lv_obj_t *v = text(b, &lv_font_montserrat_14, C_MUTED, variant);
            lv_obj_align(v, LV_ALIGN_BOTTOM_MID, 0, 0);
        } else {
            lv_obj_center(t);
        }
    }
    show(scr, MATERIAL);
}

// ------------------------------------------------------------- colour

static const uint32_t CHANNEL_COLORS[3] = {0xE5484D, 0x30B050, 0x3B82F6};

static uint8_t channel(int c) { return (uint8_t)(edit.rgba >> (24 - 8 * c)); }

static void refresh_color(bool sliders) {
    set_swatch(w_preview, nullptr, edit.rgba);
    lv_label_set_text_fmt(w_preview_hex, "#%06lX", (unsigned long)(edit.rgba >> 8));
    for (int c = 0; c < 3; c++) {
        if (sliders)
            lv_slider_set_value(w_slider[c], channel(c), LV_ANIM_OFF);
        lv_label_set_text_fmt(w_slider_value[c], "%u", channel(c));
    }
}

static void on_palette(lv_event_t *e) {
    edit.rgba = (PALETTE[(intptr_t)lv_event_get_user_data(e)] << 8) | 0xFF;
    refresh_color(true);
}

static void on_slider(lv_event_t *e) {
    int c = (int)(intptr_t)lv_event_get_user_data(e);
    uint32_t value = (uint32_t)lv_slider_get_value(lv_event_get_target_obj(e));
    int shift = 24 - 8 * c;
    edit.rgba = (edit.rgba & ~(0xFFu << shift)) | (value << shift);
    refresh_color(false);
}

static void on_color_done(lv_event_t *e) {
    if ((intptr_t)lv_event_get_user_data(e)) {
        bambu::Spool s;
        spool::get(s);
        s.rgba = edit.rgba;
        spool::set(s);
    }
    navigate(HOME);
}

static void build_color() {
    spool::get(edit);
    lv_obj_t *scr = new_screen();
    lv_obj_t *bar = header(scr, "Color");
    w_preview = box(bar, 96, 8, 56, 32, 0, 8);
    lv_obj_set_style_border_width(w_preview, 2, 0);
    lv_obj_set_style_border_color(w_preview, hex(C_BORDER), 0);
    w_preview_hex = text(bar, &lv_font_montserrat_16, C_MUTED, "");
    lv_obj_set_pos(w_preview_hex, 164, 14);
    button(bar, 284, 6, 88, 36, "Cancel", C_BUTTON, on_color_done, (void *)0);
    button(bar, 380, 6, 92, 36, "Done", C_ACCENT, on_color_done, (void *)1);

    for (int i = 0; i < 24; i++) {
        lv_obj_t *b = lv_button_create(scr);
        lv_obj_set_pos(b, 12 + (i % 8) * 58, 58 + (i / 8) * 48);
        lv_obj_set_size(b, 50, 40);
        lv_obj_set_style_bg_color(b, hex(PALETTE[i]), 0);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, hex(C_BORDER), 0);
        lv_obj_add_event_cb(b, on_palette, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    static const char *names[3] = {"R", "G", "B"};
    for (int c = 0; c < 3; c++) {
        int y = 216 + c * 34;
        lv_obj_t *name = text(scr, &lv_font_montserrat_16, C_MUTED, names[c]);
        lv_obj_set_pos(name, 16, y + 4);
        w_slider[c] = lv_slider_create(scr);
        lv_obj_set_pos(w_slider[c], 48, y + 6);
        lv_obj_set_size(w_slider[c], 352, 14);
        lv_slider_set_range(w_slider[c], 0, 255);
        lv_obj_set_style_bg_color(w_slider[c], hex(C_PANEL2), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(w_slider[c], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(w_slider[c], hex(CHANNEL_COLORS[c]), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(w_slider[c], hex(CHANNEL_COLORS[c]), LV_PART_KNOB);
        lv_obj_set_ext_click_area(w_slider[c], 10);
        lv_obj_add_event_cb(w_slider[c], on_slider, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)c);
        w_slider_value[c] = text(scr, &lv_font_montserrat_16, C_TEXT, "");
        lv_obj_set_pos(w_slider_value[c], 420, y + 4);
    }
    show(scr, COLOR);
    refresh_color(true);
}

// ------------------------------------------------------------- details

enum Field { F_WEIGHT, F_DIAMETER, F_NOZZLE_MIN, F_NOZZLE_MAX, F_BED, F_DRY_TEMP, F_DRY_TIME,
             F_WIDTH, F_COUNT };
static const char *FIELD_NAMES[F_COUNT] = {
    "Weight", "Diameter", "Nozzle min", "Nozzle max", "Bed", "Dry temp", "Dry time",
    "Spool width",
};

static void refresh_field(int f) {
    lv_obj_t *l = w_field_value[f];
    switch (f) {
    case F_WEIGHT: lv_label_set_text_fmt(l, "%u g", edit.weight_g); break;
    case F_DIAMETER:
        lv_label_set_text_fmt(l, "%u.%02u mm", edit.diameter_x100 / 100, edit.diameter_x100 % 100);
        break;
    case F_NOZZLE_MIN: lv_label_set_text_fmt(l, "%u " DEG "C", edit.nozzle_min_c); break;
    case F_NOZZLE_MAX: lv_label_set_text_fmt(l, "%u " DEG "C", edit.nozzle_max_c); break;
    case F_BED: lv_label_set_text_fmt(l, "%u " DEG "C", edit.bed_c); break;
    case F_DRY_TEMP: lv_label_set_text_fmt(l, "%u " DEG "C", edit.dry_c); break;
    case F_DRY_TIME: lv_label_set_text_fmt(l, "%u h", edit.dry_hours); break;
    case F_WIDTH:
        lv_label_set_text_fmt(l, "%u.%02u mm", edit.width_x100 / 100, edit.width_x100 % 100);
        break;
    }
}

static void step(uint16_t &value, int delta, int low, int high) {
    int next = (int)value + delta;
    value = (uint16_t)(next < low ? low : (next > high ? high : next));
}

static void on_step(lv_event_t *e) {
    intptr_t code = (intptr_t)lv_event_get_user_data(e);
    int f = (int)(code >> 1);
    int dir = (code & 1) ? 1 : -1;
    bool repeat = lv_event_get_code(e) == LV_EVENT_LONG_PRESSED_REPEAT;
    switch (f) {
    case F_WEIGHT:
        step(edit.weight_g, dir * (repeat ? 250 : 50), 50, 5000);
        edit.length_m = 0;
        break;
    case F_DIAMETER:
        if (repeat)
            return;
        edit.diameter_x100 = edit.diameter_x100 == 175 ? 285 : 175;
        edit.length_m = 0;
        break;
    case F_NOZZLE_MIN: step(edit.nozzle_min_c, dir * 5, 150, 400); break;
    case F_NOZZLE_MAX: step(edit.nozzle_max_c, dir * 5, 150, 450); break;
    case F_BED: step(edit.bed_c, dir * 5, 0, 130); break;
    case F_DRY_TEMP: step(edit.dry_c, dir * 5, 0, 95); break;
    case F_DRY_TIME: step(edit.dry_hours, dir, 0, 48); break;
    case F_WIDTH: step(edit.width_x100, dir * 25, 2000, 12000); break;
    }
    if (edit.nozzle_min_c > edit.nozzle_max_c) {
        if (f == F_NOZZLE_MIN)
            edit.nozzle_max_c = edit.nozzle_min_c;
        else
            edit.nozzle_min_c = edit.nozzle_max_c;
        refresh_field(F_NOZZLE_MIN);
        refresh_field(F_NOZZLE_MAX);
    }
    refresh_field(f);
}

static void on_details_done(lv_event_t *e) {
    if ((intptr_t)lv_event_get_user_data(e)) {
        bambu::Spool s;
        spool::get(s);
        edit.rgba = s.rgba;  // only the fields this screen edits
        memcpy(edit.type, s.type, sizeof(s.type));
        memcpy(edit.detail, s.detail, sizeof(s.detail));
        memcpy(edit.code, s.code, sizeof(s.code));
        spool::set(edit);
    }
    navigate(HOME);
}

static void build_details() {
    spool::get(edit);
    lv_obj_t *scr = new_screen();
    lv_obj_t *bar = header(scr, "Details");
    button(bar, 284, 6, 88, 36, "Cancel", C_BUTTON, on_details_done, (void *)0);
    button(bar, 380, 6, 92, 36, "Done", C_ACCENT, on_details_done, (void *)1);
    for (int f = 0; f < F_COUNT; f++) {
        int x = 12 + (f / 4) * 232, y = 56 + (f % 4) * 64;
        lv_obj_t *cell = box(scr, x, y, 224, 56, C_PANEL, 10);
        lv_obj_t *name = text(cell, &lv_font_montserrat_14, C_MUTED, FIELD_NAMES[f]);
        lv_obj_set_pos(name, 12, 6);
        w_field_value[f] = text(cell, &lv_font_montserrat_20, C_TEXT, "");
        lv_obj_set_pos(w_field_value[f], 12, 26);
        for (int dir = 0; dir < 2; dir++) {
            lv_obj_t *b = button(cell, dir ? 172 : 120, 6, 44, 44, dir ? LV_SYMBOL_PLUS
                                                                      : LV_SYMBOL_MINUS,
                                 C_BUTTON, nullptr);
            void *arg = (void *)(intptr_t)(f << 1 | dir);
            // SHORT_CLICKED, not CLICKED: a long press ends with the repeat
            // handler, and CLICKED would then add one more step on release.
            lv_obj_add_event_cb(b, on_step, LV_EVENT_SHORT_CLICKED, arg);
            lv_obj_add_event_cb(b, on_step, LV_EVENT_LONG_PRESSED_REPEAT, arg);
        }
        refresh_field(f);
    }
    show(scr, DETAILS);
}

// ------------------------------------------------------------- jobs

// Cloning is two tag passes: read the source, then write the magic target.
enum CloneStage { CL_OFF, CL_SOURCE, CL_TARGET };
static CloneStage clone_stage;
static bool clone_from_library;  // library pick vs. physical capture
static char clone_desc[48];

static const char *job_title(tagsvc::Op op) {
    switch (op) {
    case tagsvc::OP_WRITE: return "Write tag";
    case tagsvc::OP_READ: return "Read tag";
    case tagsvc::OP_DECODE: return "Read tag";
    case tagsvc::OP_ERASE: return "Reset tag";
    case tagsvc::OP_CAPTURE: return "Copy a tag";
    case tagsvc::OP_CLONE: return "Write clone";
    default: return "";
    }
}

static void on_job_button(lv_event_t *e) {
    switch ((intptr_t)lv_event_get_user_data(e)) {
    case 0:  // cancel while waiting
        tagsvc::cancel(tagsvc::OWNER_UI);
        navigate(HOME);
        break;
    case 1:  // done
        navigate(HOME);
        break;
    case 2:  // write another / retry / clone another
        if (job_op == tagsvc::OP_CLONE) {
            if (!job_result.ok) {
                request_job(tagsvc::OP_CLONE);  // the image is still loaded
            } else if (clone_from_library) {
                navigate(LIBRARY);  // pick the next spool
            } else {
                clone_stage = CL_SOURCE;
                request_job(tagsvc::OP_CAPTURE);
            }
        } else if (job_result.ok) {
            job_ui = J_REMOVE;
            lv_obj_set_hidden(w_left, true);
            lv_obj_set_hidden(w_right, true);
            lv_label_set_text(w_result_detail, "Remove the tag, then hold the next one");
        } else {
            request_job(job_op);
        }
        break;
    }
}

static void show_result() {
    lv_obj_set_hidden(w_spinner, true);
    lv_obj_set_hidden(w_message, true);
    lv_obj_set_hidden(w_sub, true);
    lv_obj_set_hidden(w_bar, true);
    lv_obj_set_hidden(w_progress, true);
    lv_obj_set_hidden(w_cancel, true);
    lv_obj_set_hidden(w_icon, false);
    lv_obj_set_hidden(w_result_title, false);
    lv_obj_set_hidden(w_result_detail, false);
    lv_obj_set_hidden(w_right, false);

    bool ok = job_result.ok;
    lv_obj_set_style_bg_color(w_icon, hex(ok ? C_ACCENT : C_ERROR), 0);
    lv_label_set_text(w_icon_label, ok ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
    led(!ok, ok, false, 3000);

    char uid[21] = "";
    if (job_card.kind != tagops::NONE)
        tagops::format_uid(job_card.uid, uid);
    const char *ok_title = "Tag written";
    const char *fail_title = "Write failed";
    if (job_op == tagsvc::OP_ERASE) {
        ok_title = "Tag reset";
        fail_title = "Reset failed";
    } else if (job_op == tagsvc::OP_CLONE) {
        ok_title = "Clone written";
        fail_title = "Clone failed";
    } else if (job_op == tagsvc::OP_CAPTURE) {
        fail_title = "Copy failed";  // success goes straight on to the clone stage
    }
    if (ok) {
        lv_label_set_text(w_result_title, ok_title);
        lv_label_set_text_fmt(w_result_detail, "%s%s", uid,
                              job_op == tagsvc::OP_ERASE ? BULLET "factory keys"
                                                         : BULLET "verified, ready for the AMS");
    } else {
        lv_label_set_text(w_result_title, fail_title);
        if (job_result.sector != 0xFF)
            lv_label_set_text_fmt(w_result_detail, "%s, sector %u (%s)", job_result.message,
                                  job_result.sector, Reader::status_name(job_result.status));
        else
            lv_label_set_text(w_result_detail, job_result.message ? job_result.message : "");
    }
    if (job_op == tagsvc::OP_WRITE || job_op == tagsvc::OP_CLONE) {
        lv_obj_set_hidden(w_left, false);
        const char *label = job_op == tagsvc::OP_CLONE ? (ok ? "Clone another" : "Retry")
                                                       : (ok ? "Write another" : "Retry");
        lv_label_set_text(lv_obj_get_child(w_left, 0), label);
    }
}

static void refresh_job(const tagsvc::Snapshot &snap) {
    if (job_ui == J_REMOVE) {
        if (!snap.present)
            request_job(tagsvc::OP_WRITE);
        return;
    }
    if (job_ui == J_DONE || snap.owner != tagsvc::OWNER_UI)
        return;
    if (snap.phase == tagsvc::RUNNING && job_ui == J_WAIT) {
        job_ui = J_RUN;
        led(false, false, false, 0);
        lv_obj_set_hidden(w_spinner, true);
        lv_obj_set_hidden(w_cancel, true);
        lv_obj_set_hidden(w_sub, true);
        lv_obj_set_hidden(w_bar, false);
        lv_obj_set_hidden(w_progress, false);
        lv_label_set_text(w_message, "Keep the tag still");
    }
    if (snap.phase == tagsvc::RUNNING && snap.total) {
        lv_bar_set_range(w_bar, 0, snap.total);
        lv_bar_set_value(w_bar, snap.done, LV_ANIM_OFF);
        uint8_t half = snap.total / 2;
        if (job_op == tagsvc::OP_CLONE && snap.done <= half)
            lv_label_set_text_fmt(w_progress, "Cloning block %u of %u", snap.done, half);
        else if (job_op == tagsvc::OP_CLONE)
            lv_label_set_text_fmt(w_progress, "Verifying %u of %u", snap.done - half, half);
        else if (job_op == tagsvc::OP_WRITE && snap.done <= half)
            lv_label_set_text_fmt(w_progress, "Writing sector %u of %u", snap.done, half);
        else if (job_op == tagsvc::OP_WRITE)
            lv_label_set_text_fmt(w_progress, "Verifying sector %u of %u", snap.done - half, half);
        else
            lv_label_set_text_fmt(w_progress, "Sector %u of %u", snap.done, snap.total);
    }
    if (snap.phase == tagsvc::DONE) {
        job_ui = J_DONE;
        job_card = snap.job_card;
        job_result = snap.result;
        job_info = snap.info;
        job_info_ok = snap.info_ok;
        tagsvc::acknowledge();
        if (job_op == tagsvc::OP_CAPTURE) {
            if (job_result.ok) {
                // Source read: describe it, then go straight to the write pass.
                snprintf(clone_desc, sizeof(clone_desc), "%s %s" BULLET "#%06lX",
                         snap.info.type, snap.info.detail, (unsigned long)(snap.info.rgba >> 8));
                clone_stage = CL_TARGET;
                request_job(tagsvc::OP_CLONE);
            } else {
                clone_stage = CL_OFF;
                show_result();
            }
            return;
        }
        if (job_op == tagsvc::OP_CLONE)
            clone_stage = CL_OFF;
#if defined(TW_READER_UNIVERSAL)
        if (job_op == tagsvc::OP_DECODE) {
            job_decode_ok = false;
            try {  // a copy of the STL result could throw on a very low heap
                tagsvc::get_decode(job_decode, job_tagtype, &job_decode_ok);
            } catch (...) {
                job_decode_ok = false;
            }
            if (job_decode_ok) {
                led(false, true, false, 1500);
                navigate(TAG_VIEW);
            } else {
                show_result();
            }
            return;
        }
#endif
        if (job_op == tagsvc::OP_READ && job_card.kind != tagops::NONE) {
            led(false, job_result.ok, false, 1500);
            bambu_view::to_result(job_info, job_info_ok, job_result.message, job_decode);
            snprintf(job_tagtype, sizeof(job_tagtype), "%s",
                     job_card.kind == tagops::UNSUPPORTED ? "iso14443a" : "mifare_classic_1k");
            job_decode_ok = true;
            navigate(TAG_VIEW);
        } else {
            show_result();
        }
    }
}

static void build_job() {
    lv_obj_t *scr = new_screen();
    header(scr, job_title(job_op));

    w_spinner = lv_spinner_create(scr);
    lv_spinner_set_anim_params(w_spinner, 1200, 240);
    lv_obj_set_size(w_spinner, 72, 72);
    lv_obj_align(w_spinner, LV_ALIGN_TOP_MID, 0, 64);
    lv_obj_set_style_arc_color(w_spinner, hex(C_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(w_spinner, hex(C_PANEL2), 0);

    w_message = text(scr, &lv_font_montserrat_20, C_TEXT, "Hold a tag on the reader");
    lv_obj_align(w_message, LV_ALIGN_TOP_MID, 0, 150);

    bambu::Spool s;
    spool::get(s);
    w_sub = text(scr, &lv_font_montserrat_16, C_MUTED, "");
    lv_obj_align(w_sub, LV_ALIGN_TOP_MID, 0, 184);
    if (job_op == tagsvc::OP_WRITE)
        lv_label_set_text_fmt(w_sub, "%s" BULLET "#%06lX" BULLET "%u g", s.detail,
                              (unsigned long)(s.rgba >> 8), s.weight_g);
    else if (job_op == tagsvc::OP_ERASE)
        lv_label_set_text(w_sub, "Blank or custom tags go back to factory keys");
    else if (job_op == tagsvc::OP_CAPTURE)
        lv_label_set_text(w_sub, "The genuine Bambu tag to copy");
    else if (job_op == tagsvc::OP_CLONE)
        lv_label_set_text(w_sub, clone_desc);
    else
        lv_label_set_text(w_sub, "Genuine Bambu or custom spool tags");

    if (job_op == tagsvc::OP_CAPTURE)
        lv_label_set_text(w_message, "Place the tag to copy");
    else if (job_op == tagsvc::OP_CLONE)
        lv_label_set_text(w_message, "Place a blank magic tag");

    w_bar = lv_bar_create(scr);
    lv_obj_set_size(w_bar, 360, 18);
    lv_obj_align(w_bar, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_set_style_bg_color(w_bar, hex(C_PANEL2), 0);
    lv_obj_set_style_bg_color(w_bar, hex(C_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_hidden(w_bar, true);
    w_progress = text(scr, &lv_font_montserrat_16, C_MUTED, "");
    lv_obj_align(w_progress, LV_ALIGN_TOP_MID, 0, 186);
    lv_obj_set_hidden(w_progress, true);

    w_icon = box(scr, 204, 62, 72, 72, C_ACCENT, LV_RADIUS_CIRCLE);
    w_icon_label = text(w_icon, &lv_font_montserrat_28, C_TEXT, LV_SYMBOL_OK);
    lv_obj_center(w_icon_label);
    lv_obj_set_hidden(w_icon, true);
    w_result_title = text(scr, &lv_font_montserrat_28, C_TEXT, "");
    lv_obj_align(w_result_title, LV_ALIGN_TOP_MID, 0, 146);
    lv_obj_set_hidden(w_result_title, true);
    w_result_detail = text(scr, &lv_font_montserrat_16, C_MUTED, "");
    lv_obj_align(w_result_detail, LV_ALIGN_TOP_MID, 0, 190);
    lv_obj_set_hidden(w_result_detail, true);

    w_cancel = button(scr, 160, 252, 160, 52, "Cancel", C_BUTTON, on_job_button, (void *)0);
    w_left = button(scr, 12, 252, 222, 52, "Write another", C_BUTTON, on_job_button, (void *)2);
    w_right = button(scr, 246, 252, 222, 52, "Done", C_ACCENT, on_job_button, (void *)1);
    lv_obj_set_hidden(w_left, true);
    lv_obj_set_hidden(w_right, true);
    show(scr, JOB);
}

static void start_job(tagsvc::Op op) {
    job_op = op;
    job_ui = J_WAIT;
    memset(&job_card, 0, sizeof(job_card));
    tagsvc::Snapshot snap;
    tagsvc::snapshot(snap);
    build_job();
    if (!tagsvc::ready()) {
        job_ui = J_DONE;
        job_result = {false, Reader::OK, 0xFF, "not enough memory to run the reader"};
        show_result();
        return;
    }
    if (!snap.reader_ok) {
        job_ui = J_DONE;
        job_result = {false, Reader::CHIP_TIMEOUT, 0xFF,
                      TW_READER_NAME " not responding - check the wiring"};
        show_result();
        return;
    }
    bambu::Spool s;
    spool::get(s);
    // A just-cancelled job can take one reader poll (~120 ms) to let go.
    // Normally the reader is idle and this takes on the first try. A short,
    // bounded retry covers a just-cancelled job still letting go; keep it small
    // so a busy reader never stalls the LVGL task for long (it reports below).
    bool queued = false;
    for (int attempt = 0; attempt < 2 && !queued; attempt++) {
        queued = tagsvc::submit(op, tagsvc::OWNER_UI, &s, spool::date(), 60000);
        if (!queued)
            vTaskDelay(pdMS_TO_TICKS(40));
    }
    if (!queued) {
        job_ui = J_DONE;
        job_result = {false, Reader::OK, 0xFF, "reader busy (serial console job running)"};
        show_result();
        return;
    }
    led(false, false, true, 0);
}

// ------------------------------------------------------------- tag view

// Map a decoded filament (any format) onto the Bambu spool editor model, so
// "Use these settings" lets the user re-write any tag as a Bambu tag.
// Clamp to the spool model's uint16 fields: a decoded tag can carry absurd or
// non-finite values, and an out-of-range double->int cast is undefined.
static uint16_t to_u16(double v) {
    if (!(v > 0.0)) return 0;  // also rejects NaN
    if (v > 65535.0) return 65535;
    return (uint16_t)(v + 0.5);
}

static void filament_to_spool(const Filament &f, bambu::Spool &s) {
    bambu::default_spool(s);
    if (!f.material.empty()) {
        strncpy(s.type, f.material.c_str(), sizeof(s.type) - 1);
        s.type[sizeof(s.type) - 1] = 0;
    }
    const std::string &name = !f.name.empty() ? f.name : f.material;
    if (!name.empty()) {
        strncpy(s.detail, name.c_str(), sizeof(s.detail) - 1);
        s.detail[sizeof(s.detail) - 1] = 0;
    }
    if (!f.sku.empty()) {
        strncpy(s.code, f.sku.c_str(), sizeof(s.code) - 1);
        s.code[sizeof(s.code) - 1] = 0;
    }
    std::string hex = f.color_hex;
    if (!hex.empty() && hex[0] == '#')
        hex.erase(0, 1);
    if (hex.size() == 6) {
        unsigned long rgb = strtoul(hex.c_str(), nullptr, 16);
        s.rgba = (uint32_t)((rgb << 8) | 0xFF);
    }
    if (f.weight_g)
        s.weight_g = to_u16(f.weight_g);
    if (f.diameter_mm != 0.0)
        s.diameter_x100 = to_u16(f.diameter_mm * 100.0);
    if (f.nozzle_min)
        s.nozzle_min_c = to_u16(f.nozzle_min);
    if (f.nozzle_max)
        s.nozzle_max_c = to_u16(f.nozzle_max);
    if (f.bed_max || f.bed_min)
        s.bed_c = to_u16(f.bed_max ? f.bed_max : f.bed_min);
    if (f.drying_temp)
        s.dry_c = to_u16(f.drying_temp);
    if (f.drying_time_min)
        s.dry_hours = to_u16(f.drying_time_min / 60);
    if (f.density != 0.0)
        s.density_x100 = to_u16(f.density * 100.0);
    if (f.length_m)
        s.length_m = to_u16(f.length_m);
}

static void on_tag_view(lv_event_t *e) {
    if ((intptr_t)lv_event_get_user_data(e)) {
        bambu::Spool s;
        filament_to_spool(job_decode.filament, s);
        spool::set(s);
    }
    navigate(HOME);
}

static void build_tag_view() {
    lv_obj_t *scr = new_screen();
    char uid[21], title[32];
    tagops::format_uid(job_card.uid, uid);
    snprintf(title, sizeof(title), "Tag %s", uid);
    lv_obj_t *bar = header(scr, title);

    fview::ViewModel vm;
    try {  // builds many strings; a low-heap throw must not crash the LVGL task
        vm = fview::build(job_decode, job_tagtype);
    } catch (...) {
        vm = fview::ViewModel();
        vm.title = "Unreadable tag";
    }
    // Header badge: the format when recognized, else the tag type.
    const std::string &badge = vm.recognized ? vm.format : vm.tag_type;
    lv_obj_t *b = text(bar, &lv_font_montserrat_16,
                       vm.recognized ? C_ACCENT : C_MUTED, badge.c_str());
    lv_obj_align(b, LV_ALIGN_RIGHT_MID, -16, 0);

    lv_obj_t *card = box(scr, 12, 56, 456, 112, C_PANEL, 12);
    int tx = 16;
    if (vm.has_color) {
        lv_obj_t *hex_label;
        swatch(card, 12, 12, 84, vm.color_rgba, &hex_label);
        tx = 112;
    }
    lv_obj_t *t = text(card, &lv_font_montserrat_20, C_TEXT, vm.title.c_str());
    lv_obj_set_pos(t, tx, 12);
    lv_obj_set_width(t, 456 - tx - 12);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    if (!vm.subtitle.empty()) {
        lv_obj_t *st = text(card, &lv_font_montserrat_16, C_MUTED, vm.subtitle.c_str());
        lv_obj_set_pos(st, tx, 44);
        lv_obj_set_width(st, 456 - tx - 12);
        lv_label_set_long_mode(st, LV_LABEL_LONG_MODE_DOTS);
    }
    if (!vm.status.empty()) {
        lv_obj_t *ss = text(card, &lv_font_montserrat_14,
                            vm.writable ? C_ACCENT : C_MUTED, vm.status.c_str());
        lv_obj_set_pos(ss, tx, 76);
    }

    // Detail rows (as many as fit above the buttons).
    int y = 174;
    size_t shown = 0;
    for (const fview::Row &r : vm.rows) {
        if (shown >= 5)
            break;
        lv_obj_t *lab = text(scr, &lv_font_montserrat_14, C_MUTED, r.label.c_str());
        lv_obj_set_pos(lab, 16, y);
        lv_obj_t *val = text(scr, &lv_font_montserrat_14, C_TEXT, r.value.c_str());
        lv_obj_set_pos(val, 210, y);
        lv_obj_set_width(val, 258);
        lv_label_set_long_mode(val, LV_LABEL_LONG_MODE_DOTS);
        y += 15;
        shown++;
    }

    // Any recognized filament can be loaded into the editor and written back.
    bool has_filament = vm.recognized && (!vm.title.empty());
    if (has_filament)
        button(scr, 12, 252, 222, 52, "Use these settings", C_BUTTON, on_tag_view, (void *)1);
    button(scr, 246, 252, 222, 52, "Done", C_ACCENT, on_tag_view, (void *)0);
    show(scr, TAG_VIEW);
}

// ------------------------------------------------------------- library

// The library pages' lists (about 2.4 KB), on the heap from the first visit
// to the library until the tag writer closes, not in static RAM for good.
struct Library {
    char materials[16][taglib::NAME_LEN];
    int material_n;
    char material[taglib::NAME_LEN];
    char colors[64][taglib::NAME_LEN];
    uint16_t counts[64];
    int color_n;
};
static Library *lib;

static void on_lib_material(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0) {
        navigate(SETTINGS);
        return;
    }
    strncpy(lib->material, lib->materials[idx], sizeof(lib->material) - 1);
    lib->material[sizeof(lib->material) - 1] = 0;
    navigate(LIBRARY_COLORS);
}

static void build_library() {
    lv_obj_t *scr = new_screen();
    lv_obj_t *bar = header(scr, "Clone from library");
    button(bar, 384, 6, 88, 36, "Back", C_BUTTON, on_lib_material, (void *)-1);
    if (!lib)
        lib = new (std::nothrow) Library();
    if (!lib) {
        lv_obj_t *l = text(scr, &lv_font_montserrat_20, C_MUTED, "Not enough memory");
        lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
        show(scr, LIBRARY);
        return;
    }
    lib->material_n = taglib::list_materials(lib->materials, 16);
    if (lib->material_n == 0) {
        lv_obj_t *l = text(scr, &lv_font_montserrat_20, C_MUTED,
                           "Library empty");
        lv_obj_align(l, LV_ALIGN_CENTER, 0, -20);
        lv_obj_t *h = text(scr, &lv_font_montserrat_14, C_MUTED,
                           "Upload genuine tags from a PC:\n"
                           "tools/tagtool.py libupload --lib <dir> <query>");
        lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(h, LV_ALIGN_CENTER, 0, 30);
        show(scr, LIBRARY);
        return;
    }
    for (int i = 0; i < lib->material_n; i++) {
        int x = 12 + (i % 4) * 116, y = 56 + (i / 4) * 64;
        lv_obj_t *b = button(scr, x, y, 108, 56, "", C_BUTTON, on_lib_material,
                             (void *)(intptr_t)i);
        lv_obj_set_style_pad_all(b, 4, 0);
        lv_label_set_text(lv_obj_get_child(b, 0), lib->materials[i]);
        lv_obj_set_width(lv_obj_get_child(b, 0), 100);
        lv_obj_set_style_text_align(lv_obj_get_child(b, 0), LV_TEXT_ALIGN_CENTER, 0);
    }
    show(scr, LIBRARY);
}

static void start_library_clone(int color_idx);

// Picking a colour no longer writes straight away: a stray tap used to arm a
// clone that then fired the instant any tag touched the reader. Confirm first.
static void on_lib_color(lv_event_t *e) {
    lib_pending_color = (int)(intptr_t)lv_event_get_user_data(e);
    navigate(LIBRARY_CONFIRM);
}

static void on_lib_colors_back(lv_event_t *e) {
    (void)e;
    navigate(LIBRARY);
}

static void build_library_colors() {
    lv_obj_t *scr = new_screen();
    if (!lib) {  // not through the library page: nothing to list
        navigate(LIBRARY);
        return;
    }
    lv_obj_t *bar = header(scr, lib->material);
    button(bar, 384, 6, 88, 36, "Back", C_BUTTON, on_lib_colors_back, nullptr);
    lib->color_n = taglib::list_colors(lib->material, lib->colors, lib->counts, 64);

    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_set_pos(list, 0, 48);
    lv_obj_set_size(list, 480, 272);
    lv_obj_set_style_bg_color(list, hex(C_BG), 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 10, 0);
    lv_obj_set_style_pad_row(list, 8, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    for (int i = 0; i < lib->color_n; i++) {
        lv_obj_t *b = lv_button_create(list);
        lv_obj_set_width(b, lv_pct(100));
        lv_obj_set_height(b, 46);
        lv_obj_set_style_bg_color(b, hex(C_BUTTON), 0);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_add_event_cb(b, on_lib_color, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *name = text(b, &lv_font_montserrat_16, C_TEXT, lib->colors[i]);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 8, 0);
        if (lib->counts[i] > 1) {
            lv_obj_t *n = text(b, &lv_font_montserrat_14, C_MUTED, "");
            lv_label_set_text_fmt(n, "%u tags", lib->counts[i]);
            lv_obj_align(n, LV_ALIGN_RIGHT_MID, -12, 0);
        }
    }
    show(scr, LIBRARY_COLORS);
}

static void on_lib_confirm(lv_event_t *e) {
    switch ((intptr_t)lv_event_get_user_data(e)) {
    case 0: navigate(LIBRARY_COLORS); break;  // back, no write
    case 1:                                   // write now
        if (lib_pending_color >= 0)
            start_library_clone(lib_pending_color);
        break;
    }
}

static void build_library_confirm() {
    lv_obj_t *scr = new_screen();
    if (!lib || lib_pending_color < 0 || lib_pending_color >= lib->color_n) {
        navigate(LIBRARY);
        return;
    }
    lv_obj_t *bar = header(scr, "Write clone");
    button(bar, 384, 6, 88, 36, "Back", C_BUTTON, on_lib_confirm, (void *)0);

    lv_obj_t *panel = box(scr, 12, 64, 456, 96, C_PANEL, 12);
    lv_obj_t *what = text(panel, &lv_font_montserrat_20, C_TEXT, "");
    lv_obj_set_pos(what, 16, 14);
    lv_label_set_text_fmt(what, "%s" BULLET "%s", lib->material, lib->colors[lib_pending_color]);
    lv_obj_t *note = text(panel, &lv_font_montserrat_16, C_MUTED,
                          "Place a blank magic tag, then tap Write.\n"
                          "The tag is locked after writing (write-once).");
    lv_obj_set_pos(note, 16, 46);

    button(scr, 12, 176, 456, 56, LV_SYMBOL_COPY "  Write to tag", C_ACCENT, on_lib_confirm,
           (void *)1);
    show(scr, LIBRARY_CONFIRM);
}

static void start_library_clone(int color_idx) {
    if (!lib || color_idx < 0 || color_idx >= lib->color_n)
        return;
    uint8_t *image = static_cast<uint8_t *>(malloc(1024));  // load_image() copies it
    if (!image) {
        navigate(HOME);
        return;
    }
    char uid[9];
    // Pick the least-recently... just the first entry; one UID per spool is the
    // user's call. (color_entry_count tells them how many share this colour.)
    bool loaded = taglib::load(lib->material, lib->colors[color_idx], 0, image, uid);
    const char *why;
    loaded = loaded && tagsvc::load_image(image, &why);
    free(image);
    if (!loaded) {
        navigate(HOME);
        return;
    }
    snprintf(clone_desc, sizeof(clone_desc), "%s %s" BULLET "%s", lib->material,
             lib->colors[color_idx], uid);
    clone_from_library = true;
    clone_stage = CL_TARGET;
    request_job(tagsvc::OP_CLONE);
}

// ------------------------------------------------------------- settings

static void on_settings(lv_event_t *e) {
    switch ((intptr_t)lv_event_get_user_data(e)) {
    case 0: navigate(HOME); break;
    case 2: request_job(tagsvc::OP_ERASE); break;
    case 3:
        clone_from_library = false;
        clone_stage = CL_SOURCE;
        request_job(tagsvc::OP_CAPTURE);
        break;
    case 4: navigate(LIBRARY); break;
    }
}

static void build_settings() {
    lv_obj_t *scr = new_screen();
    lv_obj_t *bar = header(scr, "Settings");
    button(bar, 384, 6, 88, 36, "Back", C_BUTTON, on_settings, (void *)0);
    tagsvc::Snapshot snap;
    tagsvc::snapshot(snap);
    lv_obj_t *panel = box(scr, 12, 56, 456, 88, C_PANEL, 12);
    lv_obj_t *info = text(panel, &lv_font_montserrat_16, C_MUTED, "");
    lv_obj_set_pos(info, 14, 10);
    lv_label_set_text_fmt(info,
                          "Reader: " TW_READER_NAME ", version 0x%02X%s\n"
                          "Bambu master key: %s\n"
                          "Production date written: %s\n"
                          "Firmware: %s",
                          snap.version, snap.reader_ok ? "" : " (not responding)",
                          snap.key_set ? snap.key_fingerprint : "not set", spool::date(),
                          esp_app_get_description()->version);
    // Cloning (for stock AMS): from the on-device library, or by copying a
    // physical genuine tag. For OpenAMS bays, Write on the home screen is enough.
    char lib_label[40];
    snprintf(lib_label, sizeof(lib_label), LV_SYMBOL_COPY "  Clone from library (%d)",
             taglib::count());
    button(scr, 12, 150, 456, 46, lib_label, C_ACCENT, on_settings, (void *)4);
    button(scr, 12, 204, 456, 42, "Copy a physical tag", C_BUTTON, on_settings, (void *)3);
    button(scr, 12, 252, 456, 42, "Reset a tag to blank", C_BUTTON, on_settings, (void *)2);
    show(scr, SETTINGS);
}

// ------------------------------------------------------------- plumbing

// Drives the home card between write-preset mode and read-display mode. A new,
// decodable tag on the reader is decoded once (per UID) and shown on the card;
// removing it, or a blank/unsupported tag, reverts to the editable preset.
static void update_home_card(const tagsvc::Snapshot &snap) {
#if !defined(TW_READER_UNIVERSAL)
    // The MFRC522 image has no universal decode: the card stays the write preset.
    (void)snap;
#else
    // Finish a silent decode we started on a previous tick.
    if (home_decoding) {
        if (snap.owner == tagsvc::OWNER_UI && snap.op == tagsvc::OP_DECODE &&
            snap.phase == tagsvc::DONE) {
            home_decoding = false;
            tagsvc::acknowledge();
            DecodeResult dr;
            char tt[24];
            bool ok = false;
            tagsvc::get_decode(dr, tt, &ok);
            if (ok && dr.recognized) {
                bambu::Spool s;
                filament_to_spool(dr.filament, s);  // starts from default_spool()
                render_card(s);
                strncpy(home_card_uid, home_try_uid, sizeof(home_card_uid) - 1);
                home_card_uid[sizeof(home_card_uid) - 1] = 0;
            } else {
                home_card_uid[0] = 0;   // nothing to show -> the write preset
                refresh_home_spool();
            }
        }
        return;
    }

    // No tag, or a blank/unsupported one: show the write preset.
    tagops::Kind kind = snap.present ? snap.card.kind : tagops::NONE;
    bool decodable = kind == tagops::GENUINE || kind == tagops::CUSTOM ||
                     kind == tagops::LOCKED;
    if (!snap.present || !decodable) {
        home_try_uid[0] = 0;
        if (home_card_uid[0]) {
            home_card_uid[0] = 0;
            refresh_home_spool();
        }
        return;
    }

    // A decodable tag is present: decode it once per UID and show it.
    char uid[21];
    tagops::format_uid(snap.card.uid, uid);
    if (strcmp(uid, home_try_uid) != 0) {
        strncpy(home_try_uid, uid, sizeof(home_try_uid) - 1);
        home_try_uid[sizeof(home_try_uid) - 1] = 0;
        bambu::Spool s;
        spool::get(s);  // the decode ignores it; just needed by the API
        if (tagsvc::submit(tagsvc::OP_DECODE, tagsvc::OWNER_UI, &s, spool::date(), 60000))
            home_decoding = true;
        else
            home_try_uid[0] = 0;  // reader busy; retry next tick
    }
#endif
}

static void tick(lv_timer_t *) {
    if (led_off_at && esp_timer_get_time() > led_off_at)
        led(false, false, false, 0);
    tagsvc::Snapshot snap;
    tagsvc::snapshot(snap);
    switch (current) {
    case HOME:
        if (snap.seq != shown_seq)
            refresh_home_tag(snap);
        update_home_card(snap);
        if (home_card_uid[0] == 0 && !home_decoding &&
            spool::version() != shown_spool_version)
            refresh_home_spool();
        break;
    case JOB:
        refresh_job(snap);
        break;
    default:
        break;
    }
}

static void service();

static void on_timer(lv_timer_t *t) {
    service();
    if (root)
        tick(t);
}

// The display deleted our root (back to its home screen, or a console `nav`):
// stop everything that points into it and let go of the reader.
static void on_root_deleted(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != root)
        return;  // only the screen root's own delete, not a child's
    if (service_timer) {
        lv_timer_delete(service_timer);
        service_timer = nullptr;
    }
    tagsvc::cancel(tagsvc::OWNER_UI);  // a job still waiting for a tag gives up
    led(false, false, false, 0);
    delete lib;
    lib = nullptr;
    root = nullptr;
    current = pending = NONE;
    pending_job = tagsvc::OP_NONE;
    clone_stage = CL_OFF;
    exit_wanted = false;
    job_decode = DecodeResult();  // free the decoded tag's strings/maps
    job_decode_ok = false;
    tagsvc::release();
}

void open(lv_obj_t *scr, void (*on_exit)(void)) {
    root = scr;
    exit_cb = on_exit;
    exit_wanted = false;
    current = pending = NONE;
    pending_job = tagsvc::OP_NONE;
    clone_stage = CL_OFF;

    gpio_config_t leds = {};
    leds.pin_bit_mask = (1ULL << LED_R) | (1ULL << LED_G) | (1ULL << LED_B);
    leds.mode = GPIO_MODE_OUTPUT;
    gpio_config(&leds);
    led(false, false, false, 0);

    tagsvc::acquire();
    lv_obj_add_event_cb(root, on_root_deleted, LV_EVENT_DELETE, nullptr);
    build_home();
    service_timer = lv_timer_create(on_timer, 50, nullptr);
}

static void service() {
    if (!root)
        return;
    if (exit_wanted) {
        exit_wanted = false;
        if (exit_cb)
            exit_cb();  // asynchronous: the display deletes the root later
        return;
    }
    if (pending_job != tagsvc::OP_NONE) {
        tagsvc::Op op = pending_job;
        pending_job = tagsvc::OP_NONE;
        pending = NONE;
        start_job(op);
    }
    if (pending != NONE) {
        ScreenId next = pending;
        pending = NONE;
        switch (next) {
        case HOME: build_home(); break;
        case MATERIAL: build_material(); break;
        case COLOR: build_color(); break;
        case DETAILS: build_details(); break;
        case TAG_VIEW: build_tag_view(); break;
        case SETTINGS: build_settings(); break;
        case LIBRARY: build_library(); break;
        case LIBRARY_COLORS: build_library_colors(); break;
        case LIBRARY_CONFIRM: build_library_confirm(); break;
        default: break;
        }
    }
}

}  // namespace ui
