/**
 * @file app_live.c
 * The live counterpart of app.c (see app_live.h).
 */
#include "app/app_live.h"

#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include "app/actions.h"
#include "backend/status_map.h"
#include "model/mmu_model.h"
#include "ui/ui_nav.h"
#include "ui/ui_sheet.h"
#include "ui/ui_spool_sheets.h"
#include "ui/ui_theme.h"
#include "view/strings.h"

/* A status update can arrive every few milliseconds; a full screen teardown
 * and rebuild on each one would flicker and never settle, so at most one
 * lands per this interval. A screen brought up to date in place is not a
 * rebuild and is not rate limited: it draws only the boxes that changed. */
#define LIVE_REBUILD_MIN_MS 500

static bool s_rebuild_wanted;
static uint32_t s_last_rebuild_ms;

/* Bit i is set once we have shown the "New spool, full?" sheet for slot i of
 * g_mmu.this_unit.  Cleared when pending_confirmation goes false (the episode
 * ends), so the next insertion triggers a fresh prompt. Up to 8 slots. */
static uint8_t s_confirm_shown;

/* The last spool-edit answer the display has said (g_mmu.edit_seq), and whether
 * the vendor list has been asked for since the host said it can edit. */
static uint8_t s_edit_seq_seen;
static bool s_vendors_asked;

/* The last action answer the display has said (g_mmu.action_seq). A load or an
 * unload refusal is the load screen's own business; every other one is said
 * here, once, as a toast. */
static uint8_t s_action_seq_seen;

/* Last filename for which we requested job metadata, so we only request once
 * per unique non-empty filename. */
static char s_last_filename[128];

/* Not safe on a form the user is filling in (their input would be wiped),
 * nor while an overlay is up: sheets, pickers, modals and toasts all live on
 * lv_layer_top(), and a screen refresh does not touch that layer. */
static bool rebuild_is_safe(void)
{
    switch(ui_nav_current()) {
    case UI_SCREEN_EDIT:
    case UI_SCREEN_SETTINGS:
    case UI_SCREEN_ORDER:
    case UI_SCREEN_DEBUG:
        return false;
    default:
        break;
    }

    return lv_obj_get_child_count(lv_layer_top()) == 0;
}

void app_live_init(void)
{
    mmu_model_set_live(true);
    status_map_reset(&g_mmu);
    s_confirm_shown = 0;
    s_last_filename[0] = '\0';
    s_edit_seq_seen = g_mmu.edit_seq;
    s_vendors_asked = false;
    ui_theme_init();
    ui_nav_init();
    ui_nav_goto(UI_SCREEN_HOME);
}

void app_live_model_changed(void)
{
    s_rebuild_wanted = true;
}

/* Check each slot of this display's unit for a pending_confirmation episode
 * and open the confirm sheet the first time per episode. */
static void poll_confirm(void)
{
    int this_u = g_mmu.this_unit;
    const mmu_unit_t *u;
    int slot;

    if(this_u < 0 || this_u >= (int)g_mmu.unit_count) return;
    u = &g_mmu.units[this_u];

    for(slot = 0; slot < (int)u->slot_count && slot < 8; slot++) {
        uint8_t bit = (uint8_t)(1u << (unsigned)slot);
        if(u->slots[slot].pending_confirmation) {
            if(!(s_confirm_shown & bit)) {
                s_confirm_shown |= bit;
                ui_spool_confirm_open(this_u, slot);
                return; /* one sheet at a time */
            }
        } else {
            s_confirm_shown &= (uint8_t)~bit; /* episode ended; ready for next */
        }
    }
}

/* When the print job's filename changes to a non-empty value, request the
 * file's metadata so job_need_g[] and job_short can be updated. */
static void poll_metadata(void)
{
    if(!g_mmu.print_filename[0]) return;
    if(strcmp(g_mmu.print_filename, s_last_filename) == 0) return;
    snprintf(s_last_filename, sizeof s_last_filename, "%s", g_mmu.print_filename);
    actions_log("job metadata %s", g_mmu.print_filename);
}

/* The spool editor's vendor choice is Spoolman's own list: ask for it once the
 * host says it can edit (and again after a reconnect, which forgets both). */
static void poll_vendors(void)
{
    if(!g_mmu.spoolman_edit) {
        s_vendors_asked = false;
        return;
    }
    if(s_vendors_asked) return;
    s_vendors_asked = true;
    actions_log("vendor list");
}

/* The host's answer to a spool edit: a refusal is said once, in its own words
 * after the display's lead; a taken edit asks for the vendors again, since the
 * host may have added one (Generic, on the first spool of a bay). */
static void poll_edit_result(void)
{
    char text[320];

    if(g_mmu.edit_seq == s_edit_seq_seen) return;
    s_edit_seq_seen = g_mmu.edit_seq;

    if(g_mmu.edit_refusal[0]) {
        snprintf(text, sizeof text, "%s %s", view_str(VS_LABEL_EDIT_FAILED), g_mmu.edit_refusal);
        ui_sheet_toast(text);
    }
    else {
        s_vendors_asked = false;
    }
}

/* The host's answer to an action the display sent. A refused load or unload is
 * shown by the load screen, which the user is looking at; any other refusal
 * has nowhere else to go, so it is said once as a toast in the host's own
 * words after the display's lead. */
static void poll_action_result(void)
{
    char text[320];

    if(g_mmu.action_seq == s_action_seq_seen) return;
    s_action_seq_seen = g_mmu.action_seq;

    if(!g_mmu.action_refusal[0]) return;
    if(g_mmu.action_refusal_kind == MMU_ACTION_LOAD ||
       g_mmu.action_refusal_kind == MMU_ACTION_UNLOAD) return;

    snprintf(text, sizeof text, "%s %s", view_str(VS_ACTION_REFUSED), g_mmu.action_refusal);
    ui_sheet_toast(text);
}

void app_live_poll(void)
{
    if(s_rebuild_wanted && rebuild_is_safe()) {
        if(ui_nav_update()) {
            /* The screen on screen was brought up to date where it could be:
             * no teardown, and no wait for the rebuild interval either - the
             * point of an in-place update is that the screen keeps answering
             * while the model moves. */
            s_rebuild_wanted = false;
        }
        else if(s_last_rebuild_ms == 0 ||
                lv_tick_elaps(s_last_rebuild_ms) >= LIVE_REBUILD_MIN_MS) {
            s_rebuild_wanted = false;
            s_last_rebuild_ms = lv_tick_get();
            ui_nav_refresh_async();
        }
    }

    /* Show "New spool, full?" once per slot per pending episode, only when no
     * overlay is already up and we are not on a form screen. */
    if(rebuild_is_safe()) poll_confirm();

    /* Request file metadata when the print filename changes. */
    poll_metadata();

    poll_edit_result();
    poll_action_result();
    poll_vendors();
}
