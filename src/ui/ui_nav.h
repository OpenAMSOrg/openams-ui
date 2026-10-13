/**
 * @file ui_nav.h
 * One screen alive at a time: navigating builds the new screen and lets LVGL
 * delete the old one. Modals live on lv_layer_top() and are deleted on close.
 */

#ifndef UI_NAV_H
#define UI_NAV_H

#include "lvgl.h"

typedef enum {
    UI_SCREEN_HOME = 0,
    UI_SCREEN_LOAD,        /* 3  load / unload progress */
    UI_SCREEN_ALERTS,      /* 4  alert list */
    UI_SCREEN_EDIT,        /* 5  slot edit dialog */
    UI_SCREEN_ENVIRONMENT, /* 6  environment and drying */
    UI_SCREEN_PRINT,       /* 7  in-print status */
    UI_SCREEN_REFILL,      /* 8  auto-refill groups */
    UI_SCREEN_SETTINGS,    /* 9  MMU settings */
    UI_SCREEN_MAPPING,     /* 10 print-start mapping */
    UI_SCREEN_ORDER,       /* 11 unit order and side binding */
    UI_SCREEN_UNIT_INFO,   /* 12 unit info */
    UI_SCREEN_DEBUG,       /* hidden, long-press the title bar */
#if defined(MMU_FEATURE_TAG_WRITER)
    UI_SCREEN_TAG_WRITER,  /* the spool tag writer (ESP32 image only) */
#endif
    UI_SCREEN_COUNT
} ui_screen_id_t;

void ui_nav_init(void);

/** Build and show @p id, deleting the previous screen. Returns false when the
 * screen could not be built (used by --smoke). */
bool ui_nav_goto(ui_screen_id_t id);
/** Back to home (the hub). */
void ui_nav_back(void);
/** Rebuild the screen currently shown. */
void ui_nav_refresh(void);
/** Ask the screen on screen to bring its objects up to date in place
 * (ui_screen_ops_t::update). Returns false when it has none, or when its
 * structure would change - so the caller rebuilds instead. Cheap enough to
 * try on every model change; that is the whole point of it. */
bool ui_nav_update(void);

/** Deferred forms of ui_nav_goto() / ui_nav_back() / ui_nav_refresh(): the
 * actual rebuild runs via lv_async_call(), after the triggering click or
 * timer callback has returned, so it never deletes a screen (or a timer)
 * that is still on the call stack dispatching the very event that asked for
 * the rebuild. Repeated requests before the async call lands collapse into
 * one: a goto always wins over a plain refresh, and the latest goto target
 * wins over an earlier one. Every click/timer handler that rebuilds or
 * leaves the current screen should use these, not the synchronous forms. */
void ui_nav_goto_async(ui_screen_id_t id);
void ui_nav_back_async(void);
void ui_nav_refresh_async(void);

ui_screen_id_t ui_nav_current(void);

/** Screen builds since the last reset: how many, and their total and longest
 * time in ms (building and loading, not the refresh that draws it). */
void ui_nav_build_stats(uint32_t *builds, uint32_t *ms_total, uint32_t *ms_max, bool reset);
bool ui_nav_at_home(void);
const char *ui_nav_screen_name(ui_screen_id_t id);
const char *ui_nav_screen_title(ui_screen_id_t id);

/* modals on lv_layer_top() */
void ui_nav_modal_add(lv_obj_t *modal);
void ui_nav_modal_close_all(void);
int ui_nav_modal_count(void);

/** Pool bytes that must be free before a screen / a sheet is built. A screen is
 * up to ~45 KB (Settings on a live host in the longest language) and a sheet
 * ~12 KB; the check is only a guard against building into a pool that cannot
 * hold the result, since LVGL itself does not survive a failed allocation. */
#define UI_NAV_SCREEN_NEED  (52 * 1024)
#define UI_NAV_SHEET_NEED   (16 * 1024)

/** True when at least @p need bytes of the LVGL pool are free (or the
 * allocator cannot say). Logs a warning when not. */
bool ui_nav_mem_ok(size_t need);

/* alert bell */
void ui_nav_set_alert_count(int count);
int ui_nav_alert_count(void);

#endif /* UI_NAV_H */
