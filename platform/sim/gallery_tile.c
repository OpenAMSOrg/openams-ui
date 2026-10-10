/**
 * @file gallery_tile.c
 * Gallery page for the slot tiles: unit A and unit B at the 110x150 design
 * size (docs/VISUAL_SPEC_480.md, "Home -> Tiles"), left to right, with the
 * "loading" scenario so a busy, a loaded, a low and an unknown slot all
 * appear at once.
 */

#include "gallery.h"

#include "model/mmu_model.h"
#include "ui/ui_theme.h"
#include "ui/ui_tile.h"

void gallery_page_tile(lv_obj_t *scr)
{
    /* The home grid's own x positions: inset 8, 110 px tiles, 8 px gaps. */
    static const int x[4] = { 8, 126, 244, 362 };
    int i;

    /* A1 loaded, A2 PETG-CF 45%, A3 loading, A4 unknown; B1 ASA 12%,
     * B2 PETG 95%, B3 TPU, B4 error. */
    mmu_model_set_scenario("loading");

    for(i = 0; i < 4; i++) {
        ui_tile_slot(scr, 0, i, x[i], 8, 110, 150, NULL, NULL);
        ui_tile_slot(scr, 1, i, x[i], 162, 110, 150, NULL, NULL);
    }
}
