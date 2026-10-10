/**
 * @file screen_alerts.c
 * The alert list, in the design language (ui_kit), built from the view: the
 * home unit's alerts, its toolhead's, and the panel's, each with its severity
 * icon, title, text, code, unit and its actions (a fault's retry, resume or
 * clear). Every alert sits on exactly one level in the view, so it appears
 * once here (principle 4). Below them, "Earlier" lists the history entries the
 * levels above do not already carry.
 *
 * An action's line goes to the host verbatim (actions_log); on the demo model
 * a fault action also clears the demo error, as the older screen did.
 */
#include "view/strings.h"
#include "ui/ui_screens.h"

#include "app/actions.h"
#include "model/mmu_model.h"
#include "ui/ui_icons.h"
#include "ui/ui_kit.h"
#include "ui/ui_nav.h"
#include "view/view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ACTIONS 24

/* The action lines of the cards on screen, so a button's user data is just an
 * index (the view's action structs are gone once the build returns). */
typedef struct {
    char line[VIEW_ACTION_LINE_LEN];
} act_line_t;
static act_line_t *s_act;                    /* heap, for the life of the screen */
static int s_act_count;

/* The texts already listed, so "Earlier" does not repeat them. */
#define MAX_SEEN 24
#define SEEN_LEN VIEW_ALERT_TEXT_LEN
static char (*s_seen)[SEEN_LEN];   /* heap, for the length of one build */
static int s_seen_count;

static void on_action(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);

    if(!s_act || i < 0 || i >= s_act_count) return;
    actions_log("%s", s_act[i].line);
    /* The demo has no host to clear the fault; a live host's next status does. */
    if(!mmu_model_is_live()) mmu_model_clear_error();
    ui_nav_refresh_async();
}

static void on_clear_resolved(lv_event_t *e)
{
    (void)e;
    actions_log("clear resolved alerts");
    mmu_model_clear_resolved_alerts();
    ui_nav_refresh_async();
}

/** True while at least one visible notice is informational: the kind
 * "Clear resolved" drops. */
static bool has_resolved(void)
{
    int i;

    for(i = 0; i < (int)g_mmu.alert_count; i++)
        if(mmu_alert_visible(i) && g_mmu.alerts[i].severity == MMU_SEV_INFO)
            return true;
    return false;
}

static lv_color_t sev_color(mmu_severity_t sev)
{
    return sev >= MMU_SEV_PAUSE ? UI_TOK_ERROR : UI_TOK_WARN;
}

/* One alert as a card: icon and title, the text, the code and where, then its
 * actions. @p detail false is an "Earlier" entry: no actions, a quieter card. */
static void add_card(lv_obj_t *col, const view_alert_t *a, bool current)
{
    lv_obj_t *row = ui_kit_row(col, 0, NULL, NULL);
    int inner = UI_HOME_INNER_W - 24;
    lv_color_t tone = sev_color(a->severity);
    lv_obj_t *head;
    lv_obj_t *l;
    char meta[VIEW_ALERT_CODE_LEN + VIEW_ALERT_UNIT_LEN + VIEW_ALERT_SLOT_LEN + 12];
    int i;

    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(row, 6, 0);
    if(!current) lv_obj_set_style_bg_color(row, UI_TOK_SURFACE_3, 0);

    /* the severity icon (shape carries the level as well as the color), then the title */
    head = lv_obj_create(row);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, inner, 18);
    lv_obj_set_scrollable(head, false);
    ui_home_label(head, a->severity >= MMU_SEV_PAUSE ? UI_HOME_ICON_ERROR : UI_HOME_ICON_INFO,
                  UI_HOME_FONT_SEV, current ? tone : UI_TOK_TEXT_FAINT, 0, 1);
    ui_home_label_fit(head, a->title ? a->title : "", UI_HOME_FONT_SECTION,
                      current ? UI_TOK_TEXT : UI_TOK_TEXT_MUTED, 24, -1, inner - 24);

    l = ui_kit_text(row, a->text, UI_HOME_FONT_BODY, current ? UI_TOK_TEXT : UI_TOK_TEXT_MUTED,
                    0, 0, inner);
    (void)l;

    meta[0] = '\0';
    if(a->code[0]) snprintf(meta, sizeof meta, "%s", a->code);
    if(a->has_unit && a->unit[0]) {
        size_t n = strlen(meta);

        snprintf(meta + n, sizeof meta - n, "%s%s%s%s", n ? " \xC2\xB7 " : "", a->unit,
                 a->slot_id[0] ? " " : "", a->slot_id);
    }
    if(meta[0]) ui_kit_text(row, meta, UI_HOME_FONT_META, UI_TOK_TEXT_FAINT, 0, 0, inner);

    if(current && a->action_count > 0) {
        int n = a->action_count;
        int gap = 8;
        int w = (inner - gap * (n - 1)) / n;
        lv_obj_t *bar = lv_obj_create(row);

        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, inner, 36);
        lv_obj_set_scrollable(bar, false);
        for(i = 0; i < n; i++) {
            const view_action_t *ac = &a->actions[i];
            int idx = -1;

            if(s_act && s_act_count < MAX_ACTIONS) {
                idx = s_act_count++;
                snprintf(s_act[idx].line, sizeof s_act[idx].line, "%s", ac->line);
            }
            ui_kit_button(bar, ac->label, i == 0 ? UI_KIT_BTN_PRIMARY : UI_KIT_BTN_NORMAL,
                          i * (w + gap), 0, w, 36, ac->enabled && idx >= 0, on_action,
                          (void *)(intptr_t)idx);
        }
        for(i = 0; i < n; i++)
            if(!a->actions[i].enabled && a->actions[i].reason[0])
                ui_kit_text(row, a->actions[i].reason, UI_HOME_FONT_META, UI_TOK_TEXT_FAINT,
                            0, 0, inner);
    }
    if(s_seen && s_seen_count < MAX_SEEN) snprintf(s_seen[s_seen_count++], sizeof s_seen[0], "%s", a->text);
}

static bool seen(const char *text)
{
    int i;

    for(i = 0; s_seen && i < s_seen_count; i++)
        if(strcmp(s_seen[i], text) == 0) return true;
    return false;
}

/* The errors the tiles wear: a bay in error is on its tile (tag), and its
 * actions are the tag's, so the list opens them here too - the tag's detail,
 * its code, and the retry or resume the host offers for it. */
static int add_tile_faults(lv_obj_t *col, int unit)
{
    view_tile_t *t = malloc(sizeof *t);
    view_actions_t *acts = malloc(sizeof *acts);
    view_alert_t *a = malloc(sizeof *a);
    int shown = 0;
    int bay;
    int n = (unit >= 0 && unit < (int)g_mmu.unit_count) ? (int)g_mmu.units[unit].slot_count : 0;

    if(t && acts && a) {
        for(bay = 0; bay < n; bay++) {
            int k;

            if(!view_tile(&g_mmu, unit, bay, t) || !t->has_tag || t->tag_tone != VIEW_TONE_ERROR)
                continue;
            memset(a, 0, sizeof *a);
            a->severity = MMU_SEV_PAUSE;
            a->title = t->tag;
            a->text = t->tag_detail;
            snprintf(a->code, sizeof a->code, "%s", t->tag_code);
            a->has_unit = true;
            snprintf(a->unit, sizeof a->unit, "%s", mmu_unit_name(unit));
            snprintf(a->slot_id, sizeof a->slot_id, "%s", t->sublabel);
            if(view_tile_actions(&g_mmu, unit, bay, acts)) {
                for(k = 0; k < (int)acts->count && a->action_count < VIEW_ACTIONS_PER_ALERT; k++)
                    if(acts->items[k].target == VIEW_TARGET_TAG)
                        a->actions[a->action_count++] = acts->items[k];
            }
            add_card(col, a, true);
            shown++;
        }
    }
    free(t);
    free(acts);
    free(a);
    return shown;
}

/* Each screen owns its table (its user data): the new screen is built before
 * the old one is deleted, so the delete must free the old table, not s_act. */
static void on_delete(lv_event_t *e)
{
    void *own = lv_obj_get_user_data(lv_event_get_target_obj(e));

    if(own == s_act) {
        s_act = NULL;
        s_act_count = 0;
    }
    free(own);
}

static lv_obj_t *build(void)
{
    lv_obj_t *scr = ui_kit_screen();
    lv_obj_t *col;
    view_panel_t *panel = malloc(sizeof *panel);
    view_alerts_t *g = malloc(sizeof *g);
    view_alert_t *one = malloc(sizeof *one);
    int unit = mmu_this_unit();
    int th = (unit >= 0 && unit < (int)g_mmu.unit_count) ? g_mmu.units[unit].toolhead : -1;
    int shown = 0;
    int i;
    bool earlier = false;

    s_act = malloc(MAX_ACTIONS * sizeof *s_act);
    lv_obj_set_user_data(scr, s_act);
    s_act_count = 0;
    s_seen = malloc(MAX_SEEN * sizeof *s_seen);
    s_seen_count = 0;
    lv_obj_add_event_cb(scr, on_delete, LV_EVENT_DELETE, NULL);

    /* Opening the list means the user has seen the notices this display can
     * show; system-wide ones keep their unread flag. */
    mmu_alerts_mark_read();
    ui_nav_set_alert_count(mmu_alert_unread_visible());

    if(panel) view_panel(&g_mmu, panel);
    (void)ui_kit_header(scr, panel ? panel->labels.alerts : view_str(VS_LABEL_ALERTS), false);
    if(has_resolved()) ui_kit_header_action(scr, view_str(VS_ALERTS_CLEAR), on_clear_resolved, NULL);

    col = ui_kit_column(scr, UI_HOME_HEADER_H + 2, UI_HOME_H - UI_HOME_HEADER_H - 2);

    if(g && one && panel) {
        /* the errors on the tiles, the message row's own text when nothing above
         * says it, then the unit's alerts, its toolhead's, and the panel's */
        shown += add_tile_faults(col, unit);
        {
            view_toolhead_t *vt = malloc(sizeof *vt);

            if(vt && th >= 0 && view_toolhead(&g_mmu, th, vt) && vt->message.tone != VIEW_TONE_NEUTRAL &&
               !seen(vt->message.text)) {
                memset(one, 0, sizeof *one);
                one->severity = vt->message.tone == VIEW_TONE_ERROR ? MMU_SEV_PAUSE : MMU_SEV_INFO;
                one->title = vt->message.tone == VIEW_TONE_ERROR ? panel->labels.fault : NULL;
                one->text = vt->message.text;
                add_card(col, one, true);
                shown++;
            }
            free(vt);
        }
        if(view_unit_alerts(&g_mmu, unit, g)) {
            for(i = 0; i < g->count; i++) { add_card(col, &g->items[i], true); shown++; }
        }
        if(th >= 0 && view_toolhead_alerts(&g_mmu, th, g)) {
            for(i = 0; i < g->count; i++) { add_card(col, &g->items[i], true); shown++; }
        }
        for(i = 0; i < (int)panel->alert_count; i++) {
            if(!view_alert(&g_mmu, panel->alert_idx[i], one)) continue;
            add_card(col, one, true);
            shown++;
        }

        for(i = (int)g_mmu.alert_count - 1; i >= 0; i--) {
            if(!mmu_alert_visible(i) || !view_alert(&g_mmu, i, one)) continue;
            if(seen(one->text)) continue;
            if(!earlier) {
                ui_kit_section(col, view_str(VS_ALERTS_EARLIER));
                earlier = true;
            }
            add_card(col, one, false);
            shown++;
        }
    }

    if(shown == 0) {
        lv_obj_t *r = ui_kit_row(col, 60, NULL, NULL);

        ui_home_label(r, UI_ICON_CHECK_CIRCLE, UI_HOME_FONT_ICON, UI_TOK_OK, 0, 6);
        ui_home_label(r, panel ? panel->labels.no_alerts : view_str(VS_LABEL_NO_ALERTS), UI_HOME_FONT_SECTION,
                      UI_TOK_TEXT, 28, 6);
    }

    free(panel);
    free(g);
    free(one);
    free(s_seen);
    s_seen = NULL;
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

const ui_screen_ops_t screen_alerts_ops = { build, modal_count, open_modal, NULL };
