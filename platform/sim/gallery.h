#ifndef GALLERY_H
#define GALLERY_H
#include "lvgl.h"
/* One page per component family; each lives in platform/sim/gallery_<name>.c
 * and draws its components onto scr (a fresh 480x320 screen). */
void gallery_page_band(lv_obj_t *scr);
void gallery_page_input(lv_obj_t *scr);
void gallery_page_display(lv_obj_t *scr);
void gallery_page_list(lv_obj_t *scr);
void gallery_page_steps(lv_obj_t *scr);
void gallery_page_tile(lv_obj_t *scr);
void gallery_page_sheet(lv_obj_t *scr);
/* Render every page to DIR/gallery_<name>.bmp and audit it ("gallery/<name>").
 * Returns the number of audit issues (0 = clean), or -1 if a BMP could not be written. */
int gallery_run(const char *dir);
#endif
