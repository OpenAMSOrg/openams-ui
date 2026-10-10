/**
 * @file screen_unitinfo.c
 * Screen 13: unit info, in the design language (ui_kit). The identity of the
 * unit this display is bound to: its family and config name (the view's title
 * and subtitle), then serial and firmware (the view's info) and the newest
 * firmware, with an "Update available" chip when the firmware is behind. The
 * newest version and the update flag are not in the view yet (a view gap), so
 * they come from the model.
 */
#include "ui/ui_screens.h"

#include "model/mmu_model.h"
#include "ui/ui_kit.h"
#include "view/strings.h"
#include "view/view.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROW_H 44

/* A read-only row: a muted label at the left, the value at the right, and an
 * optional chip immediately left of the value. */
static lv_obj_t *info_row(lv_obj_t *col, const char *label, const char *value, const char *chip)
{
    lv_obj_t *row = ui_kit_row(col, ROW_H, NULL, NULL);
    int vw = ui_home_text_w(value, UI_HOME_FONT_BODY);
    int inner = UI_HOME_INNER_W - 24;
    int ly = (ROW_H - 24 - lv_font_get_line_height(UI_HOME_FONT_BODY)) / 2;

    ui_home_label(row, label, UI_HOME_FONT_BODY, UI_TOK_TEXT_MUTED, 0, ly);
    ui_home_label(row, value, UI_HOME_FONT_BODY, UI_TOK_TEXT, inner - vw, ly);
    if(chip) ui_kit_tag(row, chip, UI_KIT_TONE_INFO, inner - vw - 8 - ui_kit_tag_w(chip),
                        (ROW_H - 24 - 20) / 2);
    return row;
}

static lv_obj_t *build(void)
{
    lv_obj_t *scr = ui_kit_screen();
    lv_obj_t *col;
    view_unit_t *u = malloc(sizeof *u);
    mmu_unit_ident_t ident;
    int unit = mmu_this_unit();

    (void)ui_kit_header(scr, ui_nav_screen_title(UI_SCREEN_UNIT_INFO), true);
    col = ui_kit_column(scr, UI_HOME_HEADER_H + 2, UI_HOME_H - UI_HOME_HEADER_H - 2);

    if(u && view_unit(&g_mmu, unit, u)) {
        lv_obj_t *hero = ui_kit_row(col, 48, NULL, NULL);
        int tw = ui_home_text_w(u->title, UI_HOME_FONT_TITLE);

        ui_home_label(hero, u->title, UI_HOME_FONT_TITLE, UI_TOK_TEXT, 0, (48 - 24 - 24) / 2 + 2);
        ui_home_label(hero, u->subtitle, UI_HOME_FONT_META, UI_TOK_TEXT_MUTED, tw + 8, 12);

        if(u->info.present) {
            info_row(col, view_str(VS_UI_SERIAL), "", NULL);   /* no host field carries it yet: the view has none */
            {
                bool update = mmu_unit_update_available(unit);

                info_row(col, view_str(VS_UI_FIRMWARE), "", update ? view_str(VS_UI_UPDATE) : NULL);
            }
        }
    }
    if(mmu_unit_ident(unit, &ident)) info_row(col, view_str(VS_UI_LATEST), ident.latest, NULL);

    free(u);
    return scr;
}

static int modal_count(void)
{
    return 0;
}

static void open_modal(int index)
{
    (void)index;
}

const ui_screen_ops_t screen_unitinfo_ops = { build, modal_count, open_modal, NULL };
