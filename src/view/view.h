/**
 * @file view.h
 * The view builders (docs/design/UNIFIED_UI.md §4): the one place that turns
 * the model into content. Both renderers - LVGL today, the Lit web component
 * later through view_json() - draw only what these fill; a renderer never
 * reads mmu_model_t fields directly and never composes text of its own
 * (§2's rule).
 *
 * Every builder fills a caller-provided, fixed-size struct and never
 * allocates: LVGL calls them per widget, into stack buffers (§4). Every text
 * field is final and already cut to fit by the core; every string the
 * builders emit is looked up or composed through src/view/strings.h, never
 * a literal in view.c.
 *
 * A field that holds a text the core does not own - a fixed string from the
 * table, or the host's own words - is a `const char *`, not a copy: a
 * translation can be any length, and copying it into a sized buffer is what
 * used to cut it. Only a field that *composes* text from a template keeps a
 * buffer, and that buffer is sized for the longest composition the builders
 * can make in the pseudo-locale (the language i18n-b's translations are
 * budgeted against), so nothing is cut in any language - tests/test_view.c
 * walks every fixture and scenario in every language to prove it.
 *
 * The three per-widget structs do not grow when a language is longer than
 * English: the space the composed buffers need comes from fields that hold
 * text the core does not own, which became pointers.
 *
 * Pure C: no LVGL, no platform headers.
 */

#ifndef VIEW_H
#define VIEW_H

#include "model/mmu_model.h"

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------- text sizes */

/* Every buffer below holds composed text only, and each is the longest
 * composition the builders can make in the pseudo-locale, plus the NUL:
 * tools/measure_strings.mjs prints the budget for each template from
 * design/strings/<lang>.json, and test_view's qps run is what holds the
 * numbers honest. A fixed string the core does not own is a pointer, so the
 * only lengths left to get right are these. */

#define VIEW_ID_LEN             MMU_SLOT_ID_LEN   /* a slot id ("oams18") or toolhead id */
#define VIEW_LABEL_LEN          16   /* a tool/group name or a slot id */
#define VIEW_MATERIAL_LEN       24
#define VIEW_GRAMS_LEN          20   /* "{g} g": NN g */
#define VIEW_PCT_LEN            16   /* "{pct}%": NN% */

#define VIEW_TITLE_LEN          32   /* a toolhead's title: "Toolhead" in qps is 27 bytes */
#define VIEW_SUBLABEL_LEN       32   /* "Spool {n}" - a tile's sublabel, which used to share
                                      * VIEW_LABEL_LEN with the tool and slot-id names and
                                      * so had nowhere to grow */
#define VIEW_MESSAGE_TEXT_LEN  176   /* "{tool} ran out; switching to {spool}", {spool} itself a
                                      * qps "{tool} ({unit} Spool {n})" - the longest
                                      * composition in the view, composed twice, and the
                                      * host's own fault text, which the model cuts to 95 */
#define VIEW_STEP_LABEL_LEN     32
#define VIEW_STEP_DETAIL_LEN   128   /* "{step} · {detail}" (STEP_DETAIL): the running
                                      * step's own text with its detail beside it, which
                                      * is the longest real-language step label (66
                                      * bytes) plus the separator and a 31-byte detail */
#define VIEW_REST_PATH_LEN     128   /* REST_PATH: the pseudo-locale's template is 66
                                      * bytes, plus a group name (8), a bay label
                                      * (VIEW_SUBLABEL_LEN, 25 in qps) and a material
                                      * (MMU_TEXT_LEN) */
#define VIEW_PRESSURE_TEXT_LEN  12
#define VIEW_SCALE_LEN           8
#define VIEW_SCALE_COUNT         3

#define VIEW_UNIT_ID_LEN         MMU_UNIT_NAME_LEN
#define VIEW_ENV_TEXT_LEN       48   /* "{rh}% RH | {temp}" DEG "C" */
#define VIEW_DRYER_TEXT_LEN     56   /* "{temp}" DEG "C \xc2\xb7 {hours}h left" */
#define VIEW_SERIAL_LEN         18
#define VIEW_FW_LEN             12

#define VIEW_ALERT_CODE_LEN     40
#define VIEW_ALERT_TEXT_LEN    144   /* a composed alert: the pseudo-locale's low-filament sentence is 140 bytes */
#define VIEW_ALERT_UNIT_LEN      8
#define VIEW_ALERT_SLOT_LEN     32
#define VIEW_WHEN_LEN           16

#define VIEW_SETTING_KEY_LEN    32

#define VIEW_ACTION_ID_LEN      20
#define VIEW_ACTION_LINE_LEN   168   /* "spool edit {slot} material={material} color={color} vendor={vendor} remaining={remaining} initial={initial} pa={pa} then load" is 125, and a slot id is up to VIEW_ID_LEN - 1 */
#define VIEW_FIELD_ID_LEN       12
#define VIEW_OPTION_LABEL_LEN   72   /* "{vendor} {material} \xc2\xb7 {g} g" in the pseudo-locale */

/* ------------------------------------------------------------ array caps */

#define VIEW_ACTIONS_PER_TILE    10   /* load, unload, edit, assign, unlink, change group, reread, calibrate, and a fault's two */
#define VIEW_ACTIONS_PER_UNIT     6
#define VIEW_ACTIONS_PER_TOOLHEAD 6
#define VIEW_ACTIONS_PER_ALERT    2
#define VIEW_ALERTS_PER_GROUP     6   /* a unit: its lane fault, the dryer fault, the adapter, low filament */
#define VIEW_ACTIONS_MAX          10  /* view_actions_t's own capacity: >= every per-widget cap above */
#define VIEW_MAX_FORM_FIELDS      6   /* one form's own fields: the spool editor's */
#define VIEW_MAX_FIELDS          15   /* every form's fields of one view_actions_t, pooled: the spool editor's six, twice over on a bay that offers it twice (an unidentified one: "Load" asks for the spool first), the assign form's one, the group form's one and the confirmation's one */
#define VIEW_MAX_OPTIONS        160   /* every select's choices of one view_actions_t, pooled: two editors' materials (11 each), colors (20 each) and vendors (MMU_MAX_VENDORS + 2), the spool list, or the group list */
#define VIEW_MAX_ALERTS           MMU_MAX_ALERTS
#define VIEW_MAX_SETTINGS         6   /* the host's five, plus the language row */
#define VIEW_MAX_LANGS           12   /* the language row's choices: English, the ten
                                         * translations of UNIFIED_UI.md 12 and the
                                         * pseudo-locale that holds them to their budget */
#define VIEW_LANGUAGE_SETTING_ROW (VIEW_MAX_SETTINGS - 1)
#define VIEW_MAX_PENDING_SPOOLS   8
#define VIEW_MAX_UNASSIGNED       MMU_MAX_UNITS
#define VIEW_LIST_MAX            24   /* one list sheet's rows: every bay's load/unload, edit,
                                         * re-read and Spoolman rows, the toolhead's own, the
                                         * spool-list refresh and the host's six settings */
#define VIEW_GROUP_MEMBERS_PER_GROUP MMU_MAX_GROUP_MEMBERS
#define VIEW_UNGROUPED_BAYS       MMU_MAX_SLOTS_PER_UNIT

/* -------------------------------------------------------------------- enums */

typedef enum {
    VIEW_TILE_LOADED, VIEW_TILE_READY, VIEW_TILE_EMPTY, VIEW_TILE_POSITIONING,
    VIEW_TILE_LOADING, VIEW_TILE_UNLOADING, VIEW_TILE_ERROR, VIEW_TILE_RUNOUT, VIEW_TILE_UNKNOWN,
} view_tile_state_t;

/* What a group member's state word says: the word itself is in the member's
 * state, this is what it means, so a renderer can draw it its own way. */
typedef enum {
    VIEW_MEMBER_STATE_NONE, VIEW_MEMBER_STATE_LOADED, VIEW_MEMBER_STATE_READY,
    VIEW_MEMBER_STATE_EMPTY, VIEW_MEMBER_STATE_ERROR,
} view_member_state_t;

typedef enum { VIEW_INK_LIGHT, VIEW_INK_DARK } view_ink_t;

typedef enum {
    VIEW_ACTIVITY_IDLE, VIEW_ACTIVITY_LOADING, VIEW_ACTIVITY_UNLOADING,
    VIEW_ACTIVITY_RUNOUT, VIEW_ACTIVITY_ERROR,
} view_activity_kind_t;

/* What a click on the tile's parts does (UNIFIED_UI 4b): the tile body loads
 * or unloads, the ring edits the spool, the tag opens its details. */
typedef enum { VIEW_TARGET_TILE, VIEW_TARGET_RING, VIEW_TARGET_TAG } view_target_t;

typedef enum {
    VIEW_DRYER_HEAT, VIEW_DRYER_HOLD, VIEW_DRYER_COOL, VIEW_DRYER_FAULT, VIEW_DRYER_OFF,
} view_dryer_tone_t;

/* One tone scale for a toolhead's message and a tile's tag (UNIFIED_UI 4a):
 * red, yellow, plain gray. */
typedef enum { VIEW_TONE_ERROR, VIEW_TONE_INFO, VIEW_TONE_NEUTRAL } view_tone_t;

typedef enum {
    VIEW_STYLE_PRIMARY, VIEW_STYLE_NORMAL, VIEW_STYLE_DANGER,
} view_action_style_t;

typedef enum {
    VIEW_FIELD_NUMBER, VIEW_FIELD_SELECT, VIEW_FIELD_TOGGLE,
} view_field_kind_t;

/* ------------------------------------------------------------------ group */

/* One bay of a group, as the groups list shows it. A bay of the unit the list
 * is scoped to is named the way its own tile is ("Spool N") and carries its
 * color, material and state; a bay of another unit is a backup this unit may
 * fall back to, never something to manage from here, so it is named with its
 * unit and dimmed, and still carries its color, material and state, which say
 * whether the fallback would match and is ready (docs/GROUPS.md). */
typedef struct {
    char slot_id[VIEW_ID_LEN];                 /* "oams21", so a renderer can name the bay */
    int bay;                                   /* the bay's index in its own unit */
    bool mine;                                 /* a bay of the unit the list is scoped to */
    bool dim;                                  /* another unit's: name it and dim it */
    bool spare;                                /* the one infinite spooling would load next */
    char label[VIEW_SUBLABEL_LEN];             /* "Spool N", or "oams2 Spool N" */
    bool color_known;                          /* false: color is null, no filament color to show */
    uint32_t color;
    char material[VIEW_MATERIAL_LEN];
    char brand[VIEW_MATERIAL_LEN];
    const char *state;                         /* Loaded / Ready / Empty / Error */
    view_member_state_t state_kind;            /* what that word says, to draw it */
    view_tone_t state_tone;
} view_group_member_t;

typedef struct {
    char name[VIEW_LABEL_LEN];                 /* "T0": the tool the slicer loads by name */
    int index;                                 /* the group's index in mmu_model_t.groups, for
                                                * view_group_actions() */
    bool editable;                             /* its membership may change now */
    const char *reason;                        /* "" when editable, else why not */
    uint8_t member_count;
    view_group_member_t members[VIEW_GROUP_MEMBERS_PER_GROUP];
} view_group_t;

/** This unit's bays that belong to no group, as the same rows. */
typedef struct {
    uint8_t count;
    view_group_member_t bays[VIEW_UNGROUPED_BAYS];
} view_ungrouped_t;

/* -------------------------------------------------------------- sub-shapes */

/* One choice of a select field: @p value goes into the field's placeholder in
 * the action line, @p label is what the picker shows. */
typedef struct {
    int32_t value;
    char label[VIEW_OPTION_LABEL_LEN];
    /* Only a Spoolman spool option carries its filament color: the JSON then
     * has "color": "#rrggbb", or null when the filament has none. Every other
     * option has no "color" key. */
    bool has_color_field;
    bool color_known;
    uint32_t color;
} view_option_t;

typedef struct {
    char id[VIEW_FIELD_ID_LEN];
    const char *label;                          /* a fixed string, never NULL */
    view_field_kind_t kind;
    double value, min, max, step;               /* select: value = the default option's value */
    const char *unit;                           /* "C", "h"; "" for a field with none */
    /* select only: this field's choices are options[option_first ..
     * option_first + option_count) of the view_actions_t that owns the
     * action. The options live once per view_actions_t, not per field, so an
     * action stays small. */
    uint8_t option_first;
    uint8_t option_count;
    /* The field asks something only the person knows, so a submit button
     * stays dead until it holds an answer: a required select is one whose
     * "choose this" choice is below 0. */
    bool required;
} view_field_t;

/* A form's fields live once per view_actions_t, like the options, so an action
 * that has no form (nearly all of them) carries none: this form's fields are
 * fields[field_first .. field_first + field_count) of the view_actions_t that
 * owns the action. Only the actions of view_*_actions() ever have one. */
typedef struct {
    bool present;                              /* form != null */
    uint8_t field_first;
    uint8_t field_count;
    /* What the submit button says, and whether the form is worth submitting
     * as it stands. Both are set only by the forms that own a submit button;
     * every other form carries neither and its renderer behaves as before. */
    const char *submit_label;
    bool require_change;
} view_form_t;

typedef struct {
    bool present;                              /* confirm != null */
    const char *title;
    const char *text;
    const char *ok_label;
} view_confirm_t;

typedef struct {
    char id[VIEW_ACTION_ID_LEN];
    const char *label;                          /* a fixed string, never NULL */
    char line[VIEW_ACTION_LINE_LEN];            /* verbatim, placeholders and all */
    bool enabled;
    const char *reason;                         /* why it is dimmed, "" when enabled: a
                                                * fixed string (actions_map's own
                                                * view_str) or the host's words, never a
                                                * copy, so a translation is never cut */
    view_action_style_t style;
    view_target_t target;                       /* tile actions only; VIEW_TARGET_TILE elsewhere */
    view_confirm_t confirm;
    view_form_t form;
} view_action_t;

typedef struct {
    bool present;                               /* tool != null */
    char label[VIEW_LABEL_LEN];
    bool color_known;                           /* false: color is null, no filament color to show */
    uint32_t color;
    view_ink_t ink;
    bool outline;                               /* color is too dark for the surface: draw a light 1 px outline */
} view_tool_badge_t;

typedef struct {
    bool present;                               /* spare != null */
    bool bare;                                  /* true: position is null, draw the bare mark */
    int position;                               /* 1-based fallback rank; meaningless when bare */
    int count;                                  /* the group's non-loaded member count */
} view_spare_t;

typedef struct {
    bool present;
    float value, set_point;
    bool has_band;                              /* the regulator's normal band is known */
    float band_low, band_high;                  /* 0..1, low < high; drawn, never judged */
    const char *label;                          /* "Pressure" */
    char text[VIEW_PRESSURE_TEXT_LEN];
    char scale[VIEW_SCALE_COUNT][VIEW_SCALE_LEN];   /* tick labels: low, set point, high */
} view_pressure_t;

typedef struct {
    view_activity_kind_t kind;
    const char *steps[MMU_MAX_STEPS];           /* the host's stage labels, borrowed from the model (so a
                                                * step is never cut, and the struct stays small), except
                                                * the running step with a detail, which points at
                                                * detail_text below */
    uint8_t step_count;
    int index;                                  /* -1 = none */
    int failed;                                 /* -1 = none */
    char detail_text[VIEW_STEP_DETAIL_LEN];     /* the running step with its detail, composed (H3) */

    /* The slot's resting state: what the toolhead's path is while no plan
     * runs (PRINCIPLES.md 2, "A reserved slot has a resting state"). It is
     * absent - the whole struct zeroed - whenever a step plan is running, so
     * the two never both say something and the renderer draws one or the
     * other. The label is composed here (REST_PATH, REST_PATH_NO_MATERIAL,
     * REST_READY or REST_INSERT), the color is the loaded filament's own, the one the hotend
     * icon tints with. */
    struct {
        bool present;
        bool loaded;                            /* a bay is feeding the toolhead */
        bool color_known;                       /* false: color is null, draw the neutral track */
        uint32_t color;
        bool outline;                           /* color is too dark for the surface: draw a light 1 px outline */
        char label[VIEW_REST_PATH_LEN];
    } rest;
} view_activity_t;

/* The one message line of a toolhead card: the core merges an error, a
 * runout, a notice and the plain status into it (UNIFIED_UI 4a). */
typedef struct {
    char text[VIEW_MESSAGE_TEXT_LEN];
    view_tone_t tone;
} view_message_t;

typedef struct {
    bool present;
    char text[VIEW_ENV_TEXT_LEN];
} view_env_t;

typedef struct {
    bool present;
    char text[VIEW_DRYER_TEXT_LEN];
    view_dryer_tone_t tone;
    bool adapter_missing;
} view_dryer_t;

/* The summary of an alert group (a unit's or a toolhead's own alerts); the
 * items come from view_unit_alerts() / view_toolhead_alerts(). */
typedef struct {
    bool present;
    int severity;                               /* mmu_severity_t, the worst item's */
    int count;
    view_tone_t tone;                           /* error (a fault or a pause) or info */
    /* True only when nothing in the scope is alerting at all: no badge, no
     * fault or runout, no faulted bay, no unread history entry. A slot with no badge
     * is empty for one of two reasons - nothing alerts (clear: the slot rests
     * on the check) or what alerts is already shown beside it (the message
     * row, a tile's tag; not clear, the slot stays empty). */
    bool clear;
} view_alert_summary_t;

/* The unit-info block. serial/firmware carry nothing yet - no real host field
 * reports them (mmu_unit_ident() is the demo's own fabricated stand-in) - so
 * they are the empty string rather than a buffer nothing ever fills. */
typedef struct {
    bool present;
    const char *family;                         /* the hardware family name, never translated */
} view_info_t;

/* ------------------------------------------------------------------- tile */

typedef struct {
    char slot_id[VIEW_ID_LEN];
    int bay;

    bool has_tool;
    char tool[VIEW_LABEL_LEN];                  /* "T0"; empty when has_tool is false */

    char label[VIEW_LABEL_LEN];
    bool has_sublabel;
    char sublabel[VIEW_SUBLABEL_LEN];

    view_spare_t spare;

    view_tile_state_t state;
    bool has_tag;
    const char *tag;                            /* a fixed string, "" when has_tag is false */
    view_tone_t tag_tone;
    const char *tag_detail;                     /* a fixed string, or the host's own fault
                                                * words, both owned by the model or the
                                                * table: never copied, so a translation
                                                * and a long host sentence are never cut */
    char tag_code[VIEW_ALERT_CODE_LEN];         /* the host's fault code behind an error tag, or "" */

    bool color_known;                           /* false: color and the gradient are null; draw the neutral surface */
    uint32_t color;
    view_ink_t ink;
    uint32_t gradient_top;                      /* 0xRRGGBB, already mixed */
    uint32_t gradient_bottom;
    const char *color_name;                     /* the palette name nearest the color ("black"), borrowed from
                                                 * the table; "" when the color is unknown */
    bool dim;                                   /* empty, unknown or offline: one flat, dimmed look */
    bool hatched;                               /* exactly a bay with no spool (empty) or a tile of an offline unit */

    char name[VIEW_MATERIAL_LEN];
    char material[VIEW_MATERIAL_LEN];
    char brand[VIEW_MATERIAL_LEN];
    char grams_text[VIEW_GRAMS_LEN];
    int pct;                                    /* -1 = unknown */
    char pct_text[VIEW_PCT_LEN];
    bool low;

    bool rfid;
    bool pending_confirmation;
    bool calibrated;

    /* actions: not here (they are only needed once a sheet opens);
     * view_tile_actions() builds them on demand. */
} view_tile_t;

/* ------------------------------------------------------------------- unit */

typedef struct {
    const char *title;                          /* the hardware family ("AMS 2 Pro") */
    char subtitle[VIEW_UNIT_ID_LEN];            /* the unit's config name ("oams1"), which is also its id */
    bool online;
    const char *status_text;                    /* "Offline" when not online, else "" */

    view_env_t env;
    view_dryer_t dryer;
    view_alert_summary_t alert;
    view_info_t info;

    /* actions: view_unit_actions(), on demand. */

    /* bays: the caller asks view_tile() itself for each one (bay_count is
     * how many there are, so a caller can loop 0..bay_count-1). */
    uint8_t bay_count;
} view_unit_t;

/* -------------------------------------------------------------- toolhead */

typedef struct {
    const char *id;                             /* the FPS lane id, as configured */
    char title[VIEW_TITLE_LEN];                 /* the extruder's display name */
    const char *subtitle;                       /* the same FPS id, in the card's own line */

    view_tool_badge_t tool;
    view_pressure_t pressure;
    view_activity_t activity;                   /* the stepper only */
    view_message_t message;
    view_alert_summary_t alert;                 /* the lane fault(s) */

    /* actions: view_toolhead_actions(), on demand. */

    /* units: the caller walks the model's own units whose `.toolhead` index
     * matches this one and calls view_unit() on each (units are not
     * duplicated into this struct). */
} view_toolhead_t;

/* ---------------------------------------------------------------- alert */

typedef struct {
    mmu_severity_t severity;
    char code[VIEW_ALERT_CODE_LEN];             /* "" - not carried by mmu_alert_t yet */
    const char *title;                          /* the severity's name, a fixed string */
    const char *text;                           /* the alert's own sentence: a fixed string
                                                * or the host's words (borrowed, so a
                                                * translation is never cut), or text_buf
                                                * for one the core composes (low filament,
                                                * a job shortfall) in the current language */
    char text_buf[VIEW_ALERT_TEXT_LEN];
    bool has_unit;
    char unit[VIEW_ALERT_UNIT_LEN];
    bool has_slot;
    char slot_id[VIEW_ALERT_SLOT_LEN];          /* "" - not carried by mmu_alert_t yet */
    char when_text[VIEW_WHEN_LEN];              /* "" - no timestamp stored yet */
    bool unread;

    view_action_t actions[VIEW_ACTIONS_PER_ALERT];
    uint8_t action_count;
} view_alert_t;

/* ---------------------------------------------------------------- setting */

/* One choice of the language row: @p value is the language's index in the
 * built-in tables and @p label that language's own name. */
typedef struct {
    int32_t value;
    const char *label;
} view_lang_option_t;

typedef struct {
    char key[VIEW_SETTING_KEY_LEN];
    const char *label;
    const char *note;
    bool value;                                 /* the on/off rows only */
    view_action_t action_on;                    /* present when is_choice is false */
    view_action_t action_off;
    bool is_choice;                             /* the language row: a list, not a switch */
    view_lang_option_t options[VIEW_MAX_LANGS];
    uint8_t option_count;
    int selected;                               /* the current option's index */
} view_setting_t;

/* ------------------------------------------------------------------ panel */

typedef struct {
    bool present;
    const char *text;
} view_panel_notice_t;

/* The panel's own chrome strings (the host may translate them instead). */
typedef struct {
    const char *alerts;
    const char *settings;
    const char *fault;
    const char *pressure;
    const char *no_alerts;
    const char *close;
    const char *cancel;
    const char *no_response;                    /* the panel's own transient message */
    const char *edit_failed;                    /* "Could not save the spool:", the lead of a host's refusal of an edit: it ends in its own colon, and a renderer adds the host's words after a space */
    const char *action_refused;                 /* "Could not do that:", the same lead for a refusal of any other action (a G-code script the printer refused) */
    const char *groups;                         /* the head of a unit's filament groups section */
    const char *no_group;                       /* the row of the bays that belong to no group */
    const char *low;                            /* "Low": a tile's word for a spool under 15 % */
} view_labels_t;

typedef struct {
    bool online;
    uint8_t pending_count;
    char pending[VIEW_MAX_PENDING_SPOOLS][VIEW_ID_LEN];
} view_spoolman_t;

typedef struct {
    /* toolheads: how many of m->toolheads[0..toolhead_count) the caller
     * should build with view_toolhead(); unassigned_units: the model unit
     * indices (0..unit_count) that have no toolhead (toolhead < 0), for the
     * caller to build with view_unit(). */
    uint8_t toolhead_count;
    uint8_t unassigned_units[VIEW_MAX_UNASSIGNED];
    uint8_t unassigned_count;

    /* panel-level alerts: the history entries that belong to no unit
     * (m->alerts[alert_idx[i]]), for the host's bar. */
    uint8_t alert_idx[VIEW_MAX_ALERTS];
    uint8_t alert_count;
    uint8_t unread_count;

    view_panel_notice_t notice;
    view_labels_t labels;
    view_spoolman_t spoolman;

    /* "Nothing is loading", for a list that has nothing to offer: no load or
     * unload is under way anywhere, and so none of the list's rows apply. The
     * empty list's own words, in the current language. */
    view_panel_notice_t nothing_to_load;

    view_setting_t settings[VIEW_MAX_SETTINGS];
    uint8_t setting_count;
} view_panel_t;

/* One row of a list sheet: what it says, what it sends when tapped, and why it
 * cannot right now. Smaller than a view_action_t because a list draws only a
 * label and a reason - it has no form, no confirm and no style. */
typedef struct {
    char id[VIEW_ACTION_ID_LEN];
    const char *label;                          /* a fixed string, never NULL */
    char label_buf[VIEW_LABEL_LEN];             /* ... or this row's own label_buf, for the
                                                   few rows whose label names a bay and so
                                                   the core composes it (view_alert_t's
                                                   text_buf is the same idea) */
    char line[VIEW_ACTION_LINE_LEN];
    const char *reason;                         /* why it is dimmed, "" when it is not */
} view_action_row_t;

/* Everything that applies to the home screen right now, in the order a list
 * shows it. Built on demand (it is the size of a sheet), never inside the
 * panel struct. */
typedef struct {
    view_action_row_t rows[VIEW_LIST_MAX];
    uint8_t count;
} view_action_rows_t;

/** The items of a toolhead's or a unit's alert group, built on demand (their
 * actions are large). */
typedef struct {
    view_alert_t items[VIEW_ALERTS_PER_GROUP];
    uint8_t count;
    int severity;                               /* the worst item's */
    int total;                                  /* the group's count (a lane fault may count several) */
    view_tone_t tone;
} view_alerts_t;

/* ---------------------------------------------------------------- actions */

/** The actions of one tile, unit or toolhead, built when a sheet opens (the
 * widget structs above stay small enough for the device's LVGL task stack).
 * options[] is the shared pool the select fields of items[] index into. */
typedef struct {
    view_action_t items[VIEW_ACTIONS_MAX];
    uint8_t count;
    view_field_t fields[VIEW_MAX_FIELDS];
    uint8_t field_count;
    view_option_t options[VIEW_MAX_OPTIONS];
    uint8_t option_count;
} view_actions_t;

/* --------------------------------------------------------------- builders */

/** Fill @p out for m->units[unit_idx]'s bay @p bay. False (out left zeroed)
 * on an out-of-range @p unit_idx or @p bay. */
bool view_tile(const mmu_model_t *m, int unit_idx, int bay, view_tile_t *out);

/** Fill @p out for m->units[unit_idx]. False (out left zeroed) on an
 * out-of-range @p unit_idx. */
bool view_unit(const mmu_model_t *m, int unit_idx, view_unit_t *out);

/** Fill @p out for m->toolheads[toolhead_idx]. False (out left zeroed) on an
 * out-of-range or unused @p toolhead_idx. */
bool view_toolhead(const mmu_model_t *m, int toolhead_idx, view_toolhead_t *out);

/** The colors the spool editor offers by name (the COLOR_* strings), in the
 * order they are offered; view_palette_count() of them. */
int view_palette_count(void);
uint32_t view_palette_color(int i);
const char *view_palette_name(int i);
/** The palette name nearest to @p rgb (squared RGB distance), so any color - a
 * tag read, an off-shade - still gets a sensible name. */
const char *view_color_name(uint32_t rgb);

/** Why the bay's "Edit spool" is dimmed (the table's own words, valid for as long
 * as the language stands), or NULL when the host can take an edit of it now. For
 * a renderer that draws its own edit screen and still dims by the same check. */
const char *view_tile_edit_reason(const mmu_model_t *m, int unit_idx, int bay);

/** The actions of m->units[unit_idx]'s bay @p bay, in order: load, unload,
 * edit_spool, assign_spool (or spool_list_refresh when the model holds no
 * spool list), unlink, change_group, reread, calibrate. False (out left zeroed) on an out-of-range
 * index. */
bool view_tile_actions(const mmu_model_t *m, int unit_idx, int bay, view_actions_t *out);

/** How many of m->units[unit_idx]'s bays sit in a group: how many
 * view_group() calls a caller makes. */
uint8_t view_group_count(const mmu_model_t *m, int unit_idx);

/** Fill @p out for the @p index'th group (0..view_group_count()-1) that holds
 * a bay of m->units[unit_idx]: its name, its members in order (the unit's own
 * named like their tiles, another unit's dimmed and named with its unit), the
 * next one infinite spooling would load, and whether the membership may change
 * now. False (out left zeroed) on an out-of-range unit or index. */
bool view_group(const mmu_model_t *m, int unit_idx, int index, view_group_t *out);

/** The actions of the @p index'th group of view_group_count(): the "Delete
 * group" action, dimmed with the group's own reason when it may not be deleted
 * (dimmed, never hidden - PRINCIPLES.md ruling 1). False (out left zeroed) on
 * an out-of-range unit or index. */
bool view_group_actions(const mmu_model_t *m, int unit_idx, int index, view_actions_t *out);

/** Fill @p out with m->units[unit_idx]'s bays that are in no group, in bay
 * order. False (out left zeroed) on an out-of-range unit. */
bool view_ungrouped_bays(const mmu_model_t *m, int unit_idx, view_ungrouped_t *out);

/** The actions of m->units[unit_idx] (dry start/stop, clear fault). */
bool view_unit_actions(const mmu_model_t *m, int unit_idx, view_actions_t *out);

/** The actions of m->toolheads[toolhead_idx] (unload, stop, the fault's). */
bool view_toolhead_actions(const mmu_model_t *m, int toolhead_idx, view_actions_t *out);

/** The lane fault(s) of m->toolheads[toolhead_idx] that name no unit, with
 * their actions (retry, resume, clear errors). False (out zeroed) on an
 * out-of-range index; count 0 when there is none. */
bool view_toolhead_alerts(const mmu_model_t *m, int toolhead_idx, view_alerts_t *out);

/** The alerts of m->units[unit_idx]: the lane fault that names this unit (with
 * its actions), the dryer fault and the missing power adapter. */
bool view_unit_alerts(const mmu_model_t *m, int unit_idx, view_alerts_t *out);

/** Fill @p out for m->alerts[alert_idx]. False (out left zeroed) on an
 * out-of-range @p alert_idx. */
bool view_alert(const mmu_model_t *m, int alert_idx, view_alert_t *out);

/** Fill @p out with the panel's counts and notices (not its children: see
 * view_panel_t's comments for how a caller enumerates them). */
void view_panel(const mmu_model_t *m, view_panel_t *out);

/** Fill @p out with everything that applies to the home screen right now, in
 * the order a list shows it: the bound unit's bays (load or unload, the Spool
 * spool's rows), the toolhead's own actions, the spool-list refresh when the
 * model holds no spool list, "Pause and load", and the host's settings. Each
 * row's reason is its own dimmed reason, resolved in the current language, so
 * a renderer draws the row and taps it without asking anything else of the
 * core. False only when @p m is NULL. */
bool view_list_actions(const mmu_model_t *m, view_action_rows_t *out);

#endif /* VIEW_H */
