/**
 * @file ui_update.c
 * The setters behind ui_update.h. Every one compares the object against what
 * it already holds, so a screen's update() redraws the boxes that changed and
 * nothing else.
 */

#include "ui/ui_update.h"

#include "model/mmu_model.h"
#include "ui/ui_home.h"
#include "view/view.h"

#include <stdio.h>
#include <string.h>

void ui_update_text(lv_obj_t *label, const char *text)
{
    const char *want = text ? text : "";
    const char *have = lv_label_get_text(label);

    if(have && strcmp(have, want) == 0) return;
    lv_label_set_text(label, want);
}

/* The same cutting as ui_home_label_fit(), and it has to stay the same cutting:
 * a label updated here has to hold the very string a fresh build would have
 * given it, down to the ellipsis and to never cutting a UTF-8 sequence in
 * half. */
static void fit_text(char *buf, size_t n, const char *text, const lv_font_t *font, int max_w)
{
    size_t len;

    snprintf(buf, n, "%s", text ? text : "");
    len = strlen(buf);

    if(ui_home_text_w(buf, font) <= max_w) return;

    while(len > 1) {
        len--;
        while(len > 0 && ((unsigned char)buf[len] & 0xC0) == 0x80) len--;
        buf[len] = '\0';
        if(ui_home_text_w(buf, font) + ui_home_text_w("\xE2\x80\xA6", font) <= max_w) break;
    }
    if(len + 4 < n) memcpy(buf + len, "\xE2\x80\xA6", 4);
}

void ui_update_text_fit(lv_obj_t *label, const char *text, const lv_font_t *font, int max_w)
{
    char buf[128];

    fit_text(buf, sizeof buf, text, font, max_w);
    ui_update_text(label, buf);
}

void ui_update_text_color(lv_obj_t *label, lv_color_t color)
{
    if(lv_color_eq(lv_obj_get_style_text_color(label, LV_PART_MAIN), color)) return;
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
}

void ui_update_bg(lv_obj_t *box, lv_color_t color)
{
    if(lv_color_eq(lv_obj_get_style_bg_color(box, LV_PART_MAIN), color)) return;
    lv_obj_set_style_bg_color(box, color, LV_PART_MAIN);
}

void ui_update_border(lv_obj_t *box, lv_color_t color, lv_opa_t opa)
{
    if(!lv_color_eq(lv_obj_get_style_border_color(box, LV_PART_MAIN), color))
        lv_obj_set_style_border_color(box, color, LV_PART_MAIN);
    if(lv_obj_get_style_border_opa(box, LV_PART_MAIN) != opa)
        lv_obj_set_style_border_opa(box, opa, LV_PART_MAIN);
}

/* The sweep both rings draw: a percent of 360, never under 8 while it is at
 * all, so a nearly empty spool still shows a mark. */
static int sweep_of(int percent)
{
    int sweep;

    if(percent <= 0) return 0;
    sweep = (percent * 360 + 50) / 100;
    if(sweep < 8) sweep = 8;
    if(sweep > 360) sweep = 360;
    return sweep;
}

void ui_update_arc(lv_obj_t *arc, int percent, lv_color_t color)
{
    int sweep = sweep_of(percent);

    if((int)lv_arc_get_angle_end(arc) != sweep) lv_arc_set_angles(arc, 0, sweep);
    if(!lv_color_eq(lv_obj_get_style_arc_color(arc, LV_PART_INDICATOR), color))
        lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
}

void ui_update_alert_mark(lv_obj_t *slot, int tone, int count)
{
    char text[16];
    lv_color_t c = ui_home_tone_color(tone);
    lv_obj_t *icon = lv_obj_get_child(slot, 0);
    lv_obj_t *num = lv_obj_get_child(slot, 1);

    if(!icon || !num) return;
    ui_update_text(icon, tone == VIEW_TONE_ERROR ? UI_HOME_ICON_ERROR : UI_HOME_ICON_INFO);
    ui_update_text_color(icon, c);

    snprintf(text, sizeof text, "%d", count > 99 ? 99 : count);
    ui_update_text(num, text);
    ui_update_text_color(num, c);
}

void ui_update_swatch(lv_obj_t *swatch, uint32_t rgb, bool empty)
{
    /* The fill and the hairline ui_kit_swatch() sets, in both directions, so a
     * swatch that fills and one that empties again both land on what a fresh
     * build draws. */
    lv_opa_t fill = empty ? LV_OPA_TRANSP : LV_OPA_COVER;
    lv_color_t line = empty ? UI_TOK_TEXT_FAINT : UI_TOK_LINE;
    lv_opa_t line_opa = empty ? LV_OPA_COVER : 60;

    if(lv_obj_get_style_bg_opa(swatch, LV_PART_MAIN) != fill)
        lv_obj_set_style_bg_opa(swatch, fill, LV_PART_MAIN);
    if(!empty) ui_update_bg(swatch, lv_color_hex(rgb));
    ui_update_border(swatch, line, line_opa);
}

/* The header's title label: ui_kit_header()'s own second child. */
void ui_update_header_title(lv_obj_t *hdr, const char *text)
{
    lv_obj_t *title = (hdr && lv_obj_get_child_count(hdr) > 1) ? lv_obj_get_child(hdr, 1) : NULL;
    int x;

    if(!title) return;
    /* The room the header left for the title, read off the title itself
     * rather than mirrored: from where it sits to the alert slot's edge. */
    x = lv_obj_get_style_x(title, LV_PART_MAIN);
    ui_update_text_fit(title, text, UI_HOME_FONT_TITLE,
                       UI_HOME_W - x - UI_HOME_HEADER_PAD - UI_HOME_SLOT_W - UI_PX(8));
}

void ui_update_alert_totals(int *count, int *tone, bool *clear)
{
    view_unit_t u;
    view_toolhead_t th;
    int unit = mmu_this_unit();
    bool unit_ok = view_unit(&g_mmu, unit, &u);

    /* This unit's alerts and its toolhead's, summed, in the tone of the worse:
     * the numbers every sub-screen header's alert slot is drawn from (ui_kit.c
     * calls this too), so the slot can be brought up to date without the kit
     * having to be told. It rests on the check (clear) only when neither scope
     * has an alert of any kind, shown elsewhere or not. */
    *count = 0;
    *tone = VIEW_TONE_INFO;
    *clear = unit_ok && u.alert.clear;
    if(unit_ok && u.alert.present) {
        *count += u.alert.count;
        if(u.alert.tone == VIEW_TONE_ERROR) *tone = VIEW_TONE_ERROR;
    }
    if(unit >= 0 && unit < (int)g_mmu.unit_count && g_mmu.units[unit].toolhead >= 0 &&
       view_toolhead(&g_mmu, g_mmu.units[unit].toolhead, &th)) {
        if(!th.alert.clear) *clear = false;
        if(th.alert.present) {
            *count += th.alert.count;
            if(th.alert.tone == VIEW_TONE_ERROR) *tone = VIEW_TONE_ERROR;
        }
    }
}

int ui_update_alert_slot_objects(int count, bool clear)
{
    return count > 0 ? 2 : (clear ? 1 : 0);
}
