/**
 * @file screen_home.c
 * The home screen: the one unit this display is mounted on, in the approved
 * design language (docs/design/UNIFIED_UI.md 4a-4c, 5, 7).
 *
 *   0..44    ui_unit_header:    one line (family name, config name inline),
 *                              the environment pill, the alert slot
 *   46..68   ui_toolhead_message: the one message row (and the lane's alert)
 *   72..222  ui_tile_bay:       four tiles across, 108x150, fixed geometry
 *   222..320 ui_toolhead_strip: the filament path, the extruder icon with the
 *                              loaded tool
 *
 * The screen reads the model only through the builders - view_unit(),
 * view_tile(), view_toolhead() - and composes no text of its own (§2's rule).
 * Each widget's view struct is built on the stack, once per screen build, and
 * lives no longer than the build (§4).
 */

#include "ui/ui_screens.h"

#include "app/actions.h"
#include "app/platform.h"
#include "model/mmu_model.h"
#include "ui/ui_chevron.h"
#include "ui/ui_display.h"
#include "ui/ui_group_picker.h"
#include "ui/ui_home.h"
#include "ui/ui_icons.h"
#include "ui/ui_nav.h"
#include "ui/ui_kit.h"
#include "ui/ui_sheet.h"
#include "ui/ui_spool_sheets.h"
#include "ui/ui_theme.h"
#include "ui/ui_tile.h"
#include "ui/ui_toolhead_strip.h"
#include "ui/ui_unit_header.h"
#include "ui/ui_update.h"
#include "ui/ui_widgets.h"
#include "view/strings.h"
#include "view/view.h"

#include <stdio.h>
#include <string.h>

/* Which unit the display belongs to, and its one toolhead's index. */
static int home_unit(void)
{
    return mmu_this_unit();
}

static int home_toolhead(void)
{
    return mmu_this()->toolhead;
}

/* The page of tiles on screen: one page is UI_HOME_TILE_COLS bays. The screen
 * rebuilds on every status refresh, so the page is not a widget but module
 * state - it survives the rebuild and resets only when the display is bound
 * to another unit. */
static int s_page;
static int s_page_unit = -1;
static char s_page_name[8];

/** How many pages of tiles a unit with @p bays bays has. */
static int home_pages(int bays)
{
    return (bays + UI_HOME_TILE_COLS - 1) / UI_HOME_TILE_COLS;
}

/* ------------------------------------------------------------- the tiles */

static void open_slot_sheet(int unit, int slot);

static void on_tile_tap(lv_event_t *e)
{
    void *ud = lv_event_get_user_data(e);

    open_slot_sheet(UI_DEC_A(ud), UI_DEC_B(ud));
}

/* The bay this toolhead's lane reports loaded, whatever tag its tile wears (a
 * runout, an error), or -1 when the filament came from elsewhere or nothing
 * is loaded. Never the bay a tool label finds: two bays in one group both
 * carry its tool, and the path once ran from whichever of them came last.
 * The view has no field for the loaded bay, so this reads the lane itself. */
static int loaded_bay(const view_toolhead_t *th)
{
    int t = home_toolhead();
    const mmu_toolhead_t *lane;

    if(!th->tool.present || t < 0 || t >= (int)g_mmu.toolhead_count) return -1;
    lane = &g_mmu.toolheads[t];
    if(lane->loaded_unit != home_unit()) return -1;
    if(lane->loaded_slot < 0 || lane->loaded_slot >= (int)mmu_this()->slot_count) return -1;
    return lane->loaded_slot;
}

/* Where the chevrons put the page: the loaded bay's page on arrival at a
 * unit, the page already on screen afterwards, and never a page that does not
 * exist (a unit's bay count can change under a live link). */
static int home_page(int bays, int loaded)
{
    int pages = home_pages(bays);

    if(pages <= 0) {
        s_page = 0;
    }
    else if(s_page_unit != home_unit()
            || strcmp(s_page_name, mmu_unit_name(home_unit())) != 0) {
        s_page = loaded >= 0 ? loaded / UI_HOME_TILE_COLS : 0;
        s_page_unit = home_unit();
        mmu_copy_text(s_page_name, sizeof(s_page_name), mmu_unit_name(home_unit()));
    }
    if(s_page >= pages) s_page = pages - 1;
    return s_page;
}

/* One row of four: the bays of the current page, at today's positions and
 * sizes whatever the unit's bay count (the chevrons page the rest; with four
 * bays or fewer there is only ever one page, and this is the row that was
 * always drawn). Writes the page and the tiles on it, and returns the loaded
 * bay's column on this page, or -1 when it is on another page or in no tile
 * at all - the strip then draws no path. */
static int build_tiles(lv_obj_t *scr, const view_unit_t *u, const view_toolhead_t *th,
                       int *page_out, int *pages_out, int *tiles_out)
{
    int loaded = loaded_bay(th);
    int first;
    int n;
    int i;

    *page_out = home_page(u->bay_count, loaded);
    *pages_out = home_pages(u->bay_count);

    first = *page_out * UI_HOME_TILE_COLS;
    n = u->bay_count - first;
    if(n > UI_HOME_TILE_COLS) n = UI_HOME_TILE_COLS;
    *tiles_out = n;

    for(i = 0; i < n; i++) {
        view_tile_t t;

        if(!view_tile(&g_mmu, home_unit(), first + i, &t)) continue;
        ui_tile_bay(scr, &t, UI_HOME_TILE_X(i), UI_HOME_TILES_Y,
                    UI_HOME_TILE_W, UI_HOME_TILE_H, on_tile_tap,
                    UI_ENC(home_unit(), first + i));
    }

    if(loaded < first || loaded >= first + n) return -1;
    return loaded - first;
}

/* One page left or right, then the screen again. The page outlives the
 * refresh, so the rebuild reads it back and stays where it is; a chevron with
 * nowhere to go is not a target, so this only ever moves one page. */
static void on_chevron(lv_event_t *e)
{
    s_page += (int)(intptr_t)lv_event_get_user_data(e);

    actions_log("home page %d", s_page + 1);
    ui_nav_refresh_async();
}

/* ------------------------------------------------------------ navigation */

/* The message row is the one line that says what happened, so it is the way
 * to the screen that says more about it. Which one depends on what the core
 * put in the message; the tone alone cannot tell a fault from a runout. */
static void on_message(lv_event_t *e)
{
    view_toolhead_t th;

    (void)e;
    if(!view_toolhead(&g_mmu, home_toolhead(), &th)) return;

    if(th.message.tone != VIEW_TONE_NEUTRAL) {
        actions_log("alerts");
        ui_nav_goto_async(UI_SCREEN_ALERTS);
    }
    else if(th.activity.kind == VIEW_ACTIVITY_LOADING
            || th.activity.kind == VIEW_ACTIVITY_UNLOADING) {
        actions_log("load status");
        ui_nav_goto_async(UI_SCREEN_LOAD);
    }
    else {
        actions_log("print status");
        ui_nav_goto_async(UI_SCREEN_PRINT);
    }
}

static void on_alerts(lv_event_t *e)
{
    (void)e;
    actions_log("alerts");
    ui_nav_goto_async(UI_SCREEN_ALERTS);
}

static void on_menu(lv_event_t *e)
{
    (void)e;
    actions_log("screens");
    ui_sheet_menu();
}

static void on_environment(lv_event_t *e)
{
    (void)e;
    actions_log("environment");
    ui_nav_goto_async(UI_SCREEN_ENVIRONMENT);
}

/* ------------------------------------------------------------- the sheet */

enum {
    SH_LOAD = 0,
    SH_UNLOAD,
    SH_EDIT,
    SH_REREAD,
    SH_CHANGE_GROUP,
    SH_ASSIGN_SPOOL,
};

/* The sheet is built for one bay at a time, so the rows carry only which
 * action they are; the target lives here. */
static int s_sheet_unit = -1;
static int s_sheet_slot = -1;

/** What the tile calls this bay: ready with nothing saying what is in it, the
 *  state its own Load row asks about before it loads anything. */
static bool sheet_bay_is_unknown(void)
{
    view_tile_t t;

    return view_tile(&g_mmu, s_sheet_unit, s_sheet_slot, &t) && t.state == VIEW_TILE_UNKNOWN;
}

static void on_sheet_row(lv_event_t *e)
{
    int which = (int)(intptr_t)lv_event_get_user_data(e) - 1;
    char id[MMU_SLOT_ID_LEN];

    if(s_sheet_unit < 0 || s_sheet_slot < 0) return;
    mmu_slot_id(s_sheet_unit, s_sheet_slot, id, sizeof id);

    switch(which) {
    case SH_LOAD:
        /* A bay nobody has identified asks for its spool first, so what the
         * host is told is a filament the user chose (PRINCIPLES.md 13): the
         * editor stores the spool and the load follows its answer. */
        if(sheet_bay_is_unknown()) {
            screen_edit_save_and_load();
            mmu_home_set_selection(s_sheet_unit, s_sheet_slot, -1);
            ui_sheet_close(NULL);
            ui_nav_goto_async(UI_SCREEN_EDIT);
            break;
        }
        actions_log("load %s", id);
        mmu_model_start_load(s_sheet_unit, s_sheet_slot);
        ui_sheet_close(NULL);
        ui_nav_goto_async(UI_SCREEN_LOAD);
        break;
    case SH_UNLOAD:
        actions_log("unload %s", id);
        mmu_model_start_unload();
        ui_sheet_close(NULL);
        ui_nav_goto_async(UI_SCREEN_LOAD);
        break;
    case SH_EDIT:
        actions_log("edit %s", id);
        mmu_home_set_selection(s_sheet_unit, s_sheet_slot, -1);
        ui_sheet_close(NULL);
        ui_nav_goto_async(UI_SCREEN_EDIT);
        break;
    case SH_REREAD:
        actions_log("reread %s", id);
        mmu_model_set_reread(s_sheet_unit, s_sheet_slot);
        ui_sheet_close(NULL);
        ui_nav_refresh_async();
        break;
    case SH_CHANGE_GROUP:
        actions_log("change group %s", id);
        ui_group_picker_open(s_sheet_unit, s_sheet_slot);
        break;
    case SH_ASSIGN_SPOOL:
        ui_spool_assign_open(s_sheet_unit, s_sheet_slot);
        break;
    default:
        break;
    }
}

/* A plain line under the sheet's header, in @p color. */
static void add_note(lv_obj_t *sheet, const char *text, lv_color_t color)
{
    lv_obj_t *body = ui_sheet_body(sheet);
    lv_obj_t *row, *l;

    if(!body || !text[0]) return;
    row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, UI_PX(20));
    lv_obj_set_scrollable(row, false);

    l = lv_label_create(row);
    lv_obj_remove_style_all(l);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, UI_FONT_S, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(l, UI_SCREEN_W - 2 * UI_PX(16));
    lv_obj_set_pos(l, UI_PX(16), UI_PX(2));
    ui_sheet_fit(sheet);
}

/* "T0 · next on runout: B2 (unit B)" / "T0 · no spare spool" / "Not in a
 * group" - a plain line added to the sheet body, under the header. */
static void add_group_note(lv_obj_t *sheet, int unit, int slot)
{
    lv_obj_t *body = ui_sheet_body(sheet);
    lv_obj_t *row, *l;
    char buf[160];
    int group = mmu_slot_group(unit, slot);

    if(!body) return;

    if(group < 0) {
        snprintf(buf, sizeof buf, "%s", view_str(VS_GROUP_NOT_IN));
    }
    else {
        int bu = -1, bs = -1;

        if(mmu_group_next_spare(group, &bu, &bs)) {
            vs_format(VS_HS_NEXT, buf, sizeof buf,
                      (vs_arg_t[]){ VS_ARG_T("group", g_mmu.groups[group].name),
                                    VS_ARG_T("spool", ui_kit_spool(bu, bs)) }, 2);
        }
        else {
            vs_format(VS_HS_NO_SPARE, buf, sizeof buf,
                      (vs_arg_t[]){ VS_ARG_T("group", g_mmu.groups[group].name) }, 1);
        }
    }

    row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, UI_PX(20));
    lv_obj_set_scrollable(row, false);

    l = lv_label_create(row);
    lv_obj_remove_style_all(l);
    lv_label_set_text(l, buf);
    lv_obj_set_style_text_font(l, UI_FONT_S, 0);
    lv_obj_set_style_text_color(l, UI_COL_TEXT_DIM, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(l, UI_SCREEN_W - 2 * UI_PX(16));
    lv_obj_set_pos(l, UI_PX(16), UI_PX(2));

    ui_sheet_fit(sheet);
}

/* Why a row cannot act, or NULL when it can. The order is what matters: an
 * unplugged unit explains everything, then a busy one, then the print. */
static const char *block_reason(const mmu_unit_t *u, int unit, int slot,
                                const char *busy_why, bool print_blocks)
{
    if(!u->connected)   return view_str(VS_HS_OFFLINE);
    if(g_mmu.busy != MMU_BUSY_NONE && mmu_is_this_unit(g_mmu.busy_unit))
        return view_str(VS_HS_BUSY);
    if(busy_why[0])     return busy_why;
    if(print_blocks && g_mmu.printing && g_mmu.print_unit == unit
       && g_mmu.print_slot == slot)
        return view_str(VS_HS_PRINTING);
    return NULL;
}

/* The bay's action sheet. This is milestone A: the sheet still reads the model
 * directly, because the restyle's scope is the screen behind it. */
static void open_slot_sheet(int unit, int slot)
{
    const mmu_unit_t *u;
    const mmu_slot_t *sl;
    const mmu_feed_t src = mmu_toolhead_source();
    lv_obj_t *sheet;
    char id[MMU_SLOT_ID_LEN];
    char title[96];
    char why[160];
    char label[96];
    const char *mat;
    const char *reason;
    int32_t swatch;
    char pct[16];

    if(unit < 0 || unit >= (int)g_mmu.unit_count) return;
    u = &g_mmu.units[unit];
    if(slot < 0 || slot >= (int)u->slot_count) return;
    sl = &u->slots[slot];

    s_sheet_unit = unit;
    s_sheet_slot = slot;
    mmu_home_set_selection(s_sheet_unit, s_sheet_slot, -1);

    if(sl->state == MMU_SLOT_EMPTY)              mat = view_str(VS_MATERIAL_EMPTY);
    else if(sl->info_source == MMU_INFO_UNKNOWN) mat = view_str(VS_MATERIAL_UNKNOWN);
    else                                         mat = sl->material;

    {
        char tool[16];

        ui_kit_tool(unit, slot, tool, sizeof tool);
        vs_format(VS_HS_TITLE, title, sizeof title,
                  (vs_arg_t[]){ VS_ARG_T("tool", tool), VS_ARG_T("material", mat) }, 2);
    }
    mmu_slot_id(unit, slot, id, sizeof id);

    swatch = (sl->state == MMU_SLOT_EMPTY || sl->info_source == MMU_INFO_UNKNOWN ||
              !sl->color_known)
             ? -1 : (int32_t)sl->color;
    if(sl->remaining_pct >= 0) vs_pct(sl->remaining_pct, pct, sizeof pct);
    else                       pct[0] = '\0';

    sheet = ui_sheet_open(title, UI_PX(192));
    if(!sheet) return;   /* memory is short: no sheet, nothing to fill */
    ui_sheet_header(sheet, swatch, pct[0] ? pct : NULL, sl->info_source == MMU_INFO_TAG);
    /* What the tile's tag says, first: its detail, then the host's code. */
    {
        view_tile_t vt;

        if(view_tile(&g_mmu, unit, slot, &vt) && vt.has_tag) {
            lv_color_t tc = ui_home_tone_color(vt.tag_tone);

            add_note(sheet, vt.tag_detail, tc);
            add_note(sheet, vt.tag_code, tc);
        }
    }
    add_group_note(sheet, unit, slot);

    why[0] = '\0';
    (void)mmu_busy_elsewhere(why, sizeof why);

    if(mmu_unit_is_slot_loaded(unit, slot)) {
        /* The one filament this unit has in the toolhead: the row goes back. */
        reason = block_reason(u, unit, slot, why, true);
        ui_sheet_row(sheet, UI_ICON_EJECT, view_str(VS_ACTION_UNLOAD), reason, on_sheet_row,
                     (void *)(intptr_t)(SH_UNLOAD + 1), true);
    }
    else {
        reason = block_reason(u, unit, slot, why, false);

        if(!reason && sl->state == MMU_SLOT_EMPTY)          reason = view_str(VS_HS_EMPTY);
        else if(!reason && sl->info_source == MMU_INFO_UNKNOWN) {
            /* An unidentified bay is loaded by saving its spool first, so the
             * row is dimmed only while that cannot be done - the very reason
             * the Edit row would give. The demo has no Spoolman to ask, and
             * keeps saying what the filament is unknown. */
            if(mmu_model_is_live()) reason = view_tile_edit_reason(&g_mmu, unit, slot);
            else                  reason = view_str(VS_HS_UNKNOWN);
        }
        else if(!reason && g_mmu.printing && g_mmu.print_unit == unit)
            reason = view_str(VS_HS_PRINTING);

        label[0] = '\0';
        if(!reason && (src == MMU_FEED_THIS || src == MMU_FEED_OTHER_UNIT)
           && g_mmu.loaded_unit >= 0 && g_mmu.loaded_slot >= 0) {
            char other[16] = "";

            ui_kit_tool(g_mmu.loaded_unit, g_mmu.loaded_slot, other, sizeof other);
            if(other[0]) vs_format(VS_HS_LOAD_UNLOADS, label, sizeof label,
                                   (vs_arg_t[]){ VS_ARG_T("tool", other) }, 1);
        }
        else if(!reason && src == MMU_FEED_EXT) {
            snprintf(label, sizeof label, "%s", view_str(VS_HS_LOAD_EXT));
        }

        ui_sheet_row(sheet, UI_ICON_TRAY_ARROW_DOWN, label[0] ? label : view_str(VS_ACTION_LOAD), reason,
                     on_sheet_row, (void *)(intptr_t)(SH_LOAD + 1), true);
    }

    reason = block_reason(u, unit, slot, why, true);
    /* against a live host, the one check the web panel dims "Edit spool" by */
    if(!reason && mmu_model_is_live()) reason = view_tile_edit_reason(&g_mmu, unit, slot);
    ui_sheet_row(sheet, UI_ICON_PENCIL_OUTLINE, view_str(VS_HS_EDIT), reason, on_sheet_row,
                 (void *)(intptr_t)(SH_EDIT + 1), false);

    reason = block_reason(u, unit, slot, why, true);
    if(!reason && sl->state == MMU_SLOT_EMPTY)               reason = view_str(VS_HS_EMPTY);
    else if(!reason && (sl->info_source != MMU_INFO_TAG || !u->rfid))
        reason = view_str(VS_HS_NO_TAG);
    ui_sheet_row(sheet, UI_ICON_REFRESH, view_str(VS_HS_REREAD), reason, on_sheet_row,
                 (void *)(intptr_t)(SH_REREAD + 1), false);

    {
        int group = mmu_slot_group(unit, slot);

        if(group < 0 || mmu_group_editable(group)) {
            ui_sheet_row(sheet, UI_ICON_SYNC, view_str(VS_HS_CHANGE_GROUP), NULL, on_sheet_row,
                        (void *)(intptr_t)(SH_CHANGE_GROUP + 1), false);
        }
    }

    if(g_mmu.spoolman_online || !mmu_model_is_live()) {
        ui_sheet_row(sheet, UI_ICON_LINK_VARIANT, view_str(VS_HS_ASSIGN), NULL,
                     on_sheet_row, (void *)(intptr_t)(SH_ASSIGN_SPOOL + 1), false);
    }
}

/* -------------------------------------------------------------- modals */

/* Every state the sheet can be in, as the simulator's walk asks for one. */
typedef enum {
    WANT_NORMAL = 0,
    WANT_LOADED,
    WANT_EMPTY,
    WANT_UNKNOWN,
    WANT_BUSY,
    WANT_PRINTING,
} want_t;

static bool slot_wanted(int slot, want_t want)
{
    const mmu_unit_t *u = mmu_this();
    int unit = home_unit();
    const mmu_slot_t *sl;

    if(slot < 0 || slot >= (int)u->slot_count) return false;
    sl = &u->slots[slot];

    switch(want) {
    case WANT_LOADED:
        return mmu_unit_is_slot_loaded(unit, slot);
    case WANT_EMPTY:
        return sl->state == MMU_SLOT_EMPTY;
    case WANT_UNKNOWN:
        return sl->info_source == MMU_INFO_UNKNOWN;
    case WANT_BUSY:
        return mmu_unit_is_slot_busy(unit, slot);
    case WANT_PRINTING:
        return g_mmu.printing && g_mmu.print_unit == unit && g_mmu.print_slot == slot;
    case WANT_NORMAL:
    default:
        return sl->state != MMU_SLOT_EMPTY && sl->info_source != MMU_INFO_UNKNOWN
               && !mmu_unit_is_slot_loaded(unit, slot);
    }
}

/* The first bay of this unit in the wanted state; bay 0 when it has none, so
 * every modal still shows a sheet. */
static int find_slot(want_t want)
{
    int n = (int)mmu_this()->slot_count;
    int i;

    for(i = 0; i < n; i++)
        if(slot_wanted(i, want)) return i;
    return 0;
}

static int modal_count(void)
{
    return 7;                           /* six sheets, then the Menu */
}

static void open_modal(int index)
{
    want_t want;

    switch(index) {
    case 0:  want = WANT_NORMAL;   break;
    case 1:  want = WANT_LOADED;   break;
    case 2:  want = WANT_EMPTY;    break;
    case 3:  want = WANT_BUSY;     break;
    case 4:  want = WANT_PRINTING; break;
    case 5:  want = WANT_UNKNOWN;  break;
    default:
        actions_log("screens");
        ui_sheet_menu();
        return;
    }
    open_slot_sheet(home_unit(), find_slot(want));
}

/* ------------------------------------------------------- the live update */

/* The objects build() drew, so update() can bring them up to date without
 * rebuilding the screen. A screen rebuild on every model change costs about
 * 50 ms to build and 270 ms to draw on the device, because a new screen
 * redraws every pixel; setting the values on the objects already on screen
 * redraws only the boxes that changed (ui_screens.h's update contract). */

typedef struct {
    lv_obj_t *label;     /* the tool, or "?" */
    lv_obj_t *spare;     /* the chip, or NULL for a tile with no spare */
    lv_obj_t *sublabel;
    lv_obj_t *name;
    lv_obj_t *meta;      /* NULL when the row was too tall to draw */
    lv_obj_t *rfid;      /* NULL for a spool that carries no RFID mark */
    lv_obj_t *ring;      /* the slot the arc sits in */
    lv_obj_t *arc;
    lv_obj_t *pct;       /* NULL when the status tag hides the percentage */
    lv_obj_t *tag;       /* the tag chip, or NULL for a tile with no tag */
} tile_parts_t;

/* The two tile values that are not objects but change what is drawn around
 * them: the hatch over a bay with nothing in it, and the accent ring a loaded
 * tile wears around itself. */
typedef struct {
    bool hatch;
    bool glow;
    bool dim;
    bool color;
} tile_shape_t;

typedef struct {
    lv_obj_t *title;
    lv_obj_t *subtitle;
    lv_obj_t *pill;      /* NULL when the unit has nothing to put in it */
    lv_obj_t *slot;      /* the unit alert's reserved slot */
} header_parts_t;

typedef struct {
    lv_obj_t *msg;       /* the box the one line sits in */
    lv_obj_t *icon;      /* the severity icon, NULL for a neutral tone */
    lv_obj_t *label;
    lv_obj_t *slot;
} message_parts_t;

typedef struct {
    lv_obj_t *stem[UI_HOME_TILE_COLS];   /* one per bay on screen */
    int stems;
    lv_obj_t *run_outline[3];            /* its light outline (transparent unless the color is dark) */
    lv_obj_t *run[3];                    /* the loaded bay's path */
    int runs;
    lv_obj_t *frame;
    lv_obj_t *glyph;
    lv_obj_t *chip;                      /* the loaded tool on the icon */
    lv_obj_t *chip_label;
    lv_obj_t *rest;                      /* the path in words, beside the extruder */
} strip_parts_t;

/* What the last build() drew. Only the shape is kept, never the view structs:
 * their strings are borrowed from the model, so comparing them later could
 * compare a buffer with itself. Every value is compared against the object
 * that holds it instead. */
static struct {
    lv_obj_t *scr;
    header_parts_t hdr;
    message_parts_t msg;
    strip_parts_t strip;
    lv_obj_t *tile[UI_HOME_TILE_COLS];
    tile_parts_t parts[UI_HOME_TILE_COLS];
    tile_shape_t shape[UI_HOME_TILE_COLS];
    int hdr_count;
    int tiles_n;
    int bays;
    int pages;
    int page;
    int loaded;        /* the loaded bay's column on this page, or -1 */
    bool have_unit;
    bool have_th;
    bool bound;        /* every band was bound: update() may answer at all */
} s_live;

static lv_obj_t *child_at(lv_obj_t *parent, int i)
{
    return (parent && i >= 0 && (uint32_t)i < lv_obj_get_child_count(parent))
               ? lv_obj_get_child(parent, (uint32_t)i) : NULL;
}

/* A plain box, as ui_tile.c's box() and the strip's bar() make them: not a
 * label. */
static bool is_box(lv_obj_t *o)
{
    return o && !lv_obj_check_type(o, &lv_label_class);
}

/* The next child of @p tile, when it is the label the tile's builder would
 * have put there. NULL means the tile is not shaped the way we bound it, and
 * update() then refuses rather than write to the wrong object. */
static lv_obj_t *want_label(lv_obj_t *tile, int *i)
{
    lv_obj_t *o = child_at(tile, (*i)++);

    if(o && lv_obj_check_type(o, &lv_label_class)) return o;
    return NULL;
}

/* The tile's own numbers, from ui_tile.c: they decide where the rows sit, and
 * a tile that changes one of them is a tile the shape below cannot describe. */
#define TILE_PAD_X   UI_PX(10)
#define TILE_PAD_TOP UI_PX(6)
#define TILE_PAD_BOT UI_PX(6)
#define TILE_ROW_GAP UI_PX(2)
#define TILE_RING_S  UI_PX(52)
#define TILE_BADGE_PAD UI_PX(6)
#define TILE_TAG_PAD   UI_PX(7)
#define TILE_CONTENT_W (UI_HOME_TILE_W - 2 * TILE_PAD_X)

/* U+221E, as a byte string so the source stays plain ASCII: the spare badge's
 * mark (ui_tile.c's own spelling of it). */
#define TILE_INF_MARK "\xE2\x88\x9E"

/* The name row's text, exactly as ui_tile.c's build_name_rows() picks it. */
static const char *tile_name_text(const view_tile_t *t)
{
    return t->name[0] ? t->name : t->material;
}

/* The meta row's text, the same way: the brand, then the weight. */
static void tile_meta_text(const view_tile_t *t, char *buf, size_t n)
{
    buf[0] = '\0';
    if(t->brand[0]) snprintf(buf, n, "%s", t->brand);
    if(t->grams_text[0]) {
        size_t k = strlen(buf);

        snprintf(buf + k, n - k, "%s%s", k ? " · " : "", t->grams_text);
    }
}

/* Whether the meta row fits above the ring - the one condition under which
 * ui_tile.c's build_name_rows() draws it at all, and so the one condition that
 * says whether a child is there to bind. */
static bool tile_meta_fits(void)
{
    int y = TILE_PAD_TOP
            + lv_font_get_line_height(UI_HOME_FONT_LABEL) + UI_PX(1)
            + lv_font_get_line_height(UI_HOME_FONT_META) + TILE_ROW_GAP;
    int avail = UI_HOME_TILE_H - TILE_PAD_BOT - TILE_RING_S - UI_PX(2);

    return y + lv_font_get_line_height(UI_HOME_FONT_META) <= avail;
}

/* The tile's objects, in the order ui_tile_bay() creates them, plus the two
 * values that are not objects. False when the tile is not shaped the way that
 * reads it, which is update()'s answer then. */
static bool bind_tile(lv_obj_t *tile, const view_tile_t *t, tile_parts_t *p, tile_shape_t *s)
{
    char meta[VIEW_MATERIAL_LEN * 2 + 4];
    uint32_t n;
    int i = 0;

    if(!tile) return false;
    n = lv_obj_get_child_count(tile);

    memset(p, 0, sizeof *p);
    memset(s, 0, sizeof *s);
    tile_meta_text(t, meta, sizeof meta);
    s->hatch = t->hatched;
    s->glow = (t->state == VIEW_TILE_LOADED);
    s->dim = t->dim;
    s->color = t->color_known;

    if(t->hatched) {
        if(!is_box(child_at(tile, i))) return false;
        i++;
    }
    if(!(p->label = want_label(tile, &i))) return false;
    if(t->spare.present) {
        p->spare = child_at(tile, i++);
        if(!is_box(p->spare) || lv_obj_get_child_count(p->spare) != 1) return false;
    }
    if(t->has_sublabel && !(p->sublabel = want_label(tile, &i))) return false;
    if(tile_name_text(t)[0] && !(p->name = want_label(tile, &i))) return false;
    if(meta[0] && tile_meta_fits() && !(p->meta = want_label(tile, &i))) return false;
    if(t->rfid && !(p->rfid = want_label(tile, &i))) return false;

    p->ring = child_at(tile, i++);
    if(!is_box(p->ring)) return false;
    p->arc = child_at(p->ring, 0);
    if(!p->arc) return false;

    if(t->has_tag) {
        p->tag = child_at(tile, i++);
        if(!is_box(p->tag) || lv_obj_get_child_count(p->tag) != 1) return false;
    }
    else {
        p->pct = child_at(p->arc, 0);
        if(!p->pct) return false;
    }
    /* Anything left over means the tile grew a child we do not know about. */
    return (uint32_t)i == n;
}

/* The ink the tile's own text draws in: the core's choice, made from the
 * filament color - or the muted gray of the disabled look (ui_tile.c's
 * ink_of(), which the two of them have to agree on). */
static lv_color_t tile_ink(const view_tile_t *t)
{
    if(t->dim) return UI_TOK_TEXT_MUTED;
    return t->ink == VIEW_INK_LIGHT ? UI_TOK_TEXT : UI_TOK_BG;
}

/* The tag chip's colors: the web's tag tones (ui_tile.c's tag_bg/tag_fg). */
static lv_color_t tag_bg_of(int tone)
{
    switch(tone) {
    case VIEW_TONE_ERROR: return lv_color_hex(0xB3263A);
    case VIEW_TONE_INFO:  return lv_color_hex(0xF5B942);
    default:              return lv_color_hex(0x3A4048);
    }
}

static lv_color_t tag_fg_of(int tone)
{
    switch(tone) {
    case VIEW_TONE_ERROR: return lv_color_hex(0xFFFFFF);
    case VIEW_TONE_INFO:  return lv_color_hex(0x2A1C00);
    default:              return lv_color_hex(0xE6E9ED);
    }
}

/* Every value of one tile, written onto the objects bind_tile() found. The
 * texts that decide a box's width are the only ones that move anything, and
 * each of those is inside the box the ring or the row it belongs to, so the
 * tiles keep their place either way. */
static void update_tile(lv_obj_t *tile, const tile_parts_t *p, const view_tile_t *t)
{
    lv_color_t on = tile_ink(t);
    lv_color_t ind = (t->pct >= 0 && t->low) ? UI_TOK_WARN
                                              : (t->dim ? UI_TOK_TEXT_MUTED : on);
    bool flat = t->dim || !t->color_known;

    /* the frame: the filament's own two-stop gradient, or the one disabled
     * look; and the accent ring on a loaded tile, red on a fault */
    if(flat) {
        ui_update_bg(tile, UI_TOK_SURFACE_2);
    }
    else {
        ui_update_bg(tile, lv_color_hex(t->gradient_top));
        if(!lv_color_eq(lv_obj_get_style_bg_grad_color(tile, LV_PART_MAIN),
                        lv_color_hex(t->gradient_bottom)))
            lv_obj_set_style_bg_grad_color(tile, lv_color_hex(t->gradient_bottom), LV_PART_MAIN);
    }
    if(t->state == VIEW_TILE_LOADED) {
        ui_update_border(tile, UI_TOK_ACCENT, LV_OPA_COVER);
    }
    else if(t->state == VIEW_TILE_ERROR) {
        ui_update_border(tile, UI_TOK_ERROR, LV_OPA_COVER);
    }
    else {
        /* the pressed state a tappable tile wears, exactly as tile_frame()
         * sets it when the tile is neither loaded nor in fault. A style
         * selector for a state cannot be read back, and LVGL drops a write
         * that changes nothing, so this one is set as it is. */
        ui_update_border(tile, UI_TOK_TEXT_MUTED, LV_OPA_TRANSP);
        lv_obj_set_style_border_color(tile, UI_TOK_TEXT_MUTED, LV_STATE_PRESSED);
    }

    ui_update_text_fit(p->label, t->label, UI_HOME_FONT_LABEL, TILE_CONTENT_W);
    ui_update_text_color(p->label, on);
    ui_update_text_fit(p->sublabel, t->sublabel, UI_HOME_FONT_META, TILE_CONTENT_W);
    if(p->sublabel)
        ui_update_text_color(p->sublabel, t->dim ? UI_TOK_TEXT_FAINT : UI_TOK_TEXT_MUTED);
    ui_update_text_fit(p->name, tile_name_text(t), UI_HOME_FONT_BODY, TILE_CONTENT_W);
    if(p->name) ui_update_text_color(p->name, on);
    if(p->meta) {
        char meta[VIEW_MATERIAL_LEN * 2 + 4];

        tile_meta_text(t, meta, sizeof meta);
        ui_update_text_fit(p->meta, meta, UI_HOME_FONT_META, TILE_CONTENT_W);
        ui_update_text_color(p->meta, on);
    }
    if(p->rfid) ui_update_text_color(p->rfid, t->dim ? UI_TOK_TEXT_FAINT : on);

    if(p->spare) {
        char text[8];
        lv_obj_t *chip_label = child_at(p->spare, 0);

        /* the group's fallback rank, or a bare mark on the loaded member */
        if(t->spare.bare) snprintf(text, sizeof text, "%s", TILE_INF_MARK);
        else              snprintf(text, sizeof text, "%s%d", TILE_INF_MARK, t->spare.position);

        if(chip_label && strcmp(lv_label_get_text(chip_label), text) != 0) {
            int bw = ui_home_text_w(text, UI_HOME_FONT_MONO) + 2 * TILE_BADGE_PAD;
            int bx = TILE_PAD_X + ui_home_text_w(t->label, UI_HOME_FONT_LABEL) + UI_PX(6);

            lv_obj_set_size(p->spare, bw, lv_obj_get_style_height(p->spare, LV_PART_MAIN));
            lv_obj_set_pos(p->spare, bx, TILE_PAD_TOP);
            lv_obj_set_width(chip_label, bw);  /* the label spans the chip, so CENTER centers it */
            ui_update_text(chip_label, text);
        }
        ui_update_text_color(chip_label, t->dim ? UI_TOK_TEXT_MUTED : UI_TOK_TEXT);
    }

    /* the ring: the sweep is what is left, the label is what it says, and the
     * tag chip takes the middle over when the tile has one */
    ui_update_arc(p->arc, t->pct, ind);
    if(!lv_color_eq(lv_obj_get_style_arc_color(p->arc, LV_PART_MAIN),
                    t->dim ? UI_TOK_SURFACE_3 : on))
        lv_obj_set_style_arc_color(p->arc, t->dim ? UI_TOK_SURFACE_3 : on, LV_PART_MAIN);
    if(lv_obj_get_style_arc_opa(p->arc, LV_PART_MAIN) != (t->dim ? LV_OPA_COVER : LV_OPA_70))
        lv_obj_set_style_arc_opa(p->arc, t->dim ? LV_OPA_COVER : LV_OPA_70, LV_PART_MAIN);

    if(p->pct) {
        if(strcmp(lv_label_get_text(p->pct), t->pct_text) != 0) {
            /* the label is centered in the ring, so it moves with its own text */
            lv_obj_set_x(p->pct, (TILE_RING_S - ui_home_text_w(t->pct_text, UI_HOME_FONT_MONO)) / 2);
            ui_update_text(p->pct, t->pct_text);
        }
        ui_update_text_color(p->pct, t->pct >= 0 ? ind : UI_TOK_TEXT_FAINT);
    }
    else if(p->tag) {
        lv_obj_t *tag_label = child_at(p->tag, 0);

        ui_update_bg(p->tag, tag_bg_of(t->tag_tone));
        if(tag_label && strcmp(lv_label_get_text(tag_label), t->tag ? t->tag : "") != 0) {
            int tw = ui_home_text_w(t->tag ? t->tag : "", UI_HOME_FONT_BODY) + 2 * TILE_TAG_PAD;

            if(tw > TILE_CONTENT_W - UI_PX(4)) tw = TILE_CONTENT_W - UI_PX(4);
            lv_obj_set_size(p->tag, tw, lv_obj_get_style_height(p->tag, LV_PART_MAIN));
            lv_obj_set_x(p->tag, (TILE_CONTENT_W - tw) / 2);
            lv_obj_set_width(tag_label, tw);
            ui_update_text(tag_label, t->tag ? t->tag : "");
        }
        if(tag_label) ui_update_text_color(tag_label, tag_fg_of(t->tag_tone));
    }
}

/* ------------------------------------------------------- the other bands */

/* The header's objects. ui_unit_header() draws on the screen it is given, so
 * its children are the screen's first ones, in the order it makes them: the
 * title, the config name, the pill when there is one, the alert slot, the
 * menu. */
static void bind_header(lv_obj_t *scr, int base, header_parts_t *h)
{
    uint32_t n = lv_obj_get_child_count(scr);
    int count = (int)n - base;

    memset(h, 0, sizeof *h);
    if(count < 4) return;
    h->title = child_at(scr, base);
    h->subtitle = child_at(scr, base + 1);
    h->slot = child_at(scr, base + count - 2);
    if(count == 5) h->pill = child_at(scr, base + 2);
}

/* The environment pill's text and tone, as ui_unit_header.c's pill_text()
 * picks them: the reading while the unit is up, its own status while it is
 * not - which is also what says whether there is a pill at all. */
static void pill_of(const view_unit_t *u, const char **text, lv_color_t *color)
{
    if(!u->online) {
        *text = u->status_text[0] ? u->status_text : view_str(VS_UNIT_OFFLINE);
        *color = UI_TOK_ERROR;
        return;
    }
    *text = u->env.present ? u->env.text : "";
    *color = UI_TOK_PILL_TEXT;
}

/* The one line the message row shows: the core's message, or - while a load or
 * an unload runs - the step it is on (ui_toolhead_message()'s own choice). */
static const char *message_text(const view_toolhead_t *th)
{
    const view_activity_t *act = &th->activity;

    if(th->message.tone == VIEW_TONE_NEUTRAL &&
       (act->kind == VIEW_ACTIVITY_LOADING || act->kind == VIEW_ACTIVITY_UNLOADING) &&
       act->index >= 0 && act->index < act->step_count && act->steps[act->index][0])
        return act->steps[act->index];
    return th->message.text;
}

/* The message row's objects: the row holds the box the line sits in and the
 * lane's alert slot; the line box holds the severity icon when the tone is not
 * neutral, then the line itself. */
static void bind_message(lv_obj_t *row, const view_toolhead_t *th, message_parts_t *m)
{
    uint32_t n;

    memset(m, 0, sizeof *m);
    m->msg = child_at(row, 0);
    m->slot = child_at(row, 1);
    if(!m->msg || !m->slot) return;

    n = lv_obj_get_child_count(m->msg);
    m->icon = (th->message.tone != VIEW_TONE_NEUTRAL) ? child_at(m->msg, 0) : NULL;
    m->label = message_text(th)[0] ? child_at(m->msg, m->icon ? 1 : 0) : NULL;
    if((uint32_t)((m->icon != NULL) ? 1 : 0) + (m->label != NULL ? 1 : 0) != n) {
        m->icon = NULL;
        m->label = NULL;
    }
}

/* The path band's objects, in the order build_path() and build_hotend() make
 * them: a stem per bay but the loaded one, the loaded bay's own path in three
 * bars, then the extruder frame with the tool chip on its glyph, then the
 * path's words beside it. The paging chevrons come after, and only when there is a page to move to. */
static void bind_strip(lv_obj_t *strip, int pages, strip_parts_t *p)
{
    uint32_t n = lv_obj_get_child_count(strip);
    int bars = p->stems + 2 * p->runs;
    int i;

    if((int)n != bars + 2 + (pages > 1 ? 2 : 0)) return;
    for(i = 0; i < p->stems; i++) p->stem[i] = lv_obj_get_child(strip, (uint32_t)i);
    for(i = 0; i < p->runs; i++) p->run_outline[i] = lv_obj_get_child(strip, (uint32_t)(p->stems + i));
    for(i = 0; i < p->runs; i++) p->run[i] = lv_obj_get_child(strip, (uint32_t)(p->stems + p->runs + i));
    p->frame = lv_obj_get_child(strip, (uint32_t)bars);
    if(!p->frame) return;
    p->rest = lv_obj_get_child(strip, (uint32_t)(bars + 1));
    p->glyph = child_at(p->frame, 0);
    if(p->glyph) {
        p->chip = child_at(p->glyph, 0);
        if(p->chip) p->chip_label = child_at(p->chip, 0);
    }
}

/* ------------------------------------------------------------ the screen */

/* What the page on screen would hold now, without touching s_page: the tiles
 * bound are those of that page, and the loaded bay's column is the one the
 * path was drawn from. */
static void home_of(int bays, const view_toolhead_t *th, int *tiles_out, int *loaded_out)
{
    int first = s_page * UI_HOME_TILE_COLS;
    int loaded = loaded_bay(th);
    int n = bays - first;

    if(n > UI_HOME_TILE_COLS) n = UI_HOME_TILE_COLS;
    if(n < 0) n = 0;
    *tiles_out = n;
    if(loaded < first || loaded >= first + n) *loaded_out = -1;
    else                                   *loaded_out = loaded - first;
}

/* Whether a tile would still be drawn exactly as it is: the same objects, with
 * the same ones missing and present. A tile that grows or loses a row, a tag
 * chip, a badge or the hatch, or one whose fill would have to change from the
 * filament's gradient to the disabled look, is a rebuild - not an update. */
static bool same_tile_shape(const view_tile_t *t, int i)
{
    char meta[VIEW_MATERIAL_LEN * 2 + 4];
    const tile_parts_t *p = &s_live.parts[i];
    const tile_shape_t *s = &s_live.shape[i];

    tile_meta_text(t, meta, sizeof meta);

    return (t->state == VIEW_TILE_LOADED) == s->glow
        && t->hatched == s->hatch
        && t->dim == s->dim
        && t->color_known == s->color
        && t->spare.present == (p->spare != NULL)
        && t->has_sublabel == (p->sublabel != NULL)
        && (tile_name_text(t)[0] != 0) == (p->name != NULL)
        && (meta[0] != 0 && tile_meta_fits()) == (p->meta != NULL)
        && t->rfid == (p->rfid != NULL)
        && t->has_tag == (p->tag != NULL);
}

/* No unit and no toolhead to draw: the printer's data has not arrived. Say
 * where the link is (Wi-Fi, Moonraker, Klipper, OpenAMS) instead of leaving
 * the screen empty; the link marks the model dirty on every change, so this
 * is redrawn as it moves. */
/* The sentence for where the link is ("Waiting for Moonraker (disconnected)",
 * "Waiting for Klipper (startup)"), or false while it is ready. */
static bool link_status_text(char *note, size_t n)
{
    char detail[128];
    vs_arg_t arg;
    view_str_id_t id = VS_PANEL_NOT_READY;

    note[0] = '\0';
    switch(platform_link_status(detail, sizeof detail)) {
    case PLATFORM_LINK_WIFI:
        id = VS_HOME_LINK_WIFI;
        arg = (vs_arg_t)VS_ARG_T("network", detail);   /* lit-ok: placeholder name */
        break;
    case PLATFORM_LINK_PRINTER:
        id = VS_HOME_LINK_PRINTER;
        arg = (vs_arg_t)VS_ARG_T("url", detail);   /* lit-ok: placeholder name */
        break;
    case PLATFORM_LINK_KLIPPER:
        id = VS_HOME_LINK_KLIPPER;
        arg = (vs_arg_t)VS_ARG_T("state", detail);   /* lit-ok: placeholder name */
        break;
    case PLATFORM_LINK_NO_OPENAMS:
        id = VS_HOME_LINK_NO_OPENAMS;
        arg = (vs_arg_t)VS_ARG_T("unit", detail);   /* lit-ok: placeholder name */
        break;
    case PLATFORM_LINK_NO_UNIT:
        id = VS_HOME_LINK_NO_UNIT;
        arg = (vs_arg_t)VS_ARG_T("unit", detail);   /* lit-ok: placeholder name */
        break;
    case PLATFORM_LINK_READY:
        return false;
    }
    vs_format(id, note, n, &arg, 1);
    return true;
}

/* While the link is not ready, the message row says where it is, over the
 * last known (offline) units: the same sentence as the empty screen's. */
static void link_note(view_toolhead_t *th)
{
    char note[VIEW_MESSAGE_TEXT_LEN];

    if(!link_status_text(note, sizeof note)) return;
    snprintf(th->message.text, sizeof th->message.text, "%s", note);
    th->message.tone = VIEW_TONE_INFO;
}

/* A centred text line of the waiting state: wraps inside @p col's width. */
static void link_status_line(lv_obj_t *col, const char *text, const lv_font_t *font,
                             lv_color_t color)
{
    lv_obj_t *l = ui_home_label(col, text, font, color, 0, 0);

    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
}

static void build_link_status(lv_obj_t *scr)
{
    char note[192];
    lv_obj_t *col;
    lv_obj_t *badge;
    lv_obj_t *icon;

    if(!link_status_text(note, sizeof note)) note[0] = '\0';

    /* The menu keeps its place from the unit header, so Settings and the
     * other screens stay reachable while there is no printer to show. */
    (void)ui_unit_header_menu(scr, on_menu, NULL);

    /* The empty state's shape, centred on the screen: an icon on a round
     * badge, what the display is waiting for, and where the link is. */
    col = lv_obj_create(scr);
    lv_obj_remove_style_all(col);
    lv_obj_set_clickable(col, false);
    lv_obj_set_scrollable(col, false);
    lv_obj_set_size(col, UI_HOME_W - 2 * UI_PX(32), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, UI_PX(8), 0);
    lv_obj_center(col);

    badge = lv_obj_create(col);
    lv_obj_remove_style_all(badge);
    lv_obj_set_clickable(badge, false);
    lv_obj_set_scrollable(badge, false);
    lv_obj_set_size(badge, UI_PX(64), UI_PX(64));
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(badge, UI_TOK_SURFACE_1, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_margin_bottom(badge, UI_PX(6), 0);
    icon = ui_home_label(badge, UI_ICON_SYNC, UI_FONT_ICON_L, UI_TOK_TEXT_MUTED, 0, 0);
    lv_obj_center(icon);

    link_status_line(col, view_str(VS_PANEL_NOT_READY), UI_HOME_FONT_TITLE, UI_TOK_TEXT);
    if(note[0]) link_status_line(col, note, UI_HOME_FONT_BODY, UI_TOK_TEXT_MUTED);
}

static lv_obj_t *build(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    view_unit_t u;
    view_toolhead_t th;
    int loaded = -1;
    int tiles = 0;
    int page = 0;
    int pages = 0;
    int base = 0;
    int i;
    bool have_th;

    lv_obj_add_style(scr, ui_style(UI_STYLE_SCREEN), 0);
    /* The restyled home draws the dark token background, and only here:
     * ui_theme.c is shared. */
    lv_obj_set_style_bg_color(scr, UI_TOK_BG, 0);

    ui_nav_set_alert_count(mmu_alert_unread_visible());

    memset(&th, 0, sizeof th);
    memset(&s_live, 0, sizeof s_live);
    have_th = view_toolhead(&g_mmu, home_toolhead(), &th);
    if(have_th) link_note(&th);
    if(view_unit(&g_mmu, home_unit(), &u)) {
        base = (int)lv_obj_get_child_count(scr);
        ui_unit_header(scr, &u, on_environment, NULL, on_alerts, NULL, on_menu, NULL);
        bind_header(scr, base, &s_live.hdr);
        s_live.hdr_count = (int)lv_obj_get_child_count(scr) - base;
        loaded = build_tiles(scr, &u, &th, &page, &pages, &tiles);
        s_live.have_unit = true;
        s_live.bays = u.bay_count;
    }
    if(have_th) {
        lv_obj_t *row;
        lv_obj_t *strip;

        row = ui_toolhead_message(scr, &th, on_message, NULL, on_alerts, NULL);
        bind_message(row, &th, &s_live.msg);
        /* One stem per tile on screen, not per bay: a page shows four. */
        strip = ui_toolhead_strip(scr, &th, tiles, loaded, pages);
        s_live.strip.stems = tiles - (loaded >= 0 ? 1 : 0);
        s_live.strip.runs = (loaded >= 0 && loaded < tiles) ? 3 : 0;
        bind_strip(strip, pages, &s_live.strip);
        /* The chevrons go inside the strip, so they can never cover the
         * filament path or the extruder, and only when there is a page to
         * move to: a unit of four bays or fewer is untouched. */
        ui_chevron_pair(strip, page, pages, on_chevron);
        s_live.have_th = true;
    }
    if(!s_live.have_unit && !have_th) build_link_status(scr);

    /* The tiles, in the order build_tiles() made them, so the header's last
     * child is where the first one starts. A loaded tile brings its accent ring
     * with it, as a sibling just before it. */
    s_live.bound = true;
    {
        int at = base + s_live.hdr_count;

        for(i = 0; i < tiles; i++) {
            view_tile_t t;
            lv_obj_t *tile;

            if(!view_tile(&g_mmu, home_unit(), page * UI_HOME_TILE_COLS + i, &t)) {
                s_live.bound = false;
                break;
            }
            if(t.state == VIEW_TILE_LOADED) at++;
            tile = child_at(scr, at++);
            if(!tile || !bind_tile(tile, &t, &s_live.parts[i], &s_live.shape[i])) {
                s_live.bound = false;
                break;
            }
            s_live.tile[i] = tile;
        }
    }

    s_live.scr = scr;
    s_live.tiles_n = tiles;
    s_live.pages = pages;
    s_live.page = page;
    s_live.loaded = loaded;
    s_live.bound = s_live.bound
                   && s_live.have_unit && s_live.have_th
                   && s_live.hdr.title != NULL && s_live.hdr.slot != NULL
                   && s_live.msg.msg != NULL && s_live.msg.slot != NULL
                   && s_live.strip.frame != NULL && s_live.strip.glyph != NULL
                   && s_live.strip.rest != NULL
                   && s_live.hdr_count >= 4;
    return scr;
}

/* Bring the screen up to date with the model without rebuilding it: every
 * band's own objects, set only where a value really changed. False - having
 * written nothing - when the screen's shape would differ from a fresh build,
 * which is the caller's cue to rebuild as before. */
static bool update(void)
{
    view_unit_t u;
    view_toolhead_t th;
    view_tile_t tiles[UI_HOME_TILE_COLS];
    const char *text;
    lv_color_t color;
    int tiles_n = 0;
    int loaded = -1;
    int i;

    if(!s_live.bound || !s_live.scr) return false;
    if(!view_toolhead(&g_mmu, home_toolhead(), &th)) return false;
    link_note(&th);
    if(!view_unit(&g_mmu, home_unit(), &u)) return false;

    /* --- what would have to be built again, decided before anything is written */

    if(u.bay_count != s_live.bays) return false;            /* another number of tiles */
    if(home_pages(u.bay_count) != s_live.pages) return false;
    if(s_page != s_live.page) return false;

    /* the header's reading and its alert slot come and go */
    pill_of(&u, &text, &color);
    if((text[0] != 0) != (s_live.hdr.pill != NULL)) return false;
    if(ui_update_alert_slot_objects(u.alert.present ? 1 : 0, u.alert.clear) !=
       (int)lv_obj_get_child_count(s_live.hdr.slot)) return false;

    /* so does the message row's severity icon, and the line itself */
    if((th.message.tone != VIEW_TONE_NEUTRAL) != (s_live.msg.icon != NULL)) return false;
    if((message_text(&th)[0] != 0) != (s_live.msg.label != NULL)) return false;
    if(ui_update_alert_slot_objects(th.alert.present ? 1 : 0, th.alert.clear) !=
       (int)lv_obj_get_child_count(s_live.msg.slot)) return false;

    for(i = 0; i < s_live.tiles_n; i++) {
        if(!view_tile(&g_mmu, home_unit(), s_live.page * UI_HOME_TILE_COLS + i, &tiles[i]))
            return false;
        if(!same_tile_shape(&tiles[i], i)) return false;
    }

    home_of(u.bay_count, &th, &tiles_n, &loaded);
    if(tiles_n != s_live.tiles_n || loaded != s_live.loaded) return false;
    if(th.tool.present != (s_live.strip.chip != NULL)) return false;  /* the chip on the icon */

    /* --- from here on the screen on screen is only being written to */

    /* the header: the family, the config name, the reading, the alert */
    ui_update_text(s_live.hdr.title, u.title);
    ui_update_text_color(s_live.hdr.title, u.online ? UI_TOK_TEXT : UI_TOK_TEXT_MUTED);
    ui_update_text(s_live.hdr.subtitle, u.subtitle);
    if(s_live.hdr.pill) {
        lv_obj_t *pill_label = child_at(s_live.hdr.pill, 0);
        const char *have = pill_label ? lv_label_get_text(pill_label) : "";

        /* the pill is as wide as its reading and ends before the alert slot,
         * so a different reading moves it as well as re-letters it */
        if(pill_label && strcmp(have, text) != 0) {
            int d = ui_home_text_w(text, UI_HOME_FONT_META)
                    - ui_home_text_w(have, UI_HOME_FONT_META);

            lv_obj_set_width(s_live.hdr.pill,
                             lv_obj_get_style_width(s_live.hdr.pill, LV_PART_MAIN) + d);
            lv_obj_set_x(s_live.hdr.pill,
                         lv_obj_get_style_x(s_live.hdr.pill, LV_PART_MAIN) - d);
            ui_update_text(pill_label, text);
        }
        if(pill_label) ui_update_text_color(pill_label, color);
    }
    if(u.alert.present) ui_update_alert_mark(s_live.hdr.slot, u.alert.tone, u.alert.count);

    /* the one line, in its tone's color */
    if(s_live.msg.icon) {
        ui_update_text(s_live.msg.icon,
                       th.message.tone == VIEW_TONE_ERROR ? UI_HOME_ICON_ERROR : UI_HOME_ICON_INFO);
        ui_update_text_color(s_live.msg.icon, ui_home_tone_color(th.message.tone));
    }
    if(s_live.msg.label) {
        int slot_x = UI_HOME_INNER_W - UI_HOME_SLOT_W;

        ui_update_text_fit(s_live.msg.label, message_text(&th), UI_HOME_FONT_BODY,
                           slot_x - UI_PX(4) - (s_live.msg.icon ? UI_PX(22) : 0));
        ui_update_text_color(s_live.msg.label, ui_home_tone_color(th.message.tone));
    }
    if(th.alert.present) ui_update_alert_mark(s_live.msg.slot, th.alert.tone, th.alert.count);

    /* the tiles */
    for(i = 0; i < s_live.tiles_n; i++)
        update_tile(s_live.tile[i], &s_live.parts[i], &tiles[i]);

    /* the path: the loaded filament's own color, from the tile to the extruder */
    ui_update_text_fit(s_live.strip.rest, th.activity.rest.present ? th.activity.rest.label : "",
                       UI_HOME_FONT_META, ui_toolhead_rest_w(s_live.pages));
    {
        lv_color_t col = (th.tool.present && th.tool.color_known) ? lv_color_hex(th.tool.color)
                                                                  : UI_TOK_TEXT_FAINT;

        lv_opa_t ol = (th.tool.present && th.tool.color_known && th.tool.outline) ? LV_OPA_COVER
                                                                                   : LV_OPA_TRANSP;

        for(i = 0; i < s_live.strip.runs; i++) {
            ui_update_bg(s_live.strip.run[i], col);
            if(lv_obj_get_style_bg_opa(s_live.strip.run_outline[i], LV_PART_MAIN) != ol)
                lv_obj_set_style_bg_opa(s_live.strip.run_outline[i], ol, LV_PART_MAIN);
        }
        if(lv_obj_get_style_outline_opa(s_live.strip.frame, LV_PART_MAIN) != ol)
            lv_obj_set_style_outline_opa(s_live.strip.frame, ol, LV_PART_MAIN);
        ui_update_border(s_live.strip.frame, col, LV_OPA_COVER);
        ui_update_text_color(s_live.strip.glyph,
                             th.tool.present ? UI_TOK_TEXT : UI_TOK_TEXT_FAINT);
        if(s_live.strip.chip) {
            ui_update_bg(s_live.strip.chip, col);
            if(lv_obj_get_style_outline_opa(s_live.strip.chip, LV_PART_MAIN) != ol)
                lv_obj_set_style_outline_opa(s_live.strip.chip, ol, LV_PART_MAIN);
            ui_update_text(s_live.strip.chip_label, th.tool.label);
            ui_update_text_color(s_live.strip.chip_label,
                                 th.tool.ink == VIEW_INK_LIGHT ? UI_TOK_BG : UI_TOK_TEXT);
        }
    }

    ui_nav_set_alert_count(mmu_alert_unread_visible());
    return true;
}

const ui_screen_ops_t screen_home_ops = { build, modal_count, open_modal, update };