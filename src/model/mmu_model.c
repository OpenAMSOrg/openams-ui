/**
 * @file mmu_model.c
 * Dummy data for the screen workflow. One static model instance, no heap.
 */

#include "model/mmu_model.h"
#include "view/strings.h"

#include "app/actions.h"

#include <string.h>
#include <stdio.h>

mmu_model_t g_mmu;

const char *const mmu_scenario_names[MMU_SCENARIO_COUNT] = {
    "idle", "loading", "error", "drying", "printing", "runout", "dual", "remote", "lanes"
};

/* At the simulator's 500 ms model timer, three ticks is the ~1.5 s per step
 * the spec asks for. */
#define TICKS_PER_STEP   3
#define TICKS_DRY_PHASE  4
#define SECONDS_PER_MIN  1

static uint16_t s_dry_phase[MMU_MAX_UNITS];

/* Name of the unit this display is bound to. Kept outside g_mmu so it survives
 * mmu_model_set_scenario() rebuilding the whole model. */
static char s_this_name[MMU_UNIT_NAME_LEN];

/* Live mode. Also outside g_mmu: net_link_sync() copies the host's shadow over
 * the whole struct on every status update, which would otherwise clear it. */
static bool s_live;

/* ------------------------------------------------------- UTF-8-safe copies */

/** The bytes of the UTF-8 character that starts with @p b (1 for ASCII), 0
 *  for a byte that cannot start one: the same rule as cut() in
 *  web/logic/cstr.ts. */
static size_t utf8_len(unsigned char b)
{
    if(b < 0x80) return 1;
    if((b & 0xE0) == 0xC0) return 2;
    if((b & 0xF0) == 0xE0) return 3;
    if((b & 0xF8) == 0xF0) return 4;
    return 0;
}

size_t mmu_copy_text(char *out, size_t n, const char *src)
{
    size_t o = 0;

    if(!out || n == 0) return 0;       /* nothing to write into, or nowhere to write */
    out[0] = '\0';
    if(!src) return 0;

    while(*src) {
        size_t clen = utf8_len((unsigned char)*src);

        if(clen == 0) clen = 1;                /* not UTF-8: a byte is a byte */
        if(o + clen + 1 > n) break;            /* would not fit whole: stop */
        memcpy(out + o, src, clen);
        o += clen;
        src += clen;
    }
    out[o] = '\0';
    return o;
}

void mmu_append_text(char *out, size_t n, const char *src)
{
    size_t used;

    if(!out || n == 0) return;
    used = strnlen(out, n);
    if(used >= n - 1) return;                  /* already full, NUL included */
    mmu_copy_text(out + used, n - used, src);
}

/* Dummy stand-in for C2's motion_safety predicate (C2 4.2, 5.6): false while a
 * load/unload is running or an error is latched. The real backend computes it
 * from the lane's op, follower and faults. */
static void sync_motion_settled(void)
{
    g_mmu.motion_settled = (g_mmu.busy == MMU_BUSY_NONE && !g_mmu.has_error);
}

/* ---------------------------------------------------------------- builders */

/* Dummy Spoolman id for a tag-read spool: a function of the decoded facts, so
 * it is stable across scenario rebuilds and different spools get different
 * ids. C2 only requires the link to be some positive id (C2 5.5). */
static int32_t tag_spool_id(uint32_t color, int16_t remaining)
{
    return (int32_t)(1 + (((color >> 8) ^ (uint32_t)remaining) & 0x7FFF));
}

static void slot_set(mmu_slot_t *s, uint32_t color, const char *material,
                     const char *brand, int16_t remaining, mmu_info_source_t src,
                     mmu_slot_state_t state, int16_t nmin, int16_t nmax,
                     int16_t k_x1000)
{
    memset(s, 0, sizeof(*s));
    s->color = color;
    /* a bay with nothing known about its spool has no color either; black
     * filament is a spool the scenario names as such */
    s->color_known = (src != MMU_INFO_UNKNOWN);
    mmu_copy_text(s->material, sizeof(s->material), material);
    mmu_copy_text(s->brand, sizeof(s->brand), brand);
    s->remaining_pct = remaining;
    s->info_source = src;
    s->state = state;
    s->nozzle_min_c = nmin;
    s->nozzle_max_c = nmax;
    s->k_value_x1000 = k_x1000;
    /* A tag read links the spool in Spoolman (C2 5.5); grams mirror the
     * percentage of a 1000 g spool. */
    s->spool_id = (src == MMU_INFO_TAG) ? tag_spool_id(color, remaining) : -1;
    s->pending_confirmation = false;
    s->remaining_g = (src == MMU_INFO_TAG && remaining >= 0)
                     ? (int16_t)(10 * remaining)
                     : -1;
    /* a scenario names no spool size and no host-reported PA of its own */
    s->initial_g = -1;
    s->flow_k_x1000 = -1;
    s->cal_state = MMU_CAL_CONFIGURED;
    s->positioning = false;
    s->global_id = -1;
}

static void slot_empty(mmu_slot_t *s)
{
    slot_set(s, 0x000000, "", "", -1, MMU_INFO_UNKNOWN, MMU_SLOT_EMPTY, -1, -1, -1);
}

/* ------------------------------------------------------------- spool edit */

static const char *const k_materials[MMU_MATERIAL_COUNT] = {
    "PLA", "PETG", "ABS", "ASA", "TPU", "PA", "PC", "PLA-CF", "PETG-CF", "PVA",
};

const char *mmu_material_name(int i)
{
    return (i >= 0 && i < MMU_MATERIAL_COUNT) ? k_materials[i] : "";
}

/* ASCII case-insensitive equality: Spoolman keeps a material or a vendor as
 * typed, so "pla" and "PLA" are one choice. */
static bool ci_equal(const char *a, const char *b)
{
    for(; *a && *b; a++, b++) {
        char x = *a, y = *b;

        if(x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if(y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if(x != y) return false;
    }
    return *a == *b;
}

static bool vendor_listed(const mmu_model_t *m, const char *name)
{
    int i;

    for(i = 0; i < (int)m->vendor_count; i++) {
        if(ci_equal(m->vendors[i], name)) return true;
    }
    return false;
}

int mmu_edit_vendor_count(const mmu_model_t *m)
{
    if(!m) return 1;
    return (int)m->vendor_count + (vendor_listed(m, MMU_VENDOR_GENERIC) ? 0 : 1);
}

const char *mmu_edit_vendor_name(const mmu_model_t *m, int i)
{
    if(!m || i < 0 || i >= mmu_edit_vendor_count(m)) return "";
    return i < (int)m->vendor_count ? m->vendors[i] : MMU_VENDOR_GENERIC;
}

void mmu_edit_defaults(const mmu_model_t *m, int unit_idx, int slot_idx, mmu_edit_defaults_t *out)
{
    const mmu_slot_t *s;
    int i, n;

    memset(out, 0, sizeof(*out));
    out->color = -1;
    out->initial_g = 1000;
    out->remaining_g = 1000;
    out->pa_x1000 = 20;
    if(!m || unit_idx < 0 || unit_idx >= (int)m->unit_count) return;
    if(slot_idx < 0 || slot_idx >= (int)m->units[unit_idx].slot_count) return;
    s = &m->units[unit_idx].slots[slot_idx];

    /* material: the bay's own if the list has it, else the bay's own as one
     * more choice, else the first */
    out->material = s->material[0] ? MMU_MATERIAL_COUNT : 0;
    for(i = 0; i < MMU_MATERIAL_COUNT; i++) {
        if(ci_equal(s->material, k_materials[i])) {
            out->material = i;
            break;
        }
    }

    if(s->color_known) out->color = (int32_t)(s->color & 0xFFFFFFu);

    n = mmu_edit_vendor_count(m);
    out->vendor = n;
    for(i = 0; i < n; i++) {
        if(ci_equal(s->brand, mmu_edit_vendor_name(m, i))) {
            out->vendor = i;
            break;
        }
    }
    if(!s->brand[0]) {
        for(i = 0; i < n; i++) {
            if(strcmp(mmu_edit_vendor_name(m, i), MMU_VENDOR_GENERIC) == 0) out->vendor = i;
        }
    }

    /* the host reports the spool's own size when it sends one; without it the
     * size is read back from the grams and the percent left, and a spool that
     * never said either keeps Spoolman's 1000 */
    if(s->initial_g >= 1) out->initial_g = s->initial_g;
    else if(s->remaining_g >= 0 && s->remaining_pct > 0) {
        int v = ((int)s->remaining_g * 100 + s->remaining_pct / 2) / s->remaining_pct;

        if(v >= 1) out->initial_g = v;
    }
    if(s->remaining_g > out->initial_g) out->initial_g = s->remaining_g;
    out->remaining_g = s->remaining_g >= 0 ? s->remaining_g : out->initial_g;

    /* the spool's own pressure advance when the host sends one, else the
     * scenario's, else Spoolman's 0.020 */
    if(s->flow_k_x1000 >= 0) out->pa_x1000 = s->flow_k_x1000;
    else if(s->k_value_x1000 >= 0) out->pa_x1000 = s->k_value_x1000;
}

void mmu_model_edit_result(mmu_model_t *m, const char *refusal)
{
    if(!m) return;
    mmu_copy_text(m->edit_refusal, sizeof(m->edit_refusal), refusal ? refusal : "");
    m->edit_seq++;
}

mmu_action_kind_t mmu_action_kind_of(const char *line)
{
    if(!line) return MMU_ACTION_OTHER;
    if(strncmp(line, "load ", 5) == 0) return MMU_ACTION_LOAD;
    if(strncmp(line, "unload ", 7) == 0) return MMU_ACTION_UNLOAD;
    return MMU_ACTION_OTHER;
}

void mmu_model_action_sent(mmu_model_t *m, mmu_action_kind_t kind)
{
    if(!m || kind == MMU_ACTION_OTHER) return;
    m->action_pending_kind = (uint8_t)kind;
    m->action_pending_seq++;
    m->action_started_kind = MMU_ACTION_OTHER;
}

void mmu_model_action_result(mmu_model_t *m, mmu_action_kind_t kind, bool ok, const char *message)
{
    if(!m) return;
    /* The host showed this one running before it answered. A G-code script is
     * answered only once it has run, so a failure now is the run's own
     * outcome, which the host's status already shows, or the display giving
     * up the wait (a timeout, a dropped link): never a refusal to start. */
    if(kind != MMU_ACTION_OTHER && kind == m->action_started_kind) {
        m->action_started_kind = MMU_ACTION_OTHER;
        if(!ok) return;
    }
    mmu_copy_text(m->action_refusal, sizeof(m->action_refusal), ok ? "" : (message ? message : ""));
    m->action_refusal_kind = (uint8_t)kind;
    m->action_seq++;
    /* The host has answered this one, so it is no longer a load the load
     * screen is waiting to hear about - taken or refused: a G-code script is
     * answered once it has run, so a taken unload is already over. */
    if(kind == m->action_pending_kind) m->action_pending_kind = MMU_ACTION_OTHER;
}

/* ------------------------------------------------------------------ groups */

/* Scenario bootstrap only: build the group table directly (no logging, no
 * donor-removal), unlike mmu_group_assign() which is the runtime, user-facing
 * version of the same edit. */
static int group_add(const char *name)
{
    mmu_group_t *g;

    if(g_mmu.group_count >= MMU_MAX_GROUPS) return -1;
    g = &g_mmu.groups[g_mmu.group_count];
    memset(g, 0, sizeof(*g));
    mmu_copy_text(g->name, sizeof(g->name), name);
    return g_mmu.group_count++;
}

static void group_member(int group, int unit, int slot)
{
    mmu_group_t *g;

    if(group < 0 || group >= (int)g_mmu.group_count) return;
    g = &g_mmu.groups[group];
    if(g->count >= MMU_MAX_GROUP_MEMBERS) return;
    g->members[g->count].unit = (int8_t)unit;
    g->members[g->count].slot = (int8_t)slot;
    g->count++;
}

static mmu_unit_t *unit_add(const char *name, mmu_unit_kind_t kind, int slot_count,
                            mmu_side_t side)
{
    mmu_unit_t *u = &g_mmu.units[g_mmu.unit_count];
    int i;

    memset(u, 0, sizeof(*u));
    mmu_copy_text(u->name, sizeof(u->name), name);
    u->kind = kind;
    u->slot_count = (uint8_t)slot_count;
    u->side = side;
    u->connected = true;
    u->humidity_level = -1;
    u->humidity_pct = -1;
    u->temp_c = -100;
    u->ambient_c = 24;
    u->normal_motion = true;
    u->follower_uncertain = false;
    u->host_idx = -1;   /* the dummy scenarios have no host object; lane stays "" */
    u->toolhead = 0;    /* the demo has one toolhead (id ""), shared by every unit */

    for(i = 0; i < slot_count; i++) slot_empty(&u->slots[i]);

    g_mmu.unit_count++;
    return u;
}

/* Sentinel defaults for a toolhead, the same "-1 = none/unknown" convention
 * the rest of the model uses. */
static void toolhead_reset(mmu_toolhead_t *th, const char *id)
{
    memset(th, 0, sizeof(*th));
    mmu_copy_text(th->id, sizeof(th->id), id);
    th->loaded_unit = -1;
    th->loaded_slot = -1;
    th->loaded_ext = -1;
    th->busy_unit = -1;
    th->busy_slot = -1;
    th->step_current = -1;
    th->step_failed = -1;
    th->current_group = -1;
    th->error_unit = -1;
    th->error_slot = -1;
    th->runout_from_unit = -1;
    th->runout_from_slot = -1;
    th->runout_to_unit = -1;
    th->runout_to_slot = -1;
    th->print_unit = -1;
    th->print_slot = -1;
    th->motion_settled = true;
    th->pressure = -1;
    th->set_point = -1;
    th->band_low = -1;
    th->band_high = -1;
}

static void ext_add(mmu_side_t side, uint32_t color, const char *material)
{
    mmu_ext_spool_t *e = &g_mmu.ext_spools[g_mmu.ext_spool_count];

    memset(e, 0, sizeof(*e));
    e->present = true;
    e->side = side;
    e->color = color;
    mmu_copy_text(e->material, sizeof(e->material), material);
    g_mmu.ext_spool_count++;
}

static void alert_add(mmu_severity_t sev, int unit, const char *text, bool unread)
{
    mmu_alert_t *a;

    if(g_mmu.alert_count >= MMU_MAX_ALERTS) return;
    a = &g_mmu.alerts[g_mmu.alert_count];
    memset(a, 0, sizeof(*a));
    a->severity = sev;
    a->unit = (int8_t)unit;
    a->toolhead = -1;                        /* the demo's alerts name a unit or nothing, never a lane */
    a->lane_fault = false;
    a->unread = unread;
    a->raised_ms = g_mmu.now_ms;
    a->read_ms = unread ? 0 : g_mmu.now_ms;
    mmu_copy_text(a->text, sizeof(a->text), text);
    g_mmu.alert_count++;
    if(unread) g_mmu.alert_unread++;
}

static void set_steps(const char *const *list, int count)
{
    int i;

    if(count > MMU_MAX_STEPS) count = MMU_MAX_STEPS;
    memset(g_mmu.steps, 0, sizeof(g_mmu.steps));
    for(i = 0; i < count; i++) {
        mmu_copy_text(g_mmu.steps[i], sizeof(g_mmu.steps[i]), list[i]);
    }
    g_mmu.step_count = (uint8_t)count;
}

/* --------------------------------------------------------------- scenarios */

/* Shared starting point: two four-slot units (one with %RH + degC and a drier,
 * one with the A-E level), one HT single-slot unit and an external spool. */
static void scenario_base(void)
{
    mmu_unit_t *a;
    mmu_unit_t *b;
    mmu_unit_t *ht;

    memset(&g_mmu, 0, sizeof(g_mmu));
    g_mmu.motion_settled = true;
    g_mmu.spoolman_online = true;
    mmu_settings_defaults(&g_mmu.settings);
    g_mmu.loaded_unit = -1;
    g_mmu.loaded_slot = -1;
    g_mmu.loaded_ext = -1;
    g_mmu.busy_unit = -1;
    g_mmu.busy_slot = -1;
    g_mmu.step_current = -1;
    g_mmu.step_failed = -1;
    g_mmu.print_unit = -1;
    g_mmu.print_slot = -1;
    g_mmu.error_unit = -1;
    g_mmu.error_slot = -1;
    g_mmu.runout_from_unit = -1;
    g_mmu.runout_from_slot = -1;
    g_mmu.runout_to_unit = -1;
    g_mmu.runout_to_slot = -1;
    memset(s_dry_phase, 0, sizeof(s_dry_phase));

    /* The demo's one toolhead (id ""), shared by every unit (unit_add()). */
    g_mmu.toolhead_count = 1;
    toolhead_reset(&g_mmu.toolheads[0], "");

    /* --- unit A: AMS 2 Pro style, humidity in %RH, has a drier ------------ */
    a = unit_add("A", MMU_UNIT_4SLOT, 4, MMU_SIDE_NONE);
    a->variant = MMU_VARIANT_AMS2_PRO;
    a->has_humidity_pct = true;
    a->has_temp = true;
    a->can_dry = true;
    a->rfid = true;
    a->humidity_pct = 31;
    a->temp_c = 24;
    a->dry_state = MMU_DRY_IDLE;
    a->dry_target_c = 45;
    a->power_adapter = true;
    a->has_power_adapter = true;
    slot_set(&a->slots[0], 0xD23B3B, "PLA", "Bambu", 82, MMU_INFO_TAG,
             MMU_SLOT_LOADED, 190, 230, 20);
    slot_set(&a->slots[1], 0x22262B, "PETG-CF", "Generic", 45, MMU_INFO_MANUAL,
             MMU_SLOT_READY, 230, 260, 15);
    slot_empty(&a->slots[2]);
    slot_set(&a->slots[3], 0x000000, "", "", -1, MMU_INFO_UNKNOWN,
             MMU_SLOT_READY, -1, -1, -1);

    /* --- unit B: AMS 1 style, %RH + temperature, no heater/drying --------- */
    b = unit_add("B", MMU_UNIT_4SLOT, 4, MMU_SIDE_NONE);
    b->variant = MMU_VARIANT_AMS;
    b->has_humidity_pct = true;
    b->has_temp = true;
    b->rfid = true;
    b->humidity_pct = 34;
    b->temp_c = 25;
    b->ambient_c = 24;
    slot_set(&b->slots[0], 0xE08A30, "ASA", "Generic", 12, MMU_INFO_MANUAL,
             MMU_SLOT_READY, 240, 270, 12);
    /* B2: the same PLA red as A1, the infinite-spooling twin of group T0. */
    slot_set(&b->slots[1], 0xD23B3B, "PLA", "Bambu", 95, MMU_INFO_TAG,
             MMU_SLOT_READY, 190, 230, 20);
    slot_set(&b->slots[2], 0x8A9099, "TPU", "Generic", 70, MMU_INFO_MANUAL,
             MMU_SLOT_READY, 200, 230, 8);
    slot_set(&b->slots[3], 0x9B2F2F, "PLA", "Generic", 40, MMU_INFO_TAG,
             MMU_SLOT_ERROR, 190, 230, 22);

    /* --- unit HT-A: single slot high-temp drier --------------------------- */
    ht = unit_add("HT-A", MMU_UNIT_HT, 1, MMU_SIDE_NONE);
    ht->variant = MMU_VARIANT_AMS_HT;
    ht->has_humidity_pct = true;
    ht->has_temp = true;
    ht->can_dry = true;
    ht->rfid = true;
    ht->humidity_pct = 12;
    ht->temp_c = 28;
    ht->dry_state = MMU_DRY_IDLE;
    ht->dry_target_c = 55;
    ht->power_adapter = true;
    ht->has_power_adapter = true;
    slot_set(&ht->slots[0], 0x2F6FB2, "PA", "Bambu", 88, MMU_INFO_TAG,
             MMU_SLOT_READY, 260, 290, 25);

    ext_add(MMU_SIDE_NONE, 0x2F9EC4, "PETG");

    g_mmu.loaded = true;
    g_mmu.loaded_unit = 0;
    g_mmu.loaded_slot = 0;
    g_mmu.toolhead_sensor = true;

    /* The OpenAMS sample: one FPS lane, every unit on it.
     * T0 = [A1, B2] (the infinite-spooling pair), T1 = [A2], T2 = [B1],
     * T3 = [B3, B4], T4 = [HT-A1]. A3 and A4 stay unassigned. */
    {
        int t0 = group_add("T0");
        int t1 = group_add("T1");
        int t2 = group_add("T2");
        int t3 = group_add("T3");
        int t4 = group_add("T4");

        group_member(t0, 0, 0);   /* A1 */
        group_member(t0, 1, 1);   /* B2 */
        group_member(t1, 0, 1);   /* A2 */
        group_member(t2, 1, 0);   /* B1 */
        group_member(t3, 1, 2);   /* B3 */
        group_member(t3, 1, 3);   /* B4 */
        group_member(t4, 2, 0);   /* HT-A1 */
    }
}

static void scenario_idle(void)
{
    scenario_base();
    alert_add(MMU_SEV_INFO, 0, "A1: filament info read from tag", false);
    alert_add(MMU_SEV_INFO, 1, "B3: manual filament info is used", false);
}

static void scenario_loading(void)
{
    static const char *const steps[] = {
        "heat",
        "cut",
        "retract",
        "feed",
        "purge",
    };

    scenario_base();
    /* A3 gets a fresh spool while A1 is still in the toolhead. */
    slot_set(&g_mmu.units[0].slots[2], 0xD23B3B, "PLA", "Bambu", 82,
             MMU_INFO_TAG, MMU_SLOT_READY, 190, 230, 20);
    g_mmu.busy = MMU_BUSY_LOAD;
    g_mmu.busy_unit = 0;
    g_mmu.busy_slot = 2;
    set_steps(steps, 5);
    g_mmu.step_current = 0;
    alert_add(MMU_SEV_INFO, 0, "Loading A3 (PLA, red)", false);
}

static void scenario_error(void)
{
    scenario_loading();

    g_mmu.step_current = 3;
    g_mmu.step_failed = 3;
    g_mmu.has_error = true;
    g_mmu.error_severity = MMU_SEV_PAUSE;
    snprintf(g_mmu.error_code, sizeof(g_mmu.error_code), "0700-8003");
    snprintf(g_mmu.error_text, sizeof(g_mmu.error_text),
             "AMS A slot 3 failed to send out PLA (red). The extruder might be clogged.");
    g_mmu.error_unit = 0;
    g_mmu.error_slot = 2;
    alert_add(MMU_SEV_PAUSE, 0, "A3: failed to send out PLA (red)", true);
}

static void scenario_drying(void)
{
    scenario_base();

    g_mmu.units[0].dry_state = MMU_DRY_HEATING;
    g_mmu.units[0].dry_target_c = 55;
    g_mmu.units[0].dry_remaining_min = 720;   /* 12 h */
    g_mmu.units[0].dry_rotate = true;
    alert_add(MMU_SEV_INFO, 0, "A: drying started, 12 h at 55C", false);
}

static void scenario_printing(void)
{
    static const char *const steps[] = {
        "heat",
        "cut",
        "retract",
        "feed",
        "purge",
    };

    scenario_base();

    /* printing, with a filament change running at the same time */
    g_mmu.printing = true;
    g_mmu.print_unit = 0;
    g_mmu.print_slot = 1;
    g_mmu.filament_changes = 7;
    g_mmu.loaded_unit = 0;
    g_mmu.loaded_slot = 1;
    g_mmu.units[0].slots[0].state = MMU_SLOT_READY;
    g_mmu.units[0].slots[1].state = MMU_SLOT_LOADED;

    g_mmu.busy = MMU_BUSY_LOAD;
    g_mmu.busy_unit = 0;
    g_mmu.busy_slot = 3;
    set_steps(steps, 5);
    g_mmu.step_current = 2;
    alert_add(MMU_SEV_INFO, -1, "Printing, change 7 of 12", false);
}

static void scenario_runout(void)
{
    scenario_base();

    /* A1 ran out; B2, its group T0 twin, took over. Both are already T0
     * members from scenario_base(); only their state/remaining change. */
    slot_set(&g_mmu.units[0].slots[0], 0xD23B3B, "PLA", "Bambu", 0,
             MMU_INFO_TAG, MMU_SLOT_READY, 190, 230, 20);
    slot_set(&g_mmu.units[1].slots[1], 0xD23B3B, "PLA", "Bambu", 95,
             MMU_INFO_TAG, MMU_SLOT_LOADED, 190, 230, 20);

    g_mmu.loaded = true;
    g_mmu.loaded_unit = 1;
    g_mmu.loaded_slot = 1;
    g_mmu.loaded_ext = -1;
    g_mmu.toolhead_sensor = true;

    g_mmu.printing = true;
    g_mmu.print_unit = 1;
    g_mmu.print_slot = 1;
    g_mmu.filament_changes = 3;

    g_mmu.runout_active = true;
    snprintf(g_mmu.runout_material, sizeof(g_mmu.runout_material), "PLA");
    snprintf(g_mmu.runout_note, sizeof(g_mmu.runout_note),
             "A1 ran out, B2 took over");
    g_mmu.runout_from_unit = 0;
    g_mmu.runout_from_slot = 0;
    g_mmu.runout_to_unit = 1;
    g_mmu.runout_to_slot = 1;

    alert_add(MMU_SEV_INFO, 0, "Auto-refill: A1 -> B2 (PLA red)", true);
    alert_add(MMU_SEV_INFO, 1, "Auto-refill: A1 -> B2 (PLA red)", true);
}

static void scenario_dual(void)
{
    mmu_unit_t *l;
    mmu_unit_t *r;

    scenario_base();

    /* Rebuild as a dual-extruder setup: one unit per side, one spool per side. */
    g_mmu.dual_extruder = true;
    g_mmu.ext_spool_count = 0;
    g_mmu.unit_count = 0;

    l = unit_add("A", MMU_UNIT_4SLOT, 4, MMU_SIDE_LEFT);
    l->variant = MMU_VARIANT_AMS2_PRO;
    l->has_humidity_pct = true;
    l->has_temp = true;
    l->can_dry = true;
    l->rfid = true;
    l->humidity_pct = 28;
    l->temp_c = 25;
    l->dry_state = MMU_DRY_IDLE;
    l->power_adapter = true;
    l->has_power_adapter = true;
    slot_set(&l->slots[0], 0xD23B3B, "PLA", "Bambu", 82, MMU_INFO_TAG,
             MMU_SLOT_LOADED, 190, 230, 20);
    slot_set(&l->slots[1], 0x22262B, "PETG-CF", "Generic", 45, MMU_INFO_MANUAL,
             MMU_SLOT_READY, 230, 260, 15);
    slot_empty(&l->slots[2]);
    slot_empty(&l->slots[3]);

    r = unit_add("B", MMU_UNIT_4SLOT, 4, MMU_SIDE_RIGHT);
    r->variant = MMU_VARIANT_AMS2_PRO;
    r->has_humidity_pct = true;
    r->has_temp = true;
    r->can_dry = true;
    r->rfid = true;
    r->humidity_pct = 34;
    r->temp_c = 26;
    r->dry_state = MMU_DRY_IDLE;
    r->power_adapter = false;
    r->has_power_adapter = true;
    mmu_copy_text(r->cannot_dry_reason, sizeof(r->cannot_dry_reason),
             "No power adapter connected");
    slot_set(&r->slots[0], 0xE8E8E8, "PETG", "Prusament", 61, MMU_INFO_TAG,
             MMU_SLOT_READY, 220, 250, 30);
    slot_set(&r->slots[1], 0x8A9099, "TPU", "Generic", 70, MMU_INFO_MANUAL,
             MMU_SLOT_READY, 200, 230, 8);
    slot_set(&r->slots[2], 0x6A3FA0, "ABS", "Generic", 33, MMU_INFO_MANUAL,
             MMU_SLOT_READY, 240, 270, 14);
    slot_empty(&r->slots[3]);

    ext_add(MMU_SIDE_LEFT, 0x2F9EC4, "PETG");
    ext_add(MMU_SIDE_RIGHT, 0xE08A30, "ASA");

    g_mmu.loaded = true;
    g_mmu.loaded_unit = 0;
    g_mmu.loaded_slot = 0;
    g_mmu.toolhead_sensor = true;
    alert_add(MMU_SEV_INFO, -1, "Dual extruder: A is on the left, B on the right", false);

    /* scenario_base() built groups for the units it made, which dual just
     * overwrote; rebuild from scratch so every group stays within one side,
     * as the FPS lane rule requires. */
    g_mmu.group_count = 0;
    {
        int t0 = group_add("T0");
        int t1 = group_add("T1");
        int t2 = group_add("T2");
        int t3 = group_add("T3");
        int t4 = group_add("T4");

        group_member(t0, 0, 0);   /* left A1, loaded */
        group_member(t1, 0, 1);   /* left A2 */
        group_member(t2, 1, 0);   /* right B1 */
        group_member(t3, 1, 1);   /* right B2 */
        group_member(t4, 1, 2);   /* right B3 */
    }
}

/* A wide unit: eight bays where an AMS has four, so the home screen has to
 * page (ui_chevron.c). The loaded spool sits on the second page, which is
 * where the screen therefore opens; unit B and the HT unit stay so the other
 * screens keep the layout they always had. */
static void scenario_lanes(void)
{
    mmu_unit_t *a;
    int t6;
    int i;

    scenario_base();

    /* Unit A becomes a lane MMU of eight bays - a BoxTurtle, an ERCF, a
     * Tradrack. Its humidity, temperature and dryer answers are left as
     * scenario_base() set them: they are not what this scenario is about, and
     * changing them would put every other screen's layout up for audit too. */
    a = &g_mmu.units[0];
    a->kind = MMU_UNIT_LANE;
    a->variant = MMU_VARIANT_LANE;
    a->slot_count = 8;
    for(i = 4; i < 8; i++) slot_empty(&a->slots[i]);

    /* A1..A4 are scenario_base()'s (the red T0, PETG-CF, an empty bay and an
     * unidentified one); A1 is no longer the loaded spool. */
    slot_set(&a->slots[0], 0xD23B3B, "PLA", "Bambu", 82, MMU_INFO_TAG,
             MMU_SLOT_READY, 190, 230, 20);

    /* A5..A8: the second page. A5 is loaded, so the filament path drops from
     * the page's first column. */
    slot_set(&a->slots[4], 0x2F6FB2, "ABS", "Polymaker", 64, MMU_INFO_TAG,
             MMU_SLOT_LOADED, 260, 290, 25);
    slot_set(&a->slots[5], 0x2F9EC4, "PETG", "Generic", 78, MMU_INFO_MANUAL,
             MMU_SLOT_READY, 230, 260, 20);
    slot_set(&a->slots[6], 0x8A9099, "TPU", "Generic", 40, MMU_INFO_MANUAL,
             MMU_SLOT_READY, 200, 230, 8);
    slot_set(&a->slots[7], 0xE08A30, "ASA", "Generic", 12, MMU_INFO_MANUAL,
             MMU_SLOT_READY, 240, 270, 12);

    /* A group per tool on the second page, and the spare pair A6/A7, so every
     * tile there wears its own tool. A3 (no spool), A4 (unidentified) and
     * nothing else stay in no group: their tiles are the "?" one. */
    t6 = group_add("T6");
    group_member(group_add("T5"), 0, 4);
    group_member(t6, 0, 5);
    group_member(t6, 0, 6);
    group_member(group_add("T7"), 0, 7);

    g_mmu.loaded = true;
    g_mmu.loaded_unit = 0;
    g_mmu.loaded_slot = 4;       /* page 2, column 0 */
    g_mmu.toolhead_sensor = true;
}

/* A remote fault: the display is mounted on unit A while unit B is loading a
 * slot, printing from another and sitting on a paused error. */
static void scenario_remote(void)
{
    static const char *const steps[] = {
        "heat",
        "cut",
        "retract",
        "feed",
        "purge",
    };

    scenario_base();

    /* B2 (group T0, per scenario_base()) is what the print runs from; A1,
     * its group twin, is no longer the loaded spool. */
    slot_set(&g_mmu.units[0].slots[0], 0xD23B3B, "PLA", "Bambu", 82,
             MMU_INFO_TAG, MMU_SLOT_READY, 190, 230, 20);
    slot_set(&g_mmu.units[1].slots[1], 0xE8E8E8, "PETG", "Prusament", 95,
             MMU_INFO_TAG, MMU_SLOT_LOADED, 220, 250, 30);

    g_mmu.loaded = true;
    g_mmu.loaded_unit = 1;
    g_mmu.loaded_slot = 1;
    g_mmu.loaded_ext = -1;
    g_mmu.toolhead_sensor = true;

    g_mmu.printing = true;
    g_mmu.print_unit = 1;
    g_mmu.print_slot = 1;
    g_mmu.filament_changes = 4;

    /* a load of B3 is running on the other unit */
    g_mmu.busy = MMU_BUSY_LOAD;
    g_mmu.busy_unit = 1;
    g_mmu.busy_slot = 2;
    set_steps(steps, 5);
    g_mmu.step_current = 1;

    /* and B4 is sitting on a paused tangle */
    g_mmu.has_error = true;
    g_mmu.error_severity = MMU_SEV_PAUSE;
    snprintf(g_mmu.error_code, sizeof(g_mmu.error_code), "0700-8012");
    snprintf(g_mmu.error_text, sizeof(g_mmu.error_text),
             "AMS B slot 4: filament tangled. Untangle it and retry.");
    g_mmu.error_unit = 1;
    g_mmu.error_slot = 3;

    alert_add(MMU_SEV_PAUSE, 1, "B4: filament tangled", true);
}

/* -------------------------------------------------------------- public API */

void mmu_model_set_scenario(const char *name)
{
    int i;
    const char *chosen = mmu_scenario_names[0];

    if(name) {
        for(i = 0; i < MMU_SCENARIO_COUNT; i++) {
            if(strcmp(name, mmu_scenario_names[i]) == 0) {
                chosen = mmu_scenario_names[i];
                break;
            }
        }
    }

    if(strcmp(chosen, "loading") == 0)        scenario_loading();
    else if(strcmp(chosen, "error") == 0)     scenario_error();
    else if(strcmp(chosen, "drying") == 0)    scenario_drying();
    else if(strcmp(chosen, "printing") == 0)  scenario_printing();
    else if(strcmp(chosen, "runout") == 0)    scenario_runout();
    else if(strcmp(chosen, "dual") == 0)      scenario_dual();
    else if(strcmp(chosen, "remote") == 0)    scenario_remote();
    else if(strcmp(chosen, "lanes") == 0)     scenario_lanes();
    else                                      scenario_idle();

    /* The scenario just rebuilt the whole model, so restore the binding by the
     * remembered unit name. When the scenario has no such unit, fall back to
     * unit 0 but keep the name so switching back restores the old binding. */
    g_mmu.this_unit = 0;
    if(g_mmu.unit_count > 0) {
        if(s_this_name[0] == '\0') {
            mmu_copy_text(s_this_name, sizeof(s_this_name), g_mmu.units[0].name);
        }
        else {
            for(i = 0; i < g_mmu.unit_count; i++) {
                if(strcmp(g_mmu.units[i].name, s_this_name) == 0) {
                    g_mmu.this_unit = (int8_t)i;
                    break;
                }
            }
        }
    }

    g_mmu.scenario_name = chosen;
    g_mmu.tick = 0;

    g_mmu.current_group = (g_mmu.loaded_unit >= 0 && g_mmu.loaded_slot >= 0)
                          ? (int8_t)mmu_slot_group(g_mmu.loaded_unit, g_mmu.loaded_slot)
                          : -1;

    sync_motion_settled();
    mmu_model_capture_this(&g_mmu);
}

void mmu_model_init(void)
{
    mmu_model_set_scenario("idle");
}

void mmu_settings_defaults(mmu_settings_t *s)
{
    memset(s, 0, sizeof(*s));
    s->known = false;
    s->read_tag_on_insertion = true;
    s->read_tags_at_startup = true;
    s->auto_reload_on_runout = true;
    s->auto_create_spoolman_spools = true;
    s->apply_pa_on_load = false;
}

void mmu_model_set_live(bool on)
{
    s_live = on;
}

bool mmu_model_is_live(void)
{
    return s_live;
}

static void busy_finish(void)
{
    if(g_mmu.busy == MMU_BUSY_LOAD) {
        if(g_mmu.loaded_unit >= 0 && g_mmu.loaded_slot >= 0) {
            mmu_slot_t *old = &g_mmu.units[g_mmu.loaded_unit].slots[g_mmu.loaded_slot];
            if(old->state == MMU_SLOT_LOADED) old->state = MMU_SLOT_READY;
        }
        if(g_mmu.busy_unit >= 0 && g_mmu.busy_slot >= 0) {
            g_mmu.units[g_mmu.busy_unit].slots[g_mmu.busy_slot].state = MMU_SLOT_LOADED;
            g_mmu.loaded = true;
            g_mmu.loaded_unit = g_mmu.busy_unit;
            g_mmu.loaded_slot = g_mmu.busy_slot;
            g_mmu.loaded_ext = -1;
            g_mmu.toolhead_sensor = true;
            g_mmu.current_group = (int8_t)mmu_slot_group(g_mmu.loaded_unit, g_mmu.loaded_slot);
        }
    }
    else if(g_mmu.busy == MMU_BUSY_UNLOAD) {
        if(g_mmu.loaded_unit >= 0 && g_mmu.loaded_slot >= 0) {
            mmu_slot_t *old = &g_mmu.units[g_mmu.loaded_unit].slots[g_mmu.loaded_slot];
            if(old->state == MMU_SLOT_LOADED) old->state = MMU_SLOT_READY;
        }
        g_mmu.loaded = false;
        g_mmu.loaded_unit = -1;
        g_mmu.loaded_slot = -1;
        g_mmu.toolhead_sensor = false;
        g_mmu.current_group = -1;
    }

    g_mmu.busy = MMU_BUSY_NONE;
    g_mmu.busy_unit = -1;
    g_mmu.busy_slot = -1;
    g_mmu.step_current = -1;
    g_mmu.step_failed = -1;
    g_mmu.step_detail[0] = '\0';
    sync_motion_settled();
    mmu_model_capture_this(&g_mmu);
}

void mmu_model_tick(void)
{
    int i;

    g_mmu.tick++;

    if(g_mmu.busy != MMU_BUSY_NONE && g_mmu.step_count > 0 &&
       g_mmu.step_current >= 0 && g_mmu.step_failed < 0) {
        if(g_mmu.tick % TICKS_PER_STEP == 0) {
            g_mmu.step_current++;
            if(g_mmu.step_current >= (int8_t)g_mmu.step_count) busy_finish();
        }
    }

    for(i = 0; i < g_mmu.unit_count; i++) {
        mmu_unit_t *u = &g_mmu.units[i];

        switch(u->dry_state) {
            case MMU_DRY_HEATING:
                if(u->has_temp && u->temp_c < u->dry_target_c) u->temp_c++;
                if(++s_dry_phase[i] >= TICKS_DRY_PHASE) {
                    s_dry_phase[i] = 0;
                    u->dry_state = MMU_DRY_DEHUMIDIFYING;
                }
                break;
            case MMU_DRY_DEHUMIDIFYING:
                if(u->dry_remaining_min > 0) {
                    if(g_mmu.tick % SECONDS_PER_MIN == 0) u->dry_remaining_min--;
                }
                else {
                    u->dry_state = MMU_DRY_COOLING;
                    s_dry_phase[i] = 0;
                }
                break;
            case MMU_DRY_COOLING:
                if(u->has_temp) {
                    if(u->temp_c > u->ambient_c) u->temp_c--;
                    if(u->temp_c <= u->ambient_c + 5) {
                        u->dry_state = MMU_DRY_IDLE;
                        u->dry_target_c = 0;
                    }
                }
                else if(++s_dry_phase[i] >= TICKS_DRY_PHASE) {
                    s_dry_phase[i] = 0;
                    u->dry_state = MMU_DRY_IDLE;
                    u->dry_target_c = 0;
                }
                break;
            default:
                s_dry_phase[i] = 0;
                break;
        }
    }
}

/* ---------------------------------------------------------------- helpers */

const char *mmu_unit_name(int unit_idx)
{
    if(unit_idx < 0 || unit_idx >= g_mmu.unit_count) return "?";
    return g_mmu.units[unit_idx].name;
}

const char *mmu_variant_name(mmu_variant_t v)
{
    switch(v) {
        case MMU_VARIANT_AMS:      return "AMS";
        case MMU_VARIANT_AMS2_PRO: return "AMS 2 Pro";
        case MMU_VARIANT_AMS_HT:   return "AMS HT";
        case MMU_VARIANT_LANE:     return "Lane MMU";
    }
    return "AMS";
}

const char *mmu_unit_title(const mmu_unit_t *u)
{
    if(!u) return "";
    if(u->title[0]) return u->title;
    return mmu_variant_name(u->variant);
}

void mmu_slot_id_in(const mmu_model_t *m, int unit_idx, int slot_idx, char *out, size_t out_len)
{
    const char *name = (unit_idx < 0 || unit_idx >= (int)m->unit_count) ? "?"
                                                                       : m->units[unit_idx].name;
    char bay[12];

    if(out_len == 0) return;
    /* the unit's name is the host's, and the number goes after it: both halves
     * are written through the character-boundary writer, so a multi-byte name
     * is never split by the digit that follows it */
    snprintf(bay, sizeof bay, "%d", slot_idx + 1);
    mmu_copy_text(out, out_len, name);
    mmu_append_text(out, out_len, bay);
}

void mmu_slot_id(int unit_idx, int slot_idx, char *out, size_t out_len)
{
    mmu_slot_id_in(&g_mmu, unit_idx, slot_idx, out, out_len);
}

const char *mmu_slot_display_material(const mmu_slot_t *slot)
{
    if(!slot) return "";
    if(slot->state == MMU_SLOT_EMPTY) return "";
    if(slot->material[0] == '\0') return "?";
    return slot->material;
}

bool mmu_unit_is_slot_loaded(int unit_idx, int slot_idx)
{
    return g_mmu.loaded && g_mmu.loaded_ext < 0 &&
           g_mmu.loaded_unit == unit_idx && g_mmu.loaded_slot == slot_idx;
}

bool mmu_unit_is_slot_busy(int unit_idx, int slot_idx)
{
    return g_mmu.busy != MMU_BUSY_NONE &&
           g_mmu.busy_unit == unit_idx && g_mmu.busy_slot == slot_idx;
}

/* ------------------------------------------------------------- this unit */

int mmu_this_unit(void)
{
    int i = g_mmu.this_unit;

    if(i < 0 || i >= (int)g_mmu.unit_count) return 0;
    return i;
}

bool mmu_is_this_unit(int unit_idx)
{
    if(unit_idx < 0 || unit_idx >= (int)g_mmu.unit_count) return false;
    return unit_idx == mmu_this_unit();
}

mmu_unit_t *mmu_this(void)
{
    return &g_mmu.units[mmu_this_unit()];
}

void mmu_set_this_unit(int unit_idx)
{
    if(unit_idx < 0 || unit_idx >= (int)g_mmu.unit_count) return;
    g_mmu.this_unit = (int8_t)unit_idx;
    mmu_copy_text(s_this_name, sizeof(s_this_name), g_mmu.units[unit_idx].name);
}

bool mmu_set_this_unit_by_name(const char *name)
{
    char key[MMU_UNIT_NAME_LEN];
    int i;

    if(!name) return false;
    mmu_copy_text(key, sizeof key, name);   /* the stored names are cut the same way */
    for(i = 0; i < (int)g_mmu.unit_count; i++) {
        if(strcmp(g_mmu.units[i].name, key) == 0) {
            mmu_set_this_unit(i);
            return true;
        }
    }
    return false;
}

/* -------------------------------------------------------------- toolheads */

const mmu_toolhead_t *mmu_model_toolhead(const mmu_model_t *m, int idx)
{
    if(!m || idx < 0 || idx >= (int)m->toolhead_count) return NULL;
    return &m->toolheads[idx];
}

mmu_toolhead_t *mmu_this_toolhead(void)
{
    int idx = g_mmu.units[mmu_this_unit()].toolhead;

    if(idx < 0 || idx >= (int)g_mmu.toolhead_count) return NULL;
    return &g_mmu.toolheads[idx];
}

/* The toolhead of m->units[m->this_unit], or NULL when there is none. Shared
 * by mmu_model_refresh_this() and mmu_model_capture_this(): everything about
 * finding the right toolhead is identical, only the copy direction differs. */
static mmu_toolhead_t *this_toolhead_in(mmu_model_t *m)
{
    int idx;

    if(!m) return NULL;
    if(m->this_unit < 0 || m->this_unit >= (int)m->unit_count) return NULL;
    idx = m->units[m->this_unit].toolhead;
    if(idx < 0 || idx >= (int)m->toolhead_count) return NULL;
    return &m->toolheads[idx];
}

void mmu_model_refresh_this(mmu_model_t *m)
{
    mmu_toolhead_t *th = this_toolhead_in(m);

    if(!th) return;

    m->loaded = th->loaded;
    m->loaded_unit = th->loaded_unit;
    m->loaded_slot = th->loaded_slot;
    m->loaded_ext = th->loaded_ext;
    m->toolhead_sensor = th->toolhead_sensor;

    m->busy = th->busy;
    m->busy_unit = th->busy_unit;
    m->busy_slot = th->busy_slot;
    /* The host reports the operation: what the display sent is no longer
     * pending. Left set, the load screen went back to "Starting" when the
     * unload it had just shown finished, and then said the host never
     * reported it. It is running now, which is what an answer that comes
     * later has to be read against. */
    if(m->busy != MMU_BUSY_NONE && m->action_pending_kind != MMU_ACTION_OTHER) {
        m->action_started_kind = m->action_pending_kind;
        m->action_pending_kind = MMU_ACTION_OTHER;
    }
    memcpy(m->steps, th->steps, sizeof(m->steps));
    m->step_count = th->step_count;
    m->step_current = th->step_current;
    m->step_failed = th->step_failed;
    memcpy(m->step_detail, th->step_detail, sizeof(m->step_detail));

    m->current_group = th->current_group;

    m->has_error = th->has_error;
    m->error_severity = th->error_severity;
    memcpy(m->error_code, th->error_code, sizeof(m->error_code));
    memcpy(m->error_text, th->error_text, sizeof(m->error_text));
    m->error_unit = th->error_unit;
    m->error_slot = th->error_slot;
    memcpy(m->error_act, th->error_act, sizeof(m->error_act));
    m->error_act_count = th->error_act_count;
    m->error_count = th->error_count;

    m->motion_settled = th->motion_settled;

    m->runout_active = th->runout_active;
    memcpy(m->runout_material, th->runout_material, sizeof(m->runout_material));
    memcpy(m->runout_note, th->runout_note, sizeof(m->runout_note));
    m->runout_from_unit = th->runout_from_unit;
    m->runout_from_slot = th->runout_from_slot;
    m->runout_to_unit = th->runout_to_unit;
    m->runout_to_slot = th->runout_to_slot;

    m->print_unit = th->print_unit;
    m->print_slot = th->print_slot;
    m->filament_changes = th->filament_changes;
}

void mmu_model_capture_this(mmu_model_t *m)
{
    mmu_toolhead_t *th = this_toolhead_in(m);

    if(!th) return;

    th->loaded = m->loaded;
    th->loaded_unit = m->loaded_unit;
    th->loaded_slot = m->loaded_slot;
    th->loaded_ext = m->loaded_ext;
    th->toolhead_sensor = m->toolhead_sensor;

    th->busy = m->busy;
    th->busy_unit = m->busy_unit;
    th->busy_slot = m->busy_slot;
    memcpy(th->steps, m->steps, sizeof(th->steps));
    th->step_count = m->step_count;
    th->step_current = m->step_current;
    th->step_failed = m->step_failed;
    memcpy(th->step_detail, m->step_detail, sizeof(th->step_detail));

    th->current_group = m->current_group;

    th->has_error = m->has_error;
    th->error_severity = m->error_severity;
    memcpy(th->error_code, m->error_code, sizeof(th->error_code));
    memcpy(th->error_text, m->error_text, sizeof(th->error_text));
    th->error_unit = m->error_unit;
    th->error_slot = m->error_slot;
    memcpy(th->error_act, m->error_act, sizeof(th->error_act));
    th->error_act_count = m->error_act_count;
    th->error_count = m->error_count;

    th->motion_settled = m->motion_settled;

    th->runout_active = m->runout_active;
    memcpy(th->runout_material, m->runout_material, sizeof(th->runout_material));
    memcpy(th->runout_note, m->runout_note, sizeof(th->runout_note));
    th->runout_from_unit = m->runout_from_unit;
    th->runout_from_slot = m->runout_from_slot;
    th->runout_to_unit = m->runout_to_unit;
    th->runout_to_slot = m->runout_to_slot;

    th->print_unit = m->print_unit;
    th->print_slot = m->print_slot;
    th->filament_changes = m->filament_changes;
}

/* -------------------------------------------- other units, as facing this one */

mmu_feed_t mmu_toolhead_source(void)
{
    if(!g_mmu.loaded) return MMU_FEED_NONE;
    if(g_mmu.loaded_ext >= 0 && g_mmu.loaded_ext < (int)g_mmu.ext_spool_count)
        return MMU_FEED_EXT;
    if(g_mmu.loaded_unit >= 0) {
        if(mmu_is_this_unit(g_mmu.loaded_unit)) return MMU_FEED_THIS;
        return MMU_FEED_OTHER_UNIT;
    }
    return MMU_FEED_NONE;
}

bool mmu_busy_elsewhere(char *why, size_t len)
{
    if(why && len > 0) why[0] = '\0';
    if(g_mmu.busy == MMU_BUSY_NONE) return false;

    if(g_mmu.busy_unit < 0) {
        if(why && len > 0)
            vs_copy(view_str(g_mmu.busy == MMU_BUSY_UNLOAD ? VS_BUSY_EXT_UNLOADING : VS_BUSY_EXT_LOADING),
                    why, len);
        return true;
    }
    if(mmu_is_this_unit(g_mmu.busy_unit)) return false;

    if(why && len > 0) {
        bool unload = g_mmu.busy == MMU_BUSY_UNLOAD;

        if(g_mmu.busy_slot >= 0) {
            char spool[64];

            vs_unit_spool(mmu_unit_name(g_mmu.busy_unit), g_mmu.busy_slot, spool, sizeof(spool));
            vs_format(unload ? VS_BUSY_SPOOL_UNLOADING : VS_BUSY_SPOOL_LOADING, why, len,
                      (vs_arg_t[]){ VS_ARG_T("spool", spool) }, 1);
        }
        else {
            vs_format(unload ? VS_BUSY_UNIT_UNLOADING : VS_BUSY_UNIT_LOADING, why, len,
                      (vs_arg_t[]){ VS_ARG_T("unit", mmu_unit_name(g_mmu.busy_unit)) }, 1);
        }
    }
    return true;
}

bool mmu_printing_elsewhere(void)
{
    if(!g_mmu.printing) return false;
    if(g_mmu.print_unit < 0) return false;
    return !mmu_is_this_unit(g_mmu.print_unit);
}

bool mmu_error_concerns_this(void)
{
    if(!g_mmu.has_error) return false;
    return g_mmu.error_unit < 0 || mmu_is_this_unit(g_mmu.error_unit);
}

/* --------------------------------------------------------- alerts, scoped */

bool mmu_alert_visible(int alert_idx)
{
    if(alert_idx < 0 || alert_idx >= (int)g_mmu.alert_count) return false;
    if(g_mmu.alerts[alert_idx].unit < 0) return true;
    return g_mmu.alerts[alert_idx].unit == mmu_this_unit();
}

int mmu_alert_visible_count(void)
{
    int i, n = 0;

    for(i = 0; i < (int)g_mmu.alert_count; i++) {
        if(mmu_alert_visible(i)) n++;
    }
    return n;
}

int mmu_alert_unread_visible(void)
{
    int i, n = 0;

    for(i = 0; i < (int)g_mmu.alert_count; i++) {
        if(mmu_alert_visible(i) && g_mmu.alerts[i].unread) n++;
    }
    return n;
}

bool mmu_alert_badge_expired(const mmu_model_t *m, const mmu_alert_t *a)
{
    if(!m || !a) return false;
    if(a->unread) return (uint32_t)(m->now_ms - a->raised_ms) >= MMU_ALERT_BADGE_UNREAD_MS;
    return (uint32_t)(m->now_ms - a->read_ms) >= MMU_ALERT_BADGE_READ_MS;
}

void mmu_alerts_mark_read(void)
{
    int i;
    uint8_t unread = 0;

    for(i = 0; i < (int)g_mmu.alert_count; i++) {
        if(mmu_alert_visible(i) && g_mmu.alerts[i].unread) {
            g_mmu.alerts[i].unread = false;
            g_mmu.alerts[i].read_ms = g_mmu.now_ms;
        }
        if(g_mmu.alerts[i].unread) unread++;
    }
    g_mmu.alert_unread = unread;
}

/* ------------------------------------------------------------------ groups */

int mmu_slot_group_in(const mmu_model_t *mdl, int unit, int slot)
{
    int g, m;

    for(g = 0; g < (int)mdl->group_count; g++) {
        for(m = 0; m < (int)mdl->groups[g].count; m++) {
            if(mdl->groups[g].members[m].unit == (int8_t)unit &&
               mdl->groups[g].members[m].slot == (int8_t)slot)
                return g;
        }
    }
    return -1;
}

int mmu_slot_group(int unit, int slot)
{
    return mmu_slot_group_in(&g_mmu, unit, slot);
}

bool mmu_group_next_spare_in(const mmu_model_t *mdl, int group, int *out_unit, int *out_slot)
{
    const mmu_group_t *g;
    int m;

    if(!out_unit || !out_slot) return false;
    if(group < 0 || group >= (int)mdl->group_count) return false;
    g = &mdl->groups[group];
    /* "A group with one member has no spare" (docs/GROUPS.md) - with only
     * one member, the loop below would otherwise offer it as its own spare
     * whenever nothing in the group is loaded yet. */
    if(g->count <= 1) return false;

    for(m = 0; m < (int)g->count; m++) {
        int u = g->members[m].unit;
        int s = g->members[m].slot;
        const mmu_slot_t *sl;

        if(u < 0 || u >= (int)mdl->unit_count) continue;
        if(s < 0 || s >= (int)mdl->units[u].slot_count) continue;
        sl = &mdl->units[u].slots[s];

        /* READY already excludes empty/loaded/error; a slot that ran dry
         * but was not physically removed stays READY at 0%, so that has to
         * be excluded explicitly - it is not a usable spare either. */
        if(sl->state != MMU_SLOT_READY) continue;
        if(sl->remaining_pct == 0) continue;

        *out_unit = u;
        *out_slot = s;
        return true;
    }
    return false;
}

bool mmu_group_next_spare(int group, int *out_unit, int *out_slot)
{
    return mmu_group_next_spare_in(&g_mmu, group, out_unit, out_slot);
}

bool mmu_group_editable(int group)
{
    return mmu_group_editable_in(&g_mmu, group);
}

bool mmu_group_editable_in(const mmu_model_t *mdl, int group)
{
    if(!mdl) return true;
    if(group < 0 || group >= (int)mdl->group_count) return true;
    if(mdl->busy != MMU_BUSY_NONE) return false;
    if(mdl->runout_active) return false;
    if(mdl->loaded && group == mdl->current_group) return false;
    return true;
}

const char *mmu_group_edit_reason(const mmu_model_t *mdl, int group)
{
    if(!mdl) return "";
    /* The three refusal rules of docs/GROUPS.md, in the order they are
     * checked: the lane is busy, the lane is in a runout, the group is the
     * one loaded. One string each, so an action names the rule that stops
     * it (PRINCIPLES.md 8). The first two are the lane's, not the group's, so
     * they stand for an unassigned bay too: -1 is the bay's own "no group",
     * and the host refuses its assign while the lane is busy. */
    if(mdl->busy != MMU_BUSY_NONE) return view_str(VS_REASON_GROUP_BUSY);
    if(mdl->runout_active) return view_str(VS_REASON_GROUP_RUNOUT);
    if(group < 0 || group >= (int)mdl->group_count) return "";
    if(mdl->loaded && group == mdl->current_group) return view_str(VS_REASON_GROUP_LOADED);
    return "";
}

void mmu_group_next_name(const mmu_model_t *mdl, char *out, size_t n)
{
    int i;

    if(!out || n == 0) return;
    /* MMU_MAX_GROUPS + 1 tries: a full table has none free, and then the last
     * name tried is the best answer there is. */
    for(i = 0; i <= MMU_MAX_GROUPS; i++) {
        char cand[MMU_GROUP_NAME_LEN];
        int g;
        bool used = false;

        snprintf(cand, sizeof cand, "T%d", i);
        for(g = 0; g < (int)mdl->group_count; g++) {
            if(strcmp(mdl->groups[g].name, cand) == 0) { used = true; break; }
        }
        if(!used) {
            snprintf(out, n, "%s", cand);
            return;
        }
    }
    snprintf(out, n, "T%d", MMU_MAX_GROUPS);
}

bool mmu_group_on_unit_lane(const mmu_model_t *mdl, int unit_idx, int group)
{
    int th, k;

    if(!mdl) return true;
    if(unit_idx < 0 || unit_idx >= (int)mdl->unit_count) return true;
    if(group < 0 || group >= (int)mdl->group_count) return false;
    th = mdl->units[unit_idx].toolhead;
    /* An unreported lane belongs to no lane, so nothing can be said about
     * which groups are on it: every group is offered and the host refuses
     * the wrong one. */
    if(th < 0 || th >= (int)mdl->toolhead_count) return true;
    /* A group with no bay is on no lane at all, so it is nobody else's
     * group and may be filled from this one. */
    if(mdl->groups[group].count == 0) return true;
    for(k = 0; k < (int)mdl->groups[group].count; k++) {
        int u = mdl->groups[group].members[k].unit;

        if(u < 0 || u >= (int)mdl->unit_count) continue;
        if(mdl->units[u].toolhead == th) return true;
    }
    return false;
}

void mmu_group_assign(int group, int unit, int slot)
{
    mmu_group_t *g;
    char id[MMU_SLOT_ID_LEN];

    if(s_live) return;
    if(group < 0 || group >= (int)g_mmu.group_count) return;
    if(unit < 0 || unit >= (int)g_mmu.unit_count) return;
    if(slot < 0 || slot >= (int)g_mmu.units[unit].slot_count) return;

    mmu_group_unassign(unit, slot);   /* leave the donor group first */

    g = &g_mmu.groups[group];
    if(g->count >= MMU_MAX_GROUP_MEMBERS) return;
    g->members[g->count].unit = (int8_t)unit;
    g->members[g->count].slot = (int8_t)slot;
    g->count++;

    mmu_slot_id(unit, slot, id, sizeof(id));
    actions_log("assign %s %s", g->name, id);
}

void mmu_group_unassign(int unit, int slot)
{
    int group = mmu_slot_group(unit, slot);
    mmu_group_t *g;
    int m;

    if(s_live) return;
    if(group < 0) return;
    g = &g_mmu.groups[group];

    for(m = 0; m < (int)g->count; m++) {
        if(g->members[m].unit == (int8_t)unit && g->members[m].slot == (int8_t)slot) {
            int j;

            for(j = m; j + 1 < (int)g->count; j++) g->members[j] = g->members[j + 1];
            g->count--;
            break;
        }
    }
}

int mmu_group_create(const char *name)
{
    mmu_group_t *g;
    int idx;

    if(s_live) return -1;
    if(g_mmu.group_count >= MMU_MAX_GROUPS) return -1;

    idx = (int)g_mmu.group_count;
    g = &g_mmu.groups[idx];
    memset(g, 0, sizeof(*g));

    if(name && name[0]) {
        mmu_copy_text(g->name, sizeof(g->name), name);
    }
    else {
        int n;

        for(n = 0; n <= MMU_MAX_GROUPS; n++) {
            char cand[MMU_GROUP_NAME_LEN];
            int j;
            bool used = false;

            snprintf(cand, sizeof(cand), "T%d", n);
            for(j = 0; j < idx; j++) {
                if(strcmp(g_mmu.groups[j].name, cand) == 0) { used = true; break; }
            }
            if(!used) {
                mmu_copy_text(g->name, sizeof(g->name), cand);
                break;
            }
        }
    }

    g_mmu.group_count++;
    actions_log("group create %s", g->name);
    return idx;
}

/* U1c (docs/design/UNIFIED_UI.md 3): the enough-filament shortfall, computed
 * fresh from @p m every call instead of once when the job's metadata arrives
 * (status_map_apply_metadata's own one-shot alert stays as it was - this is
 * the on-demand twin the view builders call, so a group the metadata's tool
 * needed nothing from at the time, but that gains members later, is still
 * caught). Mirrors status_map_apply_metadata's own math exactly: the group
 * named "T<tool_index>", summed remaining_g over members that report one,
 * skipped when every member is unknown (never alarm on pure unknowns). */
bool mmu_model_tool_shortfall(const mmu_model_t *m, int tool_index, int *need_g, int *have_g)
{
    char gname[MMU_GROUP_NAME_LEN];
    int gi, j, need;
    int32_t have = 0;
    bool all_unknown = true;

    if(!m || tool_index < 0 || tool_index >= MMU_MAX_TOOLS) return false;

    need = (int)m->job_need_g[tool_index];
    if(need <= 0) return false;

    snprintf(gname, sizeof gname, "T%d", tool_index);
    gi = -1;
    for(j = 0; j < (int)m->group_count; j++) {
        if(strcmp(m->groups[j].name, gname) == 0) { gi = j; break; }
    }
    if(gi < 0) return false;

    for(j = 0; j < (int)m->groups[gi].count; j++) {
        int unit = (int)m->groups[gi].members[j].unit;
        int slot = (int)m->groups[gi].members[j].slot;

        if(unit < 0 || unit >= (int)m->unit_count) continue;
        if(slot < 0 || slot >= (int)m->units[unit].slot_count) continue;
        if(m->units[unit].slots[slot].remaining_g >= 0) {
            have += (int32_t)m->units[unit].slots[slot].remaining_g;
            all_unknown = false;
        }
    }
    if(all_unknown) return false;
    if(have >= (int32_t)need) return false;

    if(need_g) *need_g = need;
    if(have_g) *have_g = (int)have;
    return true;
}

/* ---------------------------------------------------- backups and runout */

bool mmu_slot_backup(int slot, int *out_unit, int *out_slot)
{
    int this_unit = mmu_this_unit();
    int group;

    if(!out_unit || !out_slot) return false;
    if(slot < 0 || slot >= (int)g_mmu.units[this_unit].slot_count) return false;

    group = mmu_slot_group(this_unit, slot);
    if(group < 0) return false;
    return mmu_group_next_spare(group, out_unit, out_slot);
}

bool mmu_runout_concerns_this(void)
{
    if(!g_mmu.runout_active) return false;
    return (g_mmu.runout_from_unit >= 0 && mmu_is_this_unit(g_mmu.runout_from_unit)) ||
           (g_mmu.runout_to_unit >= 0 && mmu_is_this_unit(g_mmu.runout_to_unit));
}

/* ------------------------------------------------------- dummy transitions */

void mmu_model_start_load(int unit_idx, int slot_idx)
{
    static const char *const with_filament[] = {
        "heat",
        "cut",
        "retract",
        "feed",
        "purge",
    };
    static const char *const empty_toolhead[] = {
        "heat",
        "feed",
        "purge",
    };

    if(s_live) return;
    if(unit_idx < 0 || unit_idx >= g_mmu.unit_count) return;
    if(slot_idx < 0 || slot_idx >= g_mmu.units[unit_idx].slot_count) return;

    g_mmu.busy = MMU_BUSY_LOAD;
    g_mmu.busy_unit = (int8_t)unit_idx;
    g_mmu.busy_slot = (int8_t)slot_idx;
    g_mmu.step_failed = -1;
    g_mmu.step_current = 0;
    g_mmu.tick = 0;
    if(g_mmu.loaded && g_mmu.loaded_unit != unit_idx) {
        set_steps(with_filament, 5);
    }
    else if(g_mmu.loaded) {
        set_steps(with_filament, 5);
    }
    else {
        set_steps(empty_toolhead, 3);
    }
    sync_motion_settled();
    mmu_model_capture_this(&g_mmu);
}

void mmu_model_start_unload(void)
{
    static const char *const steps[] = {
        "heat",
        "cut",
        "retract",
    };

    if(s_live) return;

    g_mmu.busy = MMU_BUSY_UNLOAD;
    g_mmu.busy_unit = g_mmu.loaded_unit;
    g_mmu.busy_slot = g_mmu.loaded_slot;
    g_mmu.step_failed = -1;
    g_mmu.step_current = 0;
    g_mmu.tick = 0;
    set_steps(steps, 3);
    sync_motion_settled();
    mmu_model_capture_this(&g_mmu);
}

void mmu_model_stop_busy(void)
{
    if(s_live) return;

    g_mmu.busy = MMU_BUSY_NONE;
    g_mmu.busy_unit = -1;
    g_mmu.busy_slot = -1;
    g_mmu.step_current = -1;
    g_mmu.step_failed = -1;
    g_mmu.has_error = false;
    sync_motion_settled();
    mmu_model_capture_this(&g_mmu);
}

void mmu_model_set_reread(int unit_idx, int slot_idx)
{
    mmu_slot_t *slot;

    if(s_live) return;
    if(unit_idx < 0 || unit_idx >= g_mmu.unit_count) return;
    if(slot_idx < 0 || slot_idx >= g_mmu.units[unit_idx].slot_count) return;

    slot = &g_mmu.units[unit_idx].slots[slot_idx];
    if(slot->info_source == MMU_INFO_UNKNOWN) {
        slot->info_source = MMU_INFO_TAG;
        slot->state = MMU_SLOT_READY;
        snprintf(slot->material, sizeof(slot->material), "PLA");
        snprintf(slot->brand, sizeof(slot->brand), "Bambu");
        slot->color = 0xD23B3B;
        slot->color_known = true;
        slot->remaining_pct = 100;
        slot->spool_id = tag_spool_id(slot->color, slot->remaining_pct);
        slot->remaining_g = 1000;
    }
}

/* --- misc additions --- */
/* A small dummy version table, keyed by hardware variant. Two of the variants
 * are behind a newer release so the Unit info screen has both badge states. */
typedef struct {
    mmu_variant_t variant;
    const char *firmware;
    const char *latest;
} misc_ident_row_t;

static const misc_ident_row_t s_misc_ident[] = {
    { MMU_VARIANT_AMS,      "00.00.06.49", "00.00.06.51" },
    { MMU_VARIANT_AMS2_PRO, "01.07.00.00", "01.09.00.00" },
    { MMU_VARIANT_AMS_HT,   "01.06.02.00", "01.06.02.00" },
    { MMU_VARIANT_LANE,     "00.09.04.00", "01.00.00.00" },
};

bool mmu_unit_ident(int unit_idx, mmu_unit_ident_t *out)
{
    const mmu_unit_t *u;
    const misc_ident_row_t *row = &s_misc_ident[0];
    size_t i;

    if(!out || unit_idx < 0 || unit_idx >= g_mmu.unit_count) return false;
    u = &g_mmu.units[unit_idx];
    for(i = 0; i < sizeof(s_misc_ident) / sizeof(s_misc_ident[0]); i++) {
        if(s_misc_ident[i].variant == u->variant) {
            row = &s_misc_ident[i];
            break;
        }
    }

    memset(out, 0, sizeof(*out));
    mmu_copy_text(out->model, sizeof(out->model), mmu_variant_name(u->variant));
    mmu_copy_text(out->firmware, sizeof(out->firmware), row->firmware);
    mmu_copy_text(out->latest, sizeof(out->latest), row->latest);
    /* Deterministic pseudo-serial so every unit looks distinct. */
    snprintf(out->serial, sizeof(out->serial), "0M%02dA35%06d",
             10 + unit_idx, (unit_idx * 7919 + 104729) % 1000000);
    return true;
}

bool mmu_unit_update_available(int unit_idx)
{
    mmu_unit_ident_t ident;

    if(!mmu_unit_ident(unit_idx, &ident)) return false;
    return strcmp(ident.firmware, ident.latest) != 0;
}

int mmu_unit_update_count(void)
{
    int i, n = 0;

    for(i = 0; i < g_mmu.unit_count; i++) {
        if(mmu_unit_update_available(i)) n++;
    }
    return n;
}

/* --- load additions --- */

/* External-spool loads have no slot to point at, so the target lives beside
 * the model rather than in mmu_model_t. */
static int8_t s_ext_target = -1;

void mmu_model_start_ext_load(int ext_idx)
{
    static const char *const steps[] = {
        "heat",
        "feed",
        "grab",
        "purge",
    };

    if(ext_idx < 0 || ext_idx >= (int)g_mmu.ext_spool_count) return;

    g_mmu.busy = MMU_BUSY_LOAD;
    g_mmu.busy_unit = -1;
    g_mmu.busy_slot = -1;
    g_mmu.step_failed = -1;
    g_mmu.step_current = 0;
    g_mmu.tick = 0;
    s_ext_target = (int8_t)ext_idx;
    set_steps(steps, 4);
    sync_motion_settled();
    mmu_model_capture_this(&g_mmu);
}

void mmu_model_confirm_ext_load(void)
{
    if(s_live) return;
    if(s_ext_target < 0) return;

    g_mmu.loaded = true;
    g_mmu.loaded_unit = -1;
    g_mmu.loaded_slot = -1;
    g_mmu.loaded_ext = s_ext_target;
    g_mmu.toolhead_sensor = true;
    g_mmu.current_group = -1;
    mmu_model_capture_this(&g_mmu);
}

void mmu_model_retry_ext_load(void)
{
    if(s_live) return;
    if(s_ext_target < 0) return;
    mmu_model_start_ext_load(s_ext_target);
}

int mmu_model_ext_target(void)
{
    return s_ext_target;
}

void mmu_model_clear_resolved_alerts(void)
{
    int i;
    int out = 0;
    uint8_t unread = 0;

    for(i = 0; i < (int)g_mmu.alert_count; i++) {
        if(g_mmu.alerts[i].severity == MMU_SEV_INFO) continue;
        if(out != i) g_mmu.alerts[out] = g_mmu.alerts[i];
        if(g_mmu.alerts[out].unread) unread++;
        out++;
    }
    g_mmu.alert_count = (uint8_t)out;
    g_mmu.alert_unread = unread;
}

void mmu_model_clear_error(void)
{
    if(s_live) return;

    g_mmu.has_error = false;
    g_mmu.step_failed = -1;
    if(g_mmu.step_current < 0) g_mmu.step_current = 0;
    sync_motion_settled();
    mmu_model_capture_this(&g_mmu);
}

void mmu_alerts_push_to(mmu_model_t *m, const char *text, mmu_severity_t severity, int unit)
{
    mmu_alerts_push_at(m, text, severity, unit, -1, false);
}

/* One alert into the history, newest first: index 0 is the newest. A full
 * table drops its oldest entry (the tail) to make room. */
static void alerts_push_struct(mmu_model_t *m, const mmu_alert_t *src)
{
    int n, i;

    n = (int)m->alert_count;
    if(n >= MMU_MAX_ALERTS) n = MMU_MAX_ALERTS - 1;

    for(i = n; i > 0; i--) m->alerts[i] = m->alerts[i - 1];
    m->alerts[0] = *src;
    m->alerts[0].unread = true;
    m->alerts[0].raised_ms = m->now_ms;
    m->alerts[0].read_ms = 0;
    m->alert_count = (uint8_t)(n + 1);

    /* The dropped tail may itself have been unread, so recount rather than
     * just bumping the counter. */
    m->alert_unread = 0;
    for(i = 0; i < (int)m->alert_count; i++)
        if(m->alerts[i].unread) m->alert_unread++;
}

void mmu_alerts_push_at(mmu_model_t *m, const char *text, mmu_severity_t severity, int unit,
                        int toolhead, bool lane_fault)
{
    mmu_alert_t a;

    if(!m) return;
    memset(&a, 0, sizeof a);
    a.severity = severity;
    a.unit = (int8_t)unit;
    a.toolhead = (int8_t)toolhead;
    a.lane_fault = lane_fault;
    mmu_copy_text(a.text, sizeof(a.text), text);
    alerts_push_struct(m, &a);
}

void mmu_model_push_alert(const char *text, mmu_severity_t severity, int unit)
{
    mmu_alerts_push_to(&g_mmu, text, severity, unit);
}

void mmu_model_push_alert_pending(mmu_model_t *m, const char *text,
                                   mmu_severity_t severity, int unit)
{
    mmu_model_push_alert_pending_at(m, text, severity, unit, -1, false);
}

void mmu_model_push_alert_pending_at(mmu_model_t *m, const char *text, mmu_severity_t severity,
                                     int unit, int toolhead, bool lane_fault)
{
    int n;

    if(!m) return;
    n = (int)m->alert_pending_count;
    if(n >= 4 && lane_fault) {
        /* a lane fault outranks what is queued: drop the oldest entry */
        int k;

        for(k = 0; k < 3; k++) m->alert_pending[k] = m->alert_pending[k + 1];
        n = 3;
        m->alert_pending_count = 3;
    }
    if(n >= 4) return; /* small queue; silent drop */
    memset(&m->alert_pending[n], 0, sizeof(m->alert_pending[n]));
    if(text) mmu_copy_text(m->alert_pending[n].text, sizeof(m->alert_pending[n].text), text);
    m->alert_pending[n].severity = severity;
    m->alert_pending[n].unit     = (int8_t)unit;
    m->alert_pending[n].toolhead = (int8_t)toolhead;
    m->alert_pending[n].lane_fault = lane_fault;
    m->alert_pending[n].unread   = true;
    m->alert_pending_count = (uint8_t)(n + 1);
}

void mmu_model_push_alert_pending_tpl(mmu_model_t *m, mmu_alert_kind_t kind,
                                      mmu_severity_t severity, int unit, int toolhead,
                                      int a0, int a1, int a2)
{
    int n;

    if(!m) return;
    n = (int)m->alert_pending_count;
    if(n >= 4) return; /* small queue; silent drop */
    memset(&m->alert_pending[n], 0, sizeof(m->alert_pending[n]));
    m->alert_pending[n].kind = (uint8_t)kind;
    m->alert_pending[n].arg[0] = (int16_t)a0;
    m->alert_pending[n].arg[1] = (int16_t)a1;
    m->alert_pending[n].arg[2] = (int16_t)a2;
    m->alert_pending[n].severity = severity;
    m->alert_pending[n].unit     = (int8_t)unit;
    m->alert_pending[n].toolhead = (int8_t)toolhead;
    m->alert_pending[n].unread   = true;
    m->alert_pending_count = (uint8_t)(n + 1);
}

bool mmu_model_merge_live(mmu_model_t *ui, mmu_model_t *live)
{
    mmu_alert_t pending[4];
    uint8_t count;
    int i;

    if(!ui || !live) return false;

    if(ui != live) {
        /* Snapshot the UI's alert history before the overwrite. */
        uint8_t saved_count   = ui->alert_count;
        uint8_t saved_unread  = ui->alert_unread;
        uint32_t saved_now    = ui->now_ms;
        mmu_alert_t saved[MMU_MAX_ALERTS];
        memcpy(saved, ui->alerts, sizeof(saved));

        memcpy(ui, live, sizeof(*ui));

        /* Restore the UI model's alert history. */
        ui->alert_count  = saved_count;
        ui->alert_unread = saved_unread;
        /* the clock never runs backward: both models may have been stamped */
        if((int32_t)(saved_now - ui->now_ms) > 0) ui->now_ms = saved_now;
        memcpy(ui->alerts, saved, sizeof(saved));

        /* The pending fields in ui were just overwritten from live; clear
         * them now — they will be drained below from the saved local copy. */
        ui->alert_pending_count = 0;
    }

    /* Drain pending alerts (arrival order) into the UI history. */
    count = live->alert_pending_count;
    if(count > 4) count = 4;
    memcpy(pending, live->alert_pending, count * sizeof(pending[0]));
    live->alert_pending_count = 0;

    for(i = 0; i < (int)count; i++) alerts_push_struct(ui, &pending[i]);

    /* this_unit may have changed since live's last refresh (or since ui's
     * own copy above), so re-derive the globals rather than trust either. */
    mmu_model_refresh_this(ui);

    return true;
}

/* --- home additions --- */

void mmu_home_set_selection(int unit_idx, int slot_idx, int ext_idx)
{
    g_mmu.home_selection = (unit_idx >= 0 || ext_idx >= 0);
    g_mmu.selected_unit = (int8_t)unit_idx;
    g_mmu.selected_slot = (int8_t)slot_idx;
    g_mmu.selected_ext = (int8_t)ext_idx;
}

void mmu_home_start_ext_load(int ext_idx)
{
    static const char *const steps[] = {
        "heat",
        "feed",
        "grab",
        "purge",
    };

    if(ext_idx < 0 || ext_idx >= g_mmu.ext_spool_count) return;
    if(!g_mmu.ext_spools[ext_idx].present) return;

    g_mmu.busy = MMU_BUSY_LOAD;
    g_mmu.busy_unit = -1;
    g_mmu.busy_slot = -1;
    g_mmu.step_failed = -1;
    g_mmu.step_current = 0;
    g_mmu.tick = 0;
    /* external spool steps: nothing has to be pulled back first */
    set_steps(steps, 4);
    sync_motion_settled();

    /* park it in the toolhead so the tile reacts immediately; busy_finish()
     * leaves it alone because busy_unit is -1. */
    if(g_mmu.loaded_unit >= 0 && g_mmu.loaded_slot >= 0) {
        mmu_slot_t *old = &g_mmu.units[g_mmu.loaded_unit].slots[g_mmu.loaded_slot];
        if(old->state == MMU_SLOT_LOADED) old->state = MMU_SLOT_READY;
    }
    g_mmu.loaded = true;
    g_mmu.loaded_unit = -1;
    g_mmu.loaded_slot = -1;
    g_mmu.loaded_ext = (int8_t)ext_idx;
    g_mmu.toolhead_sensor = false;
    g_mmu.current_group = -1;
    mmu_model_capture_this(&g_mmu);
}

void mmu_home_start_ext_unload(void)
{
    static const char *const steps[] = {
        "heat",
        "cut",
        "retract",
    };

    g_mmu.busy = MMU_BUSY_UNLOAD;
    g_mmu.busy_unit = -1;
    g_mmu.busy_slot = -1;
    g_mmu.step_failed = -1;
    g_mmu.step_current = 0;
    g_mmu.tick = 0;
    set_steps(steps, 3);
    g_mmu.loaded_ext = -1;
    sync_motion_settled();
    mmu_model_capture_this(&g_mmu);
}

/* --- app additions --- */

int mmu_model_scenario_count(void)
{
    return MMU_SCENARIO_COUNT;
}

const char *mmu_model_scenario_name(int i)
{
    if(i < 0 || i >= MMU_SCENARIO_COUNT) return "?";
    return mmu_scenario_names[i];
}
