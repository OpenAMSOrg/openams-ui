/**
 * @file gallery.c
 * Component gallery: renders each family's page (one per gallery_<name>.c) to
 * DIR/gallery_<name>.bmp and audits it, so the component authors have a place
 * to see their work and catch layout regressions.
 *
 * Simulator-only tooling; never reaches the ESP32 build.
 */

#include "gallery.h"

#include "audit.h"
#include "sim.h"
#include "ui/ui_nav.h"
#include "ui/ui_theme.h"

#include <stddef.h>
#include <stdio.h>

typedef struct {
    const char *name;
    void (*page)(lv_obj_t *scr);
} gallery_page_t;

static const gallery_page_t s_pages[] = {
    { "band", gallery_page_band },
    { "input", gallery_page_input },
    { "display", gallery_page_display },
    { "list", gallery_page_list },
    { "steps", gallery_page_steps },
    { "tile", gallery_page_tile },
    { "sheet", gallery_page_sheet },
};

int gallery_run(const char *dir)
{
    static lv_obj_t *prev;
    size_t i;
    int issues = 0;

    for(i = 0; i < sizeof s_pages / sizeof s_pages[0]; i++) {
        lv_obj_t *scr = lv_obj_create(NULL);
        char path[512];
        char where[64];

        lv_obj_remove_style_all(scr);
        lv_obj_add_style(scr, ui_style(UI_STYLE_SCREEN), 0);
        lv_screen_load(scr);
        if(prev) lv_obj_delete(prev);
        prev = scr;

        s_pages[i].page(scr);
        sim_pump(40);

        snprintf(path, sizeof path, "%s/gallery_%s.bmp", dir, s_pages[i].name);
        if(!sim_capture_bmp(path, true)) return -1;

        snprintf(where, sizeof where, "gallery/%s", s_pages[i].name);
        issues += audit_run(where);

        ui_nav_modal_close_all();
    }

    return issues;
}
