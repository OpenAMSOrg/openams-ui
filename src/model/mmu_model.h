/**
 * @file mmu_model.h
 * Dummy multi-material-unit data model and the scenarios the simulator cycles.
 *
 * Pure C: no LVGL, no platform headers. Everything lives in static storage
 * because the target has 520 KB of SRAM and no PSRAM.
 */

#ifndef MMU_MODEL_H
#define MMU_MODEL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MMU_MAX_UNITS           6
#define MMU_MAX_SLOTS_PER_UNIT  12
#define MMU_MAX_STEPS           8
#define MMU_MAX_ALERTS          8
#define MMU_MAX_EXT_SPOOLS      2
#define MMU_TEXT_LEN            24
#define MMU_STEP_DETAIL_LEN     32  /**< a step's own detail ("250 °C"), which the
                                      *  running step is drawn with beside it */
#define MMU_MAX_GROUPS          8
#define MMU_MAX_GROUP_MEMBERS   8
#define MMU_MAX_SPOOLS          16
#define MMU_MAX_VENDORS         64  /**< the vendors the edit form offers: Spoolman's
                                      *  own, plus the curated brands
                                      *  (status_map.c's curated_vendors), which
                                      *  is 30 of them */
#define MMU_REFUSAL_LEN         192 /**< a host refusal, whole: "Heat the extruder before an OpenAMS toolchange. ..." runs about 100 bytes */
#define MMU_VENDOR_LEN          32  /**< a vendor's name, whole: a longer one is not offered, never cut */
#define MMU_MAX_TOOLS           8   /**< Maximum number of slicer tools (T0..T7). */
#define MMU_MAX_TOOLHEADS       4   /**< FPS lanes a display's model can hold at once. */
#define MMU_UNIT_NAME_LEN       32  /**< a unit's name with its NUL: 31 characters. The host sets no limit, so a longer one is cut at a character boundary by every writer AND every lookup (cut the key first, then compare), which keeps a long name one unit instead of a new one per update. */
#define MMU_GROUP_NAME_LEN      16  /**< a group's name with its NUL */
#define MMU_LANE_ID_LEN         32  /**< a lane id or extruder name with its NUL */
#define MMU_SLOT_ID_LEN         40  /**< a slot id: a unit name, then the bay number (up to two digits), then the NUL */

/* ------------------------------------------------------------------ types */

typedef enum {
    MMU_UNIT_4SLOT,   /**< AMS-style four-lane unit */
    MMU_UNIT_HT,      /**< single-slot high-temperature drier */
    MMU_UNIT_LANE,    /**< generic lane MMU (e.g. Happy Hare / AFC) */
} mmu_unit_kind_t;

/** Hardware family of a unit, independent of what it can do. */
typedef enum {
    MMU_VARIANT_AMS,
    MMU_VARIANT_AMS2_PRO,
    MMU_VARIANT_AMS_HT,
    MMU_VARIANT_LANE,
} mmu_variant_t;

typedef enum {
    MMU_SIDE_NONE,
    MMU_SIDE_LEFT,
    MMU_SIDE_RIGHT,
} mmu_side_t;

typedef enum {
    MMU_SLOT_EMPTY,
    MMU_SLOT_READY,
    MMU_SLOT_LOADED,
    MMU_SLOT_ERROR,
} mmu_slot_state_t;

typedef enum {
    MMU_INFO_UNKNOWN,  /**< nothing known, tile shows "?" */
    MMU_INFO_TAG,      /**< read from a tag, view-only */
    MMU_INFO_MANUAL,   /**< typed in by the user, editable */
} mmu_info_source_t;

/** PTFE-length calibration state of a bay (C2 5.4). */
typedef enum {
    MMU_CAL_UNKNOWN,       /**< no calibration fact known */
    MMU_CAL_CONFIGURED,    /**< an effective path length exists */
    MMU_CAL_CALIBRATING,   /**< a calibration of this bay is in flight */
    MMU_CAL_UNCALIBRATED,  /**< no effective path length yet */
} mmu_cal_state_t;

typedef enum {
    MMU_DRY_IDLE,
    MMU_DRY_HEATING,
    MMU_DRY_DEHUMIDIFYING,
    MMU_DRY_COOLING,
    MMU_DRY_ERROR,
} mmu_dry_state_t;

typedef enum {
    MMU_BUSY_NONE,
    MMU_BUSY_LOAD,
    MMU_BUSY_UNLOAD,
} mmu_busy_t;

typedef enum {
    MMU_SEV_INFO,
    MMU_SEV_PAUSE,
    MMU_SEV_STOP,
} mmu_severity_t;

typedef struct {
    uint32_t color;                        /**< 0xRRGGBB; meaningful only when color_known */
    bool color_known;                      /**< false: no color data (the tile is neutral); a known 0x000000 is black filament */
    char material[MMU_TEXT_LEN];
    char brand[MMU_TEXT_LEN];
    int16_t remaining_pct;                  /**< -1 = unknown */
    mmu_info_source_t info_source;
    mmu_slot_state_t state;
    int16_t nozzle_min_c;                   /**< -1 = unknown */
    int16_t nozzle_max_c;                   /**< -1 = unknown */
    int16_t k_value_x1000;                  /**< -1 = unknown, else PA * 1000 */
    /* --- C2 additions (see docs/contracts/C2_status.md) --- */
    int32_t spool_id;                       /**< Spoolman spool id, -1 = none */
    bool pending_confirmation;              /**< "New spool, full?" prompt pending */
    int16_t remaining_g;                    /**< grams left, -1 = unknown */
    int16_t initial_g;                      /**< `spool.initial_g`, the spool's own size in grams; -1 = unknown */
    int16_t flow_k_x1000;                   /**< `spool.flow_k`, the spool's own pressure advance; -1 = unknown, else PA * 1000 */
    mmu_cal_state_t cal_state;              /**< PTFE calibration state */
    bool positioning;                       /**< firmware is positioning a new spool */
    int16_t global_id;                      /**< `bays[i].id`, the host's own bay id; -1 = unknown */
    bool acts_known;                        /**< the host sent `bays[i].a`; false (an older host, the demo): nothing is withheld */
    uint8_t acts;                           /**< MMU_BAY_ACT_* bits of the letters `bays[i].a` lists */
} mmu_slot_t;

/** The bay actions `bays[i].a` lists (C2 6.2), one bit per letter. Only the
 * motion ones gate anything; the rest are kept so the mask is the host's list. */
enum {
    MMU_BAY_ACT_LOAD = 1u << 0,         /**< `l` */
    MMU_BAY_ACT_UNLOAD = 1u << 1,       /**< `u` */
    MMU_BAY_ACT_CALIBRATE = 1u << 2,    /**< `c` */
    MMU_BAY_ACT_ASSIGN = 1u << 3,       /**< `a` */
    MMU_BAY_ACT_DETACH = 1u << 4,       /**< `d` */
    MMU_BAY_ACT_OTHER = 1u << 5,        /**< `p`, `q` or `e`: not offered by this display */
};

/** The unit actions `act` lists (C2 6.2) that the display offers, one bit each. */
enum {
    MMU_UNIT_ACT_DRYER_START = 1u << 0,
    MMU_UNIT_ACT_DRYER_STOP = 1u << 1,
    MMU_UNIT_ACT_CLEAR_FAULT = 1u << 2,
    MMU_UNIT_ACT_RFID_SCAN = 1u << 3,
};

/** A named, ordered group ("T0", ...): a tool or material request the
 * slicer loads by name. Members are (unit, slot) pairs on the same FPS lane,
 * in the order infinite spooling falls back through. See docs/GROUPS.md. */
typedef struct {
    char name[MMU_GROUP_NAME_LEN];
    uint8_t count;
    struct {
        int8_t unit;
        int8_t slot;
    } members[MMU_MAX_GROUP_MEMBERS];
} mmu_group_t;

typedef struct {
    char name[MMU_UNIT_NAME_LEN];           /**< "A", "B", "HT-A"; the host's own name, cut to fit */
    mmu_unit_kind_t kind;
    mmu_variant_t variant;                  /**< hardware family ("AMS", ...) */
    char title[MMU_TEXT_LEN];               /**< the product name the host's family carries ("BoxTurtle", "ERCF 2.0"); "" = mmu_variant_name(variant) */
    uint8_t slot_count;
    mmu_side_t side;                        /**< dual-extruder binding */
    bool connected;
    /* capabilities */
    bool has_humidity_level;                /**< five-step A..E */
    bool has_humidity_pct;                  /**< % RH */
    bool has_temp;
    bool can_dry;
    bool rfid;
    /* environment */
    int16_t humidity_level;                 /**< 0..4 = A..E, -1 = none */
    int16_t humidity_pct;                   /**< -1 = none */
    int16_t temp_c;                         /**< -100 = none */
    int16_t ambient_c;                      /**< room temperature COOLING settles toward */
    mmu_dry_state_t dry_state;
    int16_t dry_target_c;
    int16_t dry_remaining_min;
    bool dry_rotate;
    bool power_adapter;
    bool has_power_adapter;                 /**< the host sent a boolean `dryer.adapter` (false: unknown, not "missing") */
    /* --- C2 additions (see docs/contracts/C2_status.md) --- */
    bool normal_motion;                     /**< false when this build is not motion-verified */
    bool follower_uncertain;                /**< an unconfirmed follower stop (C2 5.6) */
    char cannot_dry_reason[MMU_TEXT_LEN * 2]; /* longer than MMU_TEXT_LEN: fits "No power adapter connected" etc. without -Wformat-truncation */
    bool ready_known;                       /**< the host sent `ready`; false: nothing is withheld for it */
    bool ready;                             /**< `ready`: false withholds load, unload and calibrate (a unit that only reports) */
    bool act_known;                         /**< the host sent `act`; false (an older host, the demo): nothing is withheld */
    uint8_t acts;                           /**< MMU_UNIT_ACT_* bits of the entries `act` lists */
    bool dry_unlisted;                      /**< `act` is known and lacks `dryer_start`: drying is unavailable, whatever `dryer` says */
    int16_t host_idx;                       /**< the unit's top-level `idx` (C2 6.2); -1 = unknown */
    char lane[MMU_LANE_ID_LEN];                          /**< the unit's `lane.id` FPS lane (C2 6.2); "" = unknown */
    int8_t toolhead;                        /**< index into mmu_model_t.toolheads[]; -1 = unknown */
    mmu_slot_t slots[MMU_MAX_SLOTS_PER_UNIT];
} mmu_unit_t;

/** Per-toolhead (FPS lane) state (U1, docs/design/UNIFIED_UI.md 3): what the
 * legacy globals below hold for a single lane, duplicated per lane so a
 * multi-toolhead host (the Mainsail panel) does not have one lane's
 * projection overwrite another's. Field names and types mirror the globals
 * exactly, so mmu_model_refresh_this()/mmu_model_capture_this() are a
 * mechanical field-by-field copy. */
typedef struct {
    char id[MMU_LANE_ID_LEN];               /**< the C2 lane id ("fps1"); "" = the demo's one toolhead */
    char extruder[MMU_LANE_ID_LEN];         /**< `lane.ext` (C2 6.2); "" = unknown */

    /* filament currently parked in this toolhead */
    bool loaded;
    int8_t loaded_unit;                     /**< -1 = none */
    int8_t loaded_slot;
    int8_t loaded_ext;                      /**< >= 0 = external spool */
    bool toolhead_sensor;

    /* load / unload in progress */
    mmu_busy_t busy;
    int8_t busy_unit;
    int8_t busy_slot;
    char steps[MMU_MAX_STEPS][MMU_TEXT_LEN * 2];
    uint8_t step_count;
    int8_t step_current;                    /**< -1 = none */
    int8_t step_failed;                     /**< -1 = none */
    char step_detail[MMU_STEP_DETAIL_LEN];  /**< the running step's own detail; "" = none */

    /** The group loaded in this toolhead right now; -1 = none. */
    int8_t current_group;

    /* active error (the lane fault, C2 5.7) */
    bool has_error;
    mmu_severity_t error_severity;
    char error_code[40];
    char error_text[MMU_TEXT_LEN * 4];
    int8_t error_unit;
    int8_t error_slot;
    char error_act[2][16];
    uint8_t error_act_count;
    uint8_t error_count;

    bool motion_settled;

    /* runout / auto-refill banner */
    bool runout_active;
    char runout_material[MMU_TEXT_LEN];
    char runout_note[MMU_TEXT_LEN * 2];
    int8_t runout_from_unit;
    int8_t runout_from_slot;
    int8_t runout_to_unit;
    int8_t runout_to_slot;

    /* print */
    int8_t print_unit;
    int8_t print_slot;
    uint16_t filament_changes;

    /* C2 6.5: from oams_manager.lanes_by_fps.<l>, not the openams_ui
     * projection (which deliberately keeps fast-changing values out). */
    float pressure;                         /**< -1 = unknown */
    float set_point;                        /**< -1 = unknown */
    float band_low, band_high;              /**< the regulator's normal band; -1 = unknown */
} mmu_toolhead_t;

typedef struct {
    bool present;
    mmu_side_t side;
    uint32_t color;
    char material[MMU_TEXT_LEN];
} mmu_ext_spool_t;

/** One entry from the Spoolman spool list (parsed from `server.spoolman.proxy`
 * GET /v1/spool). Only unarchived spools are stored; id == 0 is an empty slot. */
typedef struct {
    int32_t id;
    char material[12];
    char vendor[16];
    uint32_t color;
    bool color_known;                         /**< false: the filament has no color_hex */
    int32_t remaining_g;                      /**< -1 = unknown */
} mmu_spool_t;

/** What an alert's text is. The two the core raises itself are kept as numbers
 *  and turned into a sentence when the view is built, in the language then in
 *  use (a stored sentence would be fixed in the language it was raised in, and
 *  would need a buffer sized for the longest translation in every history
 *  slot). Anything else is the host's own words, in @c text. */
typedef enum {
    MMU_ALERT_PLAIN = 0,                    /**< the host's own words, in text */
    MMU_ALERT_LOW_FILAMENT,                 /**< arg[0] = bay, arg[1] = grams left; unit = the unit */
    MMU_ALERT_SHORTFALL,                    /**< arg[0] = tool, arg[1] = grams needed, arg[2] = have */
} mmu_alert_kind_t;

typedef struct {
    char text[MMU_TEXT_LEN * 2];            /**< MMU_ALERT_PLAIN only */
    uint8_t kind;                           /**< mmu_alert_kind_t */
    int16_t arg[3];                         /**< the numbers of a composed alert */
    mmu_severity_t severity;
    int8_t unit;                            /**< the unit it is about; -1 = none */
    int8_t toolhead;                        /**< the toolhead (FPS lane) it is about; -1 = none */
    bool lane_fault;                        /**< a lane fault's history entry (the live fault is on its toolhead) */
    bool unread;
    uint32_t raised_ms;                     /**< the model clock (now_ms) when it was raised */
    uint32_t read_ms;                       /**< the model clock when it was marked read (unread == false) */
} mmu_alert_t;

/** How long a history entry stays in a toolhead's badge: read, 2 minutes
 *  after it was read; never read, 15 minutes after it was raised
 *  (UNIFIED_UI 4g). It stays in the alerts history either way. */
#define MMU_ALERT_BADGE_READ_MS   120000u
#define MMU_ALERT_BADGE_UNREAD_MS 900000u

/** Where the filament in the toolhead came from, relative to this display. */
typedef enum {
    MMU_FEED_NONE,                          /**< toolhead empty */
    MMU_FEED_THIS,                          /**< a slot of the bound unit */
    MMU_FEED_OTHER_UNIT,                    /**< a slot of another unit */
    MMU_FEED_EXT,                           /**< an external spool */
} mmu_feed_t;

/** The host's five settings (C2 4.9), mirrored so the Settings screen can
 * show and change them. `known` is false until the host has published the
 * object; the booleans start on the C2 defaults. */
typedef struct {
    bool known;
    bool read_tag_on_insertion;
    bool read_tags_at_startup;
    bool auto_reload_on_runout;
    bool auto_create_spoolman_spools;
    bool apply_pa_on_load;
} mmu_settings_t;

typedef struct {
    bool dual_extruder;

    /* --- C2 additions (see docs/contracts/C2_status.md) --- */
    bool motion_settled;                    /**< lane motion settled, no motion action in progress */
    bool spoolman_online;                   /**< Spoolman is reachable */
    bool spoolman_stop;                     /**< the host has a sequence stop (`stop` in the component's status): server.openams.stop */
    bool spoolman_edit;                     /**< the component can edit a bay's spool (`edit` in its status) */
    /** The outcome of the last spool edit, for the display to say once:
     *  edit_seq changes with every answer, and edit_refusal is the host's own
     *  words when it refused ("Spoolman is offline"), "" when it took the edit. */
    char edit_refusal[MMU_REFUSAL_LEN];
    uint8_t edit_seq;
    /* The host's answer to an action the display sent (mmu_action_kind_t):
     * action_refusal is its own words when it refused, "" when it took the
     * action, and action_seq moves on with every answer so a display says a
     * refusal once. action_pending_kind is a load or an unload the host has
     * not reported yet, and action_pending_seq moves on with each new one.
     * action_started_kind is one the host reported running before it
     * answered: a failure answer to it is the run's outcome, which the
     * host's status reports, never a refusal to start. */
    char action_refusal[MMU_REFUSAL_LEN];
    uint8_t action_refusal_kind;
    uint8_t action_seq;
    uint8_t action_pending_kind;
    uint8_t action_pending_seq;
    uint8_t action_started_kind;
    char vendors[MMU_MAX_VENDORS][MMU_VENDOR_LEN]; /**< the vendors the edit form
                                                      *  offers: "Generic", then
                                                      *  Spoolman's own (sorted, its
                                                      *  spelling winning), then the
                                                      *  curated brands (see
                                                      *  status_map.c) */
    uint8_t vendor_count;
    mmu_settings_t settings;                /**< the host's five settings (C2 4.9) */

    mmu_unit_t units[MMU_MAX_UNITS];
    uint8_t unit_count;

    /** Per-toolhead (FPS lane) state; see mmu_toolhead_t. */
    mmu_toolhead_t toolheads[MMU_MAX_TOOLHEADS];
    uint8_t toolhead_count;

    /** Index of the unit this display is mounted on. */
    int8_t this_unit;

    mmu_ext_spool_t ext_spools[MMU_MAX_EXT_SPOOLS];
    uint8_t ext_spool_count;

    /** Spoolman spool list, refreshed by `spool list` / proxy result. */
    mmu_spool_t spools[MMU_MAX_SPOOLS];
    uint8_t spool_count;

    mmu_group_t groups[MMU_MAX_GROUPS];
    uint8_t group_count;
    /** The group loaded in the toolhead right now; -1 = none. Kept in sync
     * whenever loaded_unit/loaded_slot changes. */
    int8_t current_group;

    /* filament currently parked in the toolhead */
    bool loaded;
    int8_t loaded_unit;                     /**< -1 = none */
    int8_t loaded_slot;
    int8_t loaded_ext;                      /**< >= 0 = external spool */
    bool toolhead_sensor;

    /* load / unload in progress */
    mmu_busy_t busy;
    int8_t busy_unit;
    int8_t busy_slot;
    char steps[MMU_MAX_STEPS][MMU_TEXT_LEN * 2];
    uint8_t step_count;
    int8_t step_current;                    /**< -1 = none */
    int8_t step_failed;                     /**< -1 = none */
    char step_detail[MMU_STEP_DETAIL_LEN];  /**< the running step's own detail; "" = none */

    /* print */
    bool printing;
    int8_t print_unit;
    int8_t print_slot;
    uint16_t filament_changes;

    /* active error */
    bool has_error;
    mmu_severity_t error_severity;
    char error_code[40]; /* contract fault codes are identifiers; never truncate */
    char error_text[MMU_TEXT_LEN * 4]; /* longer than *3: fits the longest scenario error message without -Wformat-truncation */
    int8_t error_unit;
    int8_t error_slot;
    char error_act[2][16];                  /**< the fault's first two `act` tokens, host order */
    uint8_t error_act_count;                /**< 0..2 tokens held in error_act */
    uint8_t error_count;                    /**< the lane's fault count (`nf`); 0 = none */

    /* notices */
    mmu_alert_t alerts[MMU_MAX_ALERTS];
    uint8_t alert_count;
    /** The model's clock in milliseconds (monotonic, wraps at 2^32 and is
     *  compared by difference). The platform sets it; the core never reads a
     *  clock, so tests set it explicitly. It stamps alerts as they are raised
     *  and read. */
    uint32_t now_ms;
    uint8_t alert_unread;

    /** Faults that arrived while `map_fault` wrote to this model but before
     * the history lives in the authoritative UI copy.  Drained into the UI
     * model's `alerts[]` by mmu_model_merge_live(). */
    mmu_alert_t alert_pending[4];
    uint8_t alert_pending_count;

    /* runout / auto-refill banner */
    bool runout_active;
    char runout_material[MMU_TEXT_LEN];
    char runout_note[MMU_TEXT_LEN * 2];

    /* where the last auto-refill came from and went, -1 = unused */
    int8_t runout_from_unit;
    int8_t runout_from_slot;
    int8_t runout_to_unit;
    int8_t runout_to_slot;

    /** Filename of the current print job as last seen in print_stats. */
    char print_filename[128];
    /** Per-tool filament need from the job's file metadata (grams, rounded up);
     *  0 means no requirement for that tool. */
    int16_t job_need_g[MMU_MAX_TOOLS];
    /** Bitmask: bit i is set when tool i has less filament available than
     *  job_need_g[i] (all-unknown groups are never marked). */
    uint8_t job_short;

    const char *scenario_name;
    uint32_t tick;
    /* --- home additions --- */
    bool home_selection;                    /**< selected_* are meaningful */
    int8_t selected_unit;                   /**< last picked unit, -1 = none */
    int8_t selected_slot;                   /**< last picked slot, -1 = none */
    int8_t selected_ext;                    /**< last picked ext spool, -1 = none */
} mmu_model_t;

/* ---------------------------------------------------------------- globals */

extern mmu_model_t g_mmu;

/** Names accepted by --scenario; MMU_SCENARIO_COUNT entries, NULL-terminated
 * outside the array. */
extern const char *const mmu_scenario_names[];
#define MMU_SCENARIO_COUNT 9

/* --------------------------------------------------------------- lifecycle */

void mmu_model_init(void);
/** Load one of mmu_scenario_names[]. Unknown names fall back to "idle". */
void mmu_model_set_scenario(const char *name);
/** Advance animated state (load steps, drying countdown). Called from a timer. */
void mmu_model_tick(void);

/** Fill @p s with the C2 4.9 defaults and `known = false`. */
void mmu_settings_defaults(mmu_settings_t *s);

/* ------------------------------------------------------------- spool edit */

#define MMU_MATERIAL_COUNT 10
/** The common materials the edit form offers, in order ("PLA", "PETG", ...);
 *  "" out of range. A spool's own material, when it is none of these, is one
 *  more choice the form adds (index MMU_MATERIAL_COUNT). */
const char *mmu_material_name(int i);

/** The vendor the edit form falls back to, and what Spoolman calls it. */
#define MMU_VENDOR_GENERIC "Generic"

/** How many vendors the edit form offers: Spoolman's own list, plus "Generic"
 *  when that list does not hold it (so a host with no list yet still offers
 *  one). A bay's own vendor, when it is none of these, is one more choice the
 *  form adds (index mmu_edit_vendor_count()). */
int mmu_edit_vendor_count(const mmu_model_t *m);
/** Vendor @p i of the form, or "" out of range. */
const char *mmu_edit_vendor_name(const mmu_model_t *m, int i);

/** What the edit form starts from for a bay: its own facts where the model
 *  has them, else a default. One function for both cores' views and action
 *  mappers, so "unchanged" means the same thing to both. */
typedef struct {
    int material;       /**< index of mmu_material_name(), or MMU_MATERIAL_COUNT: the bay's own */
    int32_t color;      /**< 0xRRGGBB, -1 = no known color (nothing to send) */
    int vendor;         /**< index of mmu_edit_vendor_name(), or mmu_edit_vendor_count(): the bay's own */
    int remaining_g;
    int initial_g;      /**< the spool's own `initial_g`, else read back from the remaining grams and percent; 1000 when neither is known */
    int pa_x1000;       /**< the spool's own `flow_k`, else the scenario's PA, else 20 when unknown */
} mmu_edit_defaults_t;

void mmu_edit_defaults(const mmu_model_t *m, int unit_idx, int slot_idx, mmu_edit_defaults_t *out);

/** The host answered a spool edit: @p refusal is its message, "" for success.
 *  Sets edit_refusal and moves edit_seq on. */
void mmu_model_edit_result(mmu_model_t *m, const char *refusal);

/** ---------------------------------------------------------------- actions */

/** What an action the display sent does, which is what its answer is about:
 *  a load or an unload the load screen waits on, or anything else. */
typedef enum {
    MMU_ACTION_OTHER = 0,
    MMU_ACTION_LOAD,
    MMU_ACTION_UNLOAD
} mmu_action_kind_t;

/** The kind of the action line @p line (as the display wrote it: "load A2",
 *  "unload A2"), or MMU_ACTION_OTHER for anything else. The platforms call it
 *  on the way out, so an answer that comes back names the action it is about. */
mmu_action_kind_t mmu_action_kind_of(const char *line);

/** The display just sent a load or an unload and the host has not reported it
 *  yet: the load screen says it is starting until the model's own busy state
 *  moves (mmu_model_refresh_this() from a status update) or the display's own
 *  deadline passes. action_pending_seq moves on, so a display can tell a new
 *  one from the one it is already showing. */
void mmu_model_action_sent(mmu_model_t *m, mmu_action_kind_t kind);

/** The host's answer to an action of that kind: @p message is its own words
 *  when it refused, "" when it took the action. Sets action_refusal and moves
 *  action_seq on, so a display says a refusal once. A refused load or unload
 *  also ends the pending state - the host has spoken, and it said no. A
 *  failure for a load or an unload the host already reported running is not
 *  a refusal and changes nothing (see action_started_kind). */
void mmu_model_action_result(mmu_model_t *m, mmu_action_kind_t kind, bool ok, const char *message);

/** Live mode: the host's status is the only source of truth, so every model
 * function the screens call to *simulate* an operation does nothing. */
void mmu_model_set_live(bool on);
bool mmu_model_is_live(void);

/* ----------------------------------------------------------------- helpers */

/* ------------------------------------------------------- UTF-8-safe copies
 *
 * Every buffer below holds text the host wrote: a unit's config name, a
 * spool's material, a fault's sentence. snprintf() would keep the first
 * n - 1 *bytes* and could end in the middle of a multi-byte character - a
 * CJK name is three bytes a glyph - leaving a broken sequence that renders as
 * a replacement glyph. The TypeScript core cuts on character boundaries
 * (cut() in web/logic/cstr.ts) and the differential tests hold the two cores
 * to the same rule, so these two functions are the whole C side of it.
 *
 * mmu_copy_text() leaves a buffer of no size alone rather than writing past
 * it; the C writer in src/view/strings.c does the same, and no field in the
 * core is ever zero-sized. */

/** Copy @p src into @p out[0 .. n), whole UTF-8 characters only, always
 *  NUL-terminated when @p n is not 0. A character that would not fit whole is
 *  dropped rather than split, so the result is always valid UTF-8. Returns the
 *  number of bytes written, the NUL not counted. */
size_t mmu_copy_text(char *out, size_t n, const char *src);

/** mmu_copy_text() onto the end of what @p out already holds. Appending a
 *  number to a name, or a name to a sentence, must cut on the same character
 *  boundary a copy does. */
void mmu_append_text(char *out, size_t n, const char *src);

const char *mmu_unit_name(int unit_idx);
/** "AMS", "AMS 2 Pro", "AMS HT", "Lane MMU". */
const char *mmu_variant_name(mmu_variant_t v);
/** The one place a unit's title comes from: the product name the host's family
 *  named the unit ("BoxTurtle"), or its variant's name when the family carries
 *  none. A product name is a proper noun and is never translated. */
const char *mmu_unit_title(const mmu_unit_t *u);
/** "A1", "HT-A1" */
void mmu_slot_id(int unit_idx, int slot_idx, char *out, size_t out_len);
/** mmu_slot_id() against @p m instead of the g_mmu singleton ("?" + number
 * for a unit index outside @p m). */
void mmu_slot_id_in(const mmu_model_t *m, int unit_idx, int slot_idx, char *out, size_t out_len);
const char *mmu_slot_display_material(const mmu_slot_t *slot);
bool mmu_unit_is_slot_loaded(int unit_idx, int slot_idx);
bool mmu_unit_is_slot_busy(int unit_idx, int slot_idx);

/* ------------------------------------------------------------- this unit */

/** Index of the bound unit; clamps to 0 when out of range. */
int mmu_this_unit(void);
/** True when @p unit_idx is the unit this display is bound to. */
bool mmu_is_this_unit(int unit_idx);
/** The bound unit (never NULL). */
mmu_unit_t *mmu_this(void);
/** Bind the display to @p unit_idx; out-of-range values are ignored. */
void mmu_set_this_unit(int unit_idx);
/** Bind by unit name; false (binding unchanged) when no unit has that name. */
bool mmu_set_this_unit_by_name(const char *name);

/* -------------------------------------------------------------- toolheads */

/** The toolhead of the unit this display is bound to, or NULL when that unit
 * has none yet. */
mmu_toolhead_t *mmu_this_toolhead(void);
/** @p m's toolhead at @p idx, or NULL when out of range. */
const mmu_toolhead_t *mmu_model_toolhead(const mmu_model_t *m, int idx);

/** Copy the toolhead of m->units[m->this_unit] into the legacy globals
 * (loaded*, busy*, steps[]/step_*, current_group, has_error/error_*,
 * motion_settled, runout_*, print_unit/print_slot/filament_changes). With no
 * such toolhead, the globals are left as they are. Called at the end of every
 * status_map_apply_*() and by mmu_model_merge_live(); safe to call any time. */
void mmu_model_refresh_this(mmu_model_t *m);

/** The reverse of mmu_model_refresh_this(): copy the legacy globals into the
 * toolhead of m->units[m->this_unit]. Called by the dummy scenarios and
 * transitions, which still write the globals directly. A no-op when that
 * unit has no toolhead. */
void mmu_model_capture_this(mmu_model_t *m);

/* ------------------------------------------------- other units, as facing this one */

/** Where the filament in the toolhead came from, relative to this unit. */
mmu_feed_t mmu_toolhead_source(void);
/** True while a load/unload runs that does not target this unit; @p why
 * describes it ("Unit A is loading A3"). */
bool mmu_busy_elsewhere(char *why, size_t len);
/** True while a print is running from a slot of another unit. */
bool mmu_printing_elsewhere(void);
/** True when the active error is this unit's (or system-wide). */
bool mmu_error_concerns_this(void);

/* ---------------------------------------------------------- alerts, scoped */

/** True when alert @p alert_idx is system-wide or belongs to this unit. */
bool mmu_alert_visible(int alert_idx);
/** How many alerts are visible to this display. */
int mmu_alert_visible_count(void);
/** How many visible alerts are unread. */
int mmu_alert_unread_visible(void);
/** Whether a history entry has aged out of the badge at the model's clock. */
bool mmu_alert_badge_expired(const mmu_model_t *m, const mmu_alert_t *a);
/** Mark every visible alert read and recompute alert_unread. */
void mmu_alerts_mark_read(void);

/* ------------------------------------------------------------------ groups */

/** The two choices of the "Change group..." list that are not a group: the
 * values the view writes for "No group" and "New group". Every other choice
 * of that list is the index of a group in mmu_model_t.groups, and both cores
 * read the same two sentinels (web/logic/model.ts). */
#define MMU_GROUP_CHOICE_NONE (-1)
#define MMU_GROUP_CHOICE_NEW  (-2)

/** The group @p unit/@p slot belongs to, or -1 when it is unassigned. */
int mmu_slot_group(int unit, int slot);
/** mmu_slot_group() against @p m instead of the g_mmu singleton. */
int mmu_slot_group_in(const mmu_model_t *m, int unit, int slot);
/** The first member of @p group, in declared order, that is ready, has
 * filament left, and is not the one currently loaded - exactly what
 * infinite spooling would load next. False when there is no such spare. */
bool mmu_group_next_spare(int group, int *out_unit, int *out_slot);
/** mmu_group_next_spare() against @p m instead of the g_mmu singleton. */
bool mmu_group_next_spare_in(const mmu_model_t *m, int group, int *out_unit, int *out_slot);
/** False while @p group is the one loaded (current_group) and something is
 * loaded, or while a load/unload/runout is in progress anywhere. */
bool mmu_group_editable(int group);
/** mmu_group_editable() against @p m instead of the g_mmu singleton. */
bool mmu_group_editable_in(const mmu_model_t *m, int group);
/** "" while @p group's membership may be changed now, else the one refusal
 * that stops it (docs/GROUPS.md: a load/unload/calibration in progress, a
 * runout in progress, or the group that is loaded on its lane). A fixed
 * string from the view's table, valid as long as the language stands. The
 * first two rules are the lane's, so they answer for a @p group of -1 too -
 * a bay in no group is still refused while the lane is busy; the third needs
 * a group and so does not apply there. */
const char *mmu_group_edit_reason(const mmu_model_t *m, int group);
/** The next free "T<n>" a new group would take, into @p out (>= 8 bytes):
 * the lowest index whose name no group holds. */
void mmu_group_next_name(const mmu_model_t *m, char *out, size_t n);
/** True when @p group may hold a bay of m->units[unit_idx]'s lane: one of its
 * members is on a unit of the same FPS lane, or it is empty (a group with no
 * bay is on no lane at all, so it is nobody else's group). A unit whose lane
 * the host has not reported belongs to no lane, and then every group may. */
bool mmu_group_on_unit_lane(const mmu_model_t *m, int unit_idx, int group);
/** Move (@p unit, @p slot) into @p group, removing it from its previous
 * group (the donor) first. Logs "assign <group> <slot id>". */
void mmu_group_assign(int group, int unit, int slot);
/** Remove (@p unit, @p slot) from whichever group it is in; a no-op when it
 * is not in one. */
void mmu_group_unassign(int unit, int slot);
/** Create a new, empty group named @p name, or the next free "T<n>" when
 * @p name is NULL or empty. Returns its index, or -1 when the table is
 * full. */
int mmu_group_create(const char *name);

/** The enough-filament shortfall for slicer tool @p tool_index (0-based,
 * group "T<tool_index>"), recomputed fresh from @p m every call (U1c;
 * docs/design/UNIFIED_UI.md 3 and 4's toolhead.notice). False when there is
 * nothing to report (no need recorded, no such group, every member unknown,
 * or the group actually has enough); true with @p need_g/@p have_g filled
 * otherwise. */
bool mmu_model_tool_shortfall(const mmu_model_t *m, int tool_index, int *need_g, int *have_g);

/* ------------------------------------------------------ backups and runout */

/** The next spare in this unit's @p slot's group (mmu_group_next_spare on
 * top of mmu_slot_group). False when the slot is unassigned or has none. */
bool mmu_slot_backup(int slot, int *out_unit, int *out_slot);
/** True when an auto-refill runout involves this unit (from or to). */
bool mmu_runout_concerns_this(void);

/* ------------------------------------------------------- dummy transitions */

/** Keyboard-free stand-ins for the real backend. They log via actions.c and
 * animate the model so the UI has something to show. */
void mmu_model_start_load(int unit_idx, int slot_idx);
void mmu_model_start_unload(void);
void mmu_model_stop_busy(void);
void mmu_model_set_reread(int unit_idx, int slot_idx);

/* --- misc additions --- */
/* Dummy per-unit identity and firmware versions for the Unit info screen.
 * Read-only; the real backend would report these over Moonraker. */
#define MMU_IDENT_MODEL_LEN   16
#define MMU_IDENT_SERIAL_LEN  18
#define MMU_IDENT_FW_LEN      12

typedef struct {
    char model[MMU_IDENT_MODEL_LEN];    /**< "AMS 2 Pro" */
    char serial[MMU_IDENT_SERIAL_LEN];  /**< "0M10A3512300738" */
    char firmware[MMU_IDENT_FW_LEN];    /**< installed version */
    char latest[MMU_IDENT_FW_LEN];      /**< newest available version */
} mmu_unit_ident_t;

/** Fill @p out with the identity of @p unit_idx; false when out of range. */
bool mmu_unit_ident(int unit_idx, mmu_unit_ident_t *out);
/** True when @p unit_idx has a firmware newer than the one installed. */
bool mmu_unit_update_available(int unit_idx);
/** How many connected units report an available update. */
int mmu_unit_update_count(void);
/* --- load additions --- */

/** Start a manual load from external spool @p ext_idx. busy_unit/busy_slot are
 * -1 for external spools, so the target is remembered separately (there is no
 * slot index to point at). */
void mmu_model_start_ext_load(int ext_idx);
/** "Filament extruded, continue": the external spool becomes what is loaded. */
void mmu_model_confirm_ext_load(void);
/** "Not extruded yet, retry": run the external load again. */
void mmu_model_retry_ext_load(void);
/** Target of the last external load, or -1 when none was started. */
int mmu_model_ext_target(void);
/** Drop informational notices, keeping stop/pause ones. */
void mmu_model_clear_resolved_alerts(void);
/** Claim a fault solved: clear the active error and let the load resume. */
void mmu_model_clear_error(void);
/** Record a fault that has just arrived into @p m: newest first, dropping the
 * oldest when the table is full, and marked unread. */
void mmu_alerts_push_to(mmu_model_t *m, const char *text, mmu_severity_t severity, int unit);
/** As mmu_alerts_push_to(), also recording the toolhead the alert is about
 * (-1 = none) and whether it is a lane fault's entry. Where an alert belongs
 * in the panel follows: a unit's, else a toolhead's, else the panel's. */
void mmu_alerts_push_at(mmu_model_t *m, const char *text, mmu_severity_t severity, int unit,
                        int toolhead, bool lane_fault);
/** Wrapper: push into the global g_mmu history. */
void mmu_model_push_alert(const char *text, mmu_severity_t severity, int unit);
/** Push an alert into @p m's pending queue (drained into the UI model's history
 *  by mmu_model_merge_live).  Used from status mapping callbacks where @p m is
 *  a shadow model, not g_mmu. */
/** Queue a composed alert (kind != MMU_ALERT_PLAIN) with its numbers: the
 *  sentence is made when the view is built, in the current language. */
void mmu_model_push_alert_pending_tpl(mmu_model_t *m, mmu_alert_kind_t kind,
                                      mmu_severity_t severity, int unit, int toolhead,
                                      int a0, int a1, int a2);

void mmu_model_push_alert_pending_at(mmu_model_t *m, const char *text, mmu_severity_t severity,
                                     int unit, int toolhead, bool lane_fault);
void mmu_model_push_alert_pending(mmu_model_t *m, const char *text,
                                   mmu_severity_t severity, int unit);

/** Merge @p live into @p ui:
 *   - copy all of *live over *ui except the alert history (alerts[], alert_count,
 *     alert_unread), which keeps ui's values;
 *   - drain each alert from live's pending queue into ui's history in arrival order;
 *   - clear live's pending queue.
 *   When ui == live (simulator single-model path) the copy is skipped and only
 *   the pending drain runs. Returns true. */
bool mmu_model_merge_live(mmu_model_t *ui, mmu_model_t *live);

/* --- home additions --- */

/** Remember which tile the user opened (unit/slot, or ext spool with unit < 0),
 * so the edit and environment screens can start on it. unit < 0 clears it. */
void mmu_home_set_selection(int unit_idx, int slot_idx, int ext_idx);
/** Load an external spool: animate the steps and park it in the toolhead so
 * the home tile shows it as loaded right away. */
void mmu_home_start_ext_load(int ext_idx);
/** Unload whatever is in the toolhead towards the external spool. */
void mmu_home_start_ext_unload(void);

/* --- app additions --- */

/** MMU_SCENARIO_COUNT, as a function (the walk and the debug screen iterate
 * without needing the macro). */
int mmu_model_scenario_count(void);
/** mmu_scenario_names[i], or "?" out of range. */
const char *mmu_model_scenario_name(int i);

#endif /* MMU_MODEL_H */
