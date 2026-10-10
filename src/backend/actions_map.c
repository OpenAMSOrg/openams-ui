/**
 * @file actions_map.c
 * The display's action -> G-code map (see actions_map.h).
 *
 * Local lines (0) need no case of their own: they are exactly the lines that
 * match none of the host shapes below, or match one but name something the
 * model does not hold. The vocabulary the screens emit is `display`, `open`,
 * `alerts`, `screens`, `back home`, `print status`, `load status`, `bind
 * display to`, `side`, `rotate`, `edit`, `confirm mapping:`,
 * lines starting with two spaces, `clear resolved alerts`, `rescan units` and
 * the demo alert buttons (`retry`, `finished continue`, `ok`, `stop drying`,
 * `filament extruded, continue`, `not extruded yet, retry`, `retry (problem
 * solved)`).
 */

#include "backend/actions_map.h"

#include "lib/cjson/cJSON.h"
#include "view/strings.h"

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ writer */

/* The G-code being built. Commands are appended one per line; overflow is
 * sticky so the whole answer can be written and then checked, and a command
 * that does not fit turns the result into -1 rather than a truncation. */
typedef struct {
    char *buf;
    size_t cap;
    size_t used;
    bool over;
} wr_t;

static void wr_open(wr_t *w, char *buf, size_t cap)
{
    w->buf = buf;
    w->cap = cap;
    w->used = 0;
    /* A host action cannot be built when there is nowhere to write it. */
    w->over = (buf == NULL || cap == 0);
    if(!w->over) buf[0] = '\0';
}

static void wr_add(wr_t *w, const char *s)
{
    size_t n = strlen(s);

    if(w->over) return;
    if(w->used + n + 1 > w->cap) {
        w->over = true;
        return;
    }
    memcpy(w->buf + w->used, s, n);
    w->used += n;
    w->buf[w->used] = '\0';
}

/* Append one command, built from @p fmt; a newline joins it to whatever came
 * before. */
static void wr_cmd(wr_t *w, const char *fmt, ...)
{
    char tmp[128];
    va_list ap;

    if(w->over) return;

    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    if(w->used) wr_add(w, "\n");
    wr_add(w, tmp);
}

/* --------------------------------------------------------------- resolving */

/* The text after the leading "@p verb " of @p line, or NULL when the line is
 * not that verb followed by a space. */
static const char *rest(const char *line, const char *verb)
{
    size_t n = strlen(verb);

    if(strncmp(line, verb, n) != 0) return NULL;
    if(line[n] != ' ') return NULL;
    return line + n + 1;
}

/* The unit index named @p name, or -1. */
static int unit_find(const mmu_model_t *m, const char *name)
{
    int i;

    for(i = 0; i < (int)m->unit_count; i++) {
        if(strcmp(m->units[i].name, name) == 0) return i;
    }
    return -1;
}

/* The group index named @p name, or -1. */
static int group_find(const mmu_model_t *m, const char *name)
{
    int i;

    if(!name) return -1;
    for(i = 0; i < (int)m->group_count; i++) {
        if(strcmp(m->groups[i].name, name) == 0) return i;
    }
    return -1;
}

/* The (unit, slot) whose display id is @p id ("<unit name><bay + 1>", C2 6.2),
 * found by walking the model; false when no slot reads as that id. */
static bool slot_find(const mmu_model_t *m, const char *id, int *out_unit, int *out_slot)
{
    char buf[MMU_SLOT_ID_LEN];
    int i, j;

    for(i = 0; i < (int)m->unit_count; i++) {
        for(j = 0; j < (int)m->units[i].slot_count; j++) {
            mmu_slot_id_in(m, i, j, buf, sizeof buf);
            if(strcmp(buf, id) == 0) {
                if(out_unit) *out_unit = i;
                if(out_slot) *out_slot = j;
                return true;
            }
        }
    }
    return false;
}

/* The FPS lane the host gave @p unit, or NULL when it has not named it. */
static const char *unit_lane(const mmu_model_t *m, int unit)
{
    if(unit < 0 || unit >= (int)m->unit_count) return NULL;
    return m->units[unit].lane[0] ? m->units[unit].lane : NULL;
}

/* The toolhead whose id (the FPS lane) is @p id, or -1. The panel addresses
 * a toolhead directly (no particular bay) for "stop" and "unload": several
 * toolheads can be busy independently, so the single-lane display's bare
 * "stop" (busy_unit) and slot-keyed "unload <slot>" cannot carry a lane by
 * themselves. */
static int toolhead_find(const mmu_model_t *m, const char *id)
{
    int i;

    for(i = 0; i < (int)m->toolhead_count; i++) {
        if(strcmp(m->toolheads[i].id, id) == 0) return i;
    }
    return -1;
}

/* The host `idx` of @p unit (the OAMS= value), or -1 when unknown. */
static int unit_host_idx(const mmu_model_t *m, int unit)
{
    if(unit < 0 || unit >= (int)m->unit_count) return -1;
    return m->units[unit].host_idx;
}

/* Whether the toolhead of @p unit is busy loading or unloading this very bay. */
static bool busy_here(const mmu_model_t *m, int unit, int slot)
{
    int th = m->units[unit].toolhead;

    if(th < 0 || th >= (int)m->toolhead_count) return false;
    return m->toolheads[th].busy != MMU_BUSY_NONE && m->toolheads[th].busy_unit == unit &&
           m->toolheads[th].busy_slot == slot;
}

/* What the host says this bay can do (C2 6.2), applied after the model's own
 * state predicates so the more specific reason ("already loaded") still wins:
 * a bay list (`bays[i].a`) that lacks the action's letter withholds it. A host that sent neither
 * withholds nothing. @p act is one MMU_BAY_ACT_* bit. A refused action is
 * never sent, whatever else the model would let it do. */
/* The motion stage is unit-wide: a bay being positioned holds every bay's motion. */
static bool unit_positioning(const mmu_unit_t *u)
{
    int i;

    for(i = 0; i < (int)u->slot_count; i++)
        if(u->slots[i].positioning) return true;
    return false;
}

/* What holds a unit's motion, asked before any state predicate, in this
 * order: the unit is offline; a stop fault is latched on its lane (the host's
 * own words); the unit is online with no stop fault and the host says it is
 * not `ready` (C2 6.2), which is a unit that cannot move filament at all. A
 * unit that is ready is never called unsupported. */
static bool unit_ready(const mmu_model_t *m, int unit, const char **why)
{
    const mmu_unit_t *u = &m->units[unit];
    const mmu_toolhead_t *th = (u->toolhead >= 0 && u->toolhead < (int)m->toolhead_count)
                                   ? &m->toolheads[u->toolhead] : NULL;

    if(!u->connected) {
        *why = view_str(VS_HS_OFFLINE);
        return false;
    }
    if(th && th->has_error && th->error_severity == MMU_SEV_STOP) {
        *why = th->error_text[0] ? th->error_text : view_str(VS_LABEL_FAULT);
        return false;
    }
    if(u->ready_known && !u->ready) {
        *why = view_str(VS_REASON_NOT_SUPPORTED);
        return false;
    }
    return true;
}

static bool bay_may(const mmu_model_t *m, int unit, int slot, uint8_t act, const char **why)
{
    const mmu_unit_t *u = &m->units[unit];
    const mmu_slot_t *s = &u->slots[slot];

    if(!s->acts_known || (s->acts & act)) return true;

    /* The host's list is what is allowed now (C2 5.6), so a letter can be
     * missing for the bay's state or for a step the unit wants first, never
     * for "not supported" (the unit is ready). Calibrate is the one action the
     * display does not already refuse by state, so say the state when there
     * is one. A load on a ready bay of an idle lane that is waiting for its
     * PTFE calibration, which the host does offer, says to calibrate first. */
    *why = view_str(VS_REASON_NOT_NOW);
    if(act == MMU_BAY_ACT_LOAD) {
        int th0 = u->toolhead;
        bool lane_idle = th0 < 0 || th0 >= (int)m->toolhead_count ||
                         (m->toolheads[th0].busy == MMU_BUSY_NONE && !m->toolheads[th0].loaded);

        if(s->state == MMU_SLOT_READY && lane_idle && s->cal_state == MMU_CAL_UNCALIBRATED &&
           (s->acts & MMU_BAY_ACT_CALIBRATE))
            *why = view_str(VS_REASON_CALIBRATE_FIRST);
    }
    if(act == MMU_BAY_ACT_CALIBRATE) {
        int th = u->toolhead;

        if(busy_here(m, unit, slot)) *why = view_str(VS_REASON_BUSY_HERE);
        else if(s->state == MMU_SLOT_EMPTY) *why = view_str(VS_REASON_BAY_EMPTY);
        else if(s->state == MMU_SLOT_ERROR) *why = view_str(VS_REASON_BAY_ERROR);
        else if(unit_positioning(u)) *why = view_str(VS_REASON_POSITIONING);
        else if(s->state == MMU_SLOT_LOADED) *why = view_str(VS_REASON_ALREADY_LOADED);
        else if(th >= 0 && th < (int)m->toolhead_count &&
                (m->toolheads[th].busy != MMU_BUSY_NONE || m->toolheads[th].loaded))
            *why = view_str(VS_REASON_GROUP_BUSY);
    }
    return false;
}

const char *actions_dry_reason(const mmu_unit_t *u)
{
    if(u->dry_unlisted)
        return view_str(u->has_power_adapter && !u->power_adapter ? VS_ALERT_ADAPTER
                                                                  : VS_REASON_NOT_SUPPORTED);
    return u->cannot_dry_reason;
}

/* A unit action `act` lists (C2 6.2), or does not. @p act is one MMU_UNIT_ACT_*
 * bit; a host that sent no `act` withholds nothing. */
static bool unit_may(const mmu_unit_t *u, uint8_t act)
{
    return !u->act_known || (u->acts & act);
}

/* `load <slot>`: OPENAMS_LOAD wants the group by name and the bay by the
 * host's own id, so a bay outside a group, or one the host has not identified,
 * cannot be addressed (the -1 cases). Returns 0 when @p id is no slot of the
 * model. */
static int load_gcode(const mmu_model_t *m, const char *id,
                      char *buf, size_t len, const char **why)
{
    int unit, slot, gi;

    if(!slot_find(m, id, &unit, &slot)) return 0;
    if(!unit_ready(m, unit, why)) return -1;

    /* The one predicate: what the model says the bay can do now (the view dims
     * exactly what this refuses, with these reasons). */
    if(busy_here(m, unit, slot)) {
        *why = view_str(VS_REASON_BUSY_HERE);
        return -1;
    }
    switch(m->units[unit].slots[slot].state) {
    case MMU_SLOT_LOADED: *why = view_str(VS_REASON_ALREADY_LOADED); return -1;
    case MMU_SLOT_ERROR:  *why = view_str(VS_REASON_BAY_ERROR); return -1;
    case MMU_SLOT_EMPTY:  *why = view_str(VS_REASON_BAY_EMPTY); return -1;
    default: break;
    }

    if(m->units[unit].slots[slot].positioning) {
        *why = view_str(VS_REASON_POSITIONING);
        return -1;
    }

    gi = mmu_slot_group_in(m, unit, slot);
    if(gi < 0) {
        *why = view_str(VS_REASON_NOT_GROUPED);
        return -1;
    }
    if(m->units[unit].slots[slot].global_id < 0) {
        *why = view_str(VS_REASON_NO_HOST_ID);
        return -1;
    }
    if(!bay_may(m, unit, slot, MMU_BAY_ACT_LOAD, why)) return -1;

    snprintf(buf, len, "OPENAMS_LOAD GROUP=%s SLOT=%d",
             m->groups[gi].name, (int)m->units[unit].slots[slot].global_id);
    return 1;
}

/* `setting <key> on|off`: the name the host knows @p key by. The five real
 * keys map to themselves; the two legacy display spellings are translated; any
 * other key the display keeps to itself (NULL). */
static const char *option_name(const char *key)
{
    static const struct {
        const char *key;
        const char *name;
    } k_keys[] = {
        { "read-on-insertion",           "read_tag_on_insertion" },
        { "read-on-startup",             "read_tags_at_startup" },
        { "read_tag_on_insertion",       "read_tag_on_insertion" },
        { "read_tags_at_startup",        "read_tags_at_startup" },
        { "auto_reload_on_runout",       "auto_reload_on_runout" },
        { "auto_create_spoolman_spools", "auto_create_spoolman_spools" },
        { "apply_pa_on_load",            "apply_pa_on_load" },
    };
    size_t i;

    for(i = 0; i < sizeof k_keys / sizeof k_keys[0]; i++) {
        if(strcmp(k_keys[i].key, key) == 0) return k_keys[i].name;
    }
    return NULL;
}

/* ------------------------------------------------------------- the table */

/* Whether a sequence runs on a toolhead: its lane is busy loading or
 * unloading, or its stage plan has a current step that has not failed (the
 * heat, home, cut and feed steps run before and between the AMS's own moves). */
static bool sequence_runs(int busy, int step_count, int step_current, int step_failed)
{
    return busy != MMU_BUSY_NONE ||
           (step_count > 0 && step_current >= 0 && step_current < step_count && step_failed < 0);
}

/* The lane a `stop` line stops: 1 and @p *lane when a sequence runs on it, -1
 * and the reason when nothing does, 0 when the line is not a stop of a lane
 * (including the alert button `stop drying`). The bare `stop` is the
 * display's own lane (the busy unit's, else the bound unit's). */
static int stop_resolve(const char *line, const mmu_model_t *m, const char **lane, const char **why)
{
    const char *arg;

    if(strcmp(line, "stop") == 0) {
        int unit = m->busy_unit >= 0 ? m->busy_unit : m->this_unit;

        if(!sequence_runs(m->busy, m->step_count, m->step_current, m->step_failed)) {
            *why = view_str(VS_REASON_NOTHING_BUSY);
            return -1;
        }
        *lane = unit_lane(m, unit);
        if(!*lane) {
            *why = view_str(VS_REASON_LANE_UNKNOWN);
            return -1;
        }
        return 1;
    }
    arg = rest(line, "stop");
    if(arg) {
        int th = toolhead_find(m, arg);
        const mmu_toolhead_t *t;

        if(th < 0) return 0;
        t = &m->toolheads[th];
        if(!sequence_runs(t->busy, t->step_count, t->step_current, t->step_failed)) {
            *why = view_str(VS_REASON_NOTHING_BUSY);
            return -1;
        }
        *lane = t->id;
        return 1;
    }
    return 0;
}

static int dispatch(wr_t *w, const char *line, const mmu_model_t *m, const char **why)
{
    char a[MMU_SLOT_ID_LEN], b[MMU_SLOT_ID_LEN], val[8], cmd[64];
    const char *arg;
    int unit, slot, gi, t, h, n;

    /* `pause and load <slot>`: pause first, then the same load the home tile
     * sends. Built in one buffer so a load the model cannot address refuses
     * the whole line instead of leaving a bare PAUSE behind. */
    arg = rest(line, "pause and load");
    if(arg) {
        int rc = load_gcode(m, arg, cmd, sizeof cmd, why);
        if(rc != 1) return rc;
        wr_cmd(w, "PAUSE");
        wr_cmd(w, "%s", cmd);
        return 1;
    }

    arg = rest(line, "load");
    if(arg) {
        int rc = load_gcode(m, arg, cmd, sizeof cmd, why);
        if(rc == 1) wr_cmd(w, "%s", cmd);
        return rc;
    }

    /* `unload <slot>`: the lane, not the bay, is what OPENAMS_UNLOAD takes.
     * `unload <toolhead id>` (the panel, several toolheads at once) addresses
     * the lane directly when @p arg is no bay of the model. */
    arg = rest(line, "unload");
    if(arg) {
        const char *lane;
        int th;

        if(slot_find(m, arg, &unit, &slot)) {
            if(!unit_ready(m, unit, why)) return -1;
            if(busy_here(m, unit, slot)) {
                *why = view_str(VS_REASON_BUSY_HERE);
                return -1;
            }
            if(m->units[unit].slots[slot].state != MMU_SLOT_LOADED) {
                *why = view_str(VS_REASON_NOT_LOADED);
                return -1;
            }
            lane = unit_lane(m, unit);
            if(!lane) {
                *why = view_str(VS_REASON_LANE_UNKNOWN);
                return -1;
            }
            if(!bay_may(m, unit, slot, MMU_BAY_ACT_UNLOAD, why)) return -1;
            wr_cmd(w, "OPENAMS_UNLOAD FPS=%s", lane);
            return 1;
        }
        th = toolhead_find(m, arg);
        if(th < 0) return 0;
        if(!m->toolheads[th].loaded) {
            *why = view_str(VS_REASON_NO_FILAMENT);
            return -1;
        }
        /* the lane's unload is the loaded bay's unload */
        if(m->toolheads[th].loaded_unit >= 0 && m->toolheads[th].loaded_unit < (int)m->unit_count &&
           m->toolheads[th].loaded_slot >= 0 &&
           m->toolheads[th].loaded_slot < (int)m->units[m->toolheads[th].loaded_unit].slot_count &&
           (!unit_ready(m, m->toolheads[th].loaded_unit, why) ||
            !bay_may(m, m->toolheads[th].loaded_unit, m->toolheads[th].loaded_slot,
                    MMU_BAY_ACT_UNLOAD, why)))
            return -1;
        wr_cmd(w, "OPENAMS_UNLOAD FPS=%s", m->toolheads[th].id);
        return 1;
    }

    /* `stop` / `stop <toolhead id>` (UNIFIED_UI 4g): stops whatever sequence
     * runs on the lane. With the host's own stop (the component's `stop`),
     * the line is the RPC server.openams.stop, not G-code. */
    {
        const char *lane = NULL;
        int rc = stop_resolve(line, m, &lane, why);

        if(rc == -1) return -1;
        if(rc == 1) {
            if(m->spoolman_stop) return 0;      /* actions_rpc_then() has it */
            wr_cmd(w, "OAMSM_LOAD_FILAMENT_CANCEL FPS=%s", lane);
            return 1;
        }
    }

    arg = rest(line, "reread");
    if(arg) {
        if(!slot_find(m, arg, &unit, &slot)) return 0;
        if(m->units[unit].slots[slot].state == MMU_SLOT_EMPTY) {
            *why = view_str(VS_REASON_BAY_EMPTY);
            return -1;
        }
        gi = unit_host_idx(m, unit);
        if(gi < 0) {
            *why = view_str(VS_REASON_UNIT_UNKNOWN);
            return -1;
        }
        if(!unit_may(&m->units[unit], MMU_UNIT_ACT_RFID_SCAN)) {
            *why = view_str(VS_REASON_NOT_SUPPORTED);
            return -1;
        }
        wr_cmd(w, "OAMS_RFID_SCAN OAMS=%d", gi);
        return 1;
    }

    /* SPOOL= here is the bay index inside OAMS=<host idx> (C3). */
    arg = rest(line, "calibrate");
    if(arg) {
        if(!slot_find(m, arg, &unit, &slot)) return 0;
        if(!unit_ready(m, unit, why)) return -1;
        h = unit_host_idx(m, unit);
        if(h < 0) {
            *why = view_str(VS_REASON_UNIT_UNKNOWN);
            return -1;
        }
        if(!bay_may(m, unit, slot, MMU_BAY_ACT_CALIBRATE, why)) return -1;
        wr_cmd(w, "OAMS_CALIBRATE_PTFE_LENGTH OAMS=%d SPOOL=%d", h, slot);
        return 1;
    }

    /* `dry <unit> target|hours|preset ...` only edits the pending UI values,
     * so only start and stop reach the host. `%n` makes the match exact: the
     * tail has to end where the parse does. It stays -1 unless the format got
     * as far as it, so the arg[n] below never looks before the string. */
    arg = rest(line, "dry");
    if(arg) {
        n = -1;
        if(sscanf(arg, "%39s start %dC %dh%n", a, &t, &h, &n) == 3 && n >= 0 && arg[n] == '\0') {
            unit = unit_find(m, a);
            if(unit < 0) return 0;
            gi = unit_host_idx(m, unit);
            if(gi < 0) {
                *why = view_str(VS_REASON_UNIT_UNKNOWN);
                return -1;
            }
            if(!unit_may(&m->units[unit], MMU_UNIT_ACT_DRYER_START)) {
                *why = actions_dry_reason(&m->units[unit]);
                return -1;
            }
            wr_cmd(w, "OAMS_DRYER_START OAMS=%d TARGET=%d DURATION=%d", gi, t, h * 3600);
            return 1;
        }

        n = -1;
        if(sscanf(arg, "%39s stop%n", a, &n) == 1 && n >= 0 && arg[n] == '\0') {
            unit = unit_find(m, a);
            if(unit < 0) return 0;
            if(m->units[unit].dry_state == MMU_DRY_IDLE) {
                *why = view_str(VS_REASON_DRYER_IDLE);
                return -1;
            }
            gi = unit_host_idx(m, unit);
            if(gi < 0) {
                *why = view_str(VS_REASON_UNIT_UNKNOWN);
                return -1;
            }
            if(!unit_may(&m->units[unit], MMU_UNIT_ACT_DRYER_STOP)) {
                *why = view_str(VS_REASON_NOT_SUPPORTED);
                return -1;
            }
            wr_cmd(w, "OAMS_DRYER_STOP OAMS=%d", gi);
            return 1;
        }

        return 0;
    }

    arg = rest(line, "setting");
    if(arg) {
        const char *name;

        if(sscanf(arg, "%39s %7s", a, val) != 2) return 0;
        if(strcmp(val, "on") == 0) h = 1;
        else if(strcmp(val, "off") == 0) h = 0;
        else return 0;

        name = option_name(a);
        if(!name) return 0;   /* a key the display keeps to itself */
        wr_cmd(w, "OAMSM_SET_OPTION NAME=%s VALUE=%d", name, h);
        return 1;
    }

    /* Both spellings exist in the tree: the brief's `create group <name>` and
     * the model's own `group create <name>` (mmu_model.c). */
    arg = rest(line, "create group");
    if(!arg) arg = rest(line, "group create");
    if(arg) {
        if(sscanf(arg, "%39s", a) != 1) return 0;
        wr_cmd(w, "OAMSM_CREATE_GROUP GROUP=%s", a);
        return 1;
    }

    /* `delete group <name>`: the groups list's own action. The name has to be
     * a group the model holds, and the host refuses to delete one its lane is
     * using - so it is the same predicate the view dims the action by. */
    arg = rest(line, "delete group");
    if(!arg) arg = rest(line, "group delete");
    if(arg) {
        const char *why_group;

        if(sscanf(arg, "%39s", a) != 1) return 0;
        gi = group_find(m, a);
        if(gi < 0) return 0;
        why_group = mmu_group_edit_reason(m, gi);
        if(why_group[0]) {
            *why = why_group;
            return -1;
        }
        wr_cmd(w, "OAMSM_DELETE_GROUP GROUP=%s", a);
        return 1;
    }

    /* `change group <choice> <slot>`: the display's "Change group..." form
     * submits. The choice is an index into the group's own table, or one of
     * the two sentinels the view writes for the other two rows of the list,
     * so the line stays short and the mapper is the one that knows what the
     * host calls each command. */
    arg = rest(line, "change group");
    if(arg) {
        int choice;
        const char *why_group;

        if(sscanf(arg, "%d %39s", &choice, a) != 2) return 0;
        if(!slot_find(m, a, &unit, &slot)) return 0;
        gi = mmu_slot_group_in(m, unit, slot);

        /* The lane's own rules stop every change first, whatever the choice:
         * a load/unload or a runout in progress, or the bay's group being
         * the one the lane is running. */
        why_group = mmu_group_edit_reason(m, gi);
        if(why_group[0]) {
            *why = why_group;
            return -1;
        }

        if(choice == MMU_GROUP_CHOICE_NONE) {
            if(gi < 0) {
                *why = view_str(VS_REASON_NOT_GROUPED);
                return -1;
            }
            wr_cmd(w, "OAMSM_UNASSIGN_BAY GROUP=%s OAMS=%s BAY=%d",
                   m->groups[gi].name, m->units[unit].name, slot);
            return 1;
        }
        if(choice == MMU_GROUP_CHOICE_NEW) {
            /* A new group and the bay that fills it, in one script: the host
             * takes them in order, so the bay is never in a group that does
             * not exist. */
            char name[MMU_GROUP_NAME_LEN];

            mmu_group_next_name(m, name, sizeof name);
            wr_cmd(w, "OAMSM_CREATE_GROUP GROUP=%s", name);
            wr_cmd(w, "OAMSM_ASSIGN_BAY GROUP=%s OAMS=%s BAY=%d",
                   name, m->units[unit].name, slot);
            return 1;
        }
        if(choice < 0 || choice >= (int)m->group_count) return 0;
        /* And the destination: changing the loaded group's membership would
         * change what the lane loads next. */
        if(choice != gi) {
            why_group = mmu_group_edit_reason(m, choice);
            if(why_group[0]) {
                *why = why_group;
                return -1;
            }
        }
        wr_cmd(w, "OAMSM_ASSIGN_BAY GROUP=%s OAMS=%s BAY=%d",
               m->groups[choice].name, m->units[unit].name, slot);
        return 1;
    }

    /* `link <slot> <spool_id>`: Spoolman spool link via G-code (C3). Checked
     * before `assign` so the two-word prefix does not eat the first token. */
    arg = rest(line, "link");
    if(arg) {
        int spool;

        if(sscanf(arg, "%39s %d", a, &spool) != 2) return 0;
        if(!slot_find(m, a, &unit, &slot)) return 0;
        wr_cmd(w, "OAMSM_SET_BAY_SPOOL OAMS=%s BAY=%d SPOOL=%d SOURCE=manual",
               m->units[unit].name, slot, spool);
        return 1;
    }

    /* `assign spool` before `assign`: the former's second word otherwise
     * parses as a group name. */
    arg = rest(line, "assign spool");
    if(arg) {
        int spool;

        if(sscanf(arg, "%39s %d", a, &spool) != 2) return 0;
        if(!slot_find(m, a, &unit, &slot)) return 0;
        wr_cmd(w, "OAMSM_SET_BAY_SPOOL OAMS=%s BAY=%d SPOOL=%d SOURCE=manual",
               m->units[unit].name, slot, spool);
        return 1;
    }

    arg = rest(line, "assign");
    if(arg) {
        if(sscanf(arg, "%39s %39s", a, b) != 2) return 0;
        if(!slot_find(m, b, &unit, &slot)) return 0;
        wr_cmd(w, "OAMSM_ASSIGN_BAY GROUP=%s OAMS=%s BAY=%d", a, m->units[unit].name, slot);
        return 1;
    }

    arg = rest(line, "unassign");
    if(arg) {
        if(!slot_find(m, arg, &unit, &slot)) return 0;
        gi = mmu_slot_group_in(m, unit, slot);
        if(gi < 0) {
            *why = view_str(VS_REASON_NOT_GROUPED);
            return -1;
        }
        wr_cmd(w, "OAMSM_UNASSIGN_BAY GROUP=%s OAMS=%s BAY=%d",
               m->groups[gi].name, m->units[unit].name, slot);
        return 1;
    }

    arg = rest(line, "unlink");
    if(arg) {
        if(!slot_find(m, arg, &unit, &slot)) return 0;
        if(m->units[unit].slots[slot].spool_id < 0) {
            *why = view_str(VS_REASON_NO_LINK);
            return -1;
        }
        wr_cmd(w, "OAMSM_CLEAR_BAY_SPOOL OAMS=%s BAY=%d", m->units[unit].name, slot);
        return 1;
    }

    if(strcmp(line, "fault clear_errors") == 0) {
        wr_cmd(w, "OAMSM_CLEAR_ERRORS");
        return 1;
    }

    arg = rest(line, "fault clear_fault");
    if(arg) {
        unit = unit_find(m, arg);
        if(unit < 0) return 0;
        gi = unit_host_idx(m, unit);
        if(gi < 0) {
            *why = view_str(VS_REASON_UNIT_UNKNOWN);
            return -1;
        }
        if(!unit_may(&m->units[unit], MMU_UNIT_ACT_CLEAR_FAULT)) {
            *why = view_str(VS_REASON_NOT_SUPPORTED);
            return -1;
        }
        wr_cmd(w, "OAMS_CLEAR_FAULT OAMS=%d", gi);
        return 1;
    }

    arg = rest(line, "fault retry_load");
    if(arg) {
        if(sscanf(arg, "%39s", a) != 1) return 0;
        wr_cmd(w, "OAMSM_LOAD_FILAMENT GROUP=%s", a);
        return 1;
    }

    /* `fault resume` and the older bare `resume` from the alert buttons. */
    if(strcmp(line, "fault resume") == 0 || strcmp(line, "resume") == 0) {
        wr_cmd(w, "RESUME");
        return 1;
    }

    return 0;
}

/* ------------------------------------------------------------------ entry */

/* The one map, and the reason with it. @p why is a pointer to a fixed string
 * from the view's table (or the one literal here), never to a copy, so it
 * outlives the call and a translation of any length reaches the view whole. */
static int map_line(const char *line, const mmu_model_t *m,
                    char *out, size_t out_len, const char **why)
{
    wr_t w;
    const char *reason = view_str(VS_REASON_CMD_TOO_LONG);
    int rc;

    if(why) *why = "";
    if(!line || !m) return 0;

    wr_open(&w, out, out_len);
    rc = dispatch(&w, line, m, &reason);

    if(rc == 1 && w.over) rc = -1;
    if(rc == -1) {
        if(why) *why = reason;
        if(out && out_len) snprintf(out, out_len, "%s", reason);
        return -1;
    }
    return rc;
}

int actions_map(const char *line, const mmu_model_t *m, char *out, size_t out_len)
{
    return map_line(line, m, out, out_len, NULL);
}

/* ------------------------------------------------------------- RPC mapper */

/* One `key=value` token of a `spool edit` line, advancing @p p past it.
 * False at the end of the line or for a token that is not of that shape. */
static bool edit_token(const char **p, char *key, size_t klen, char *val, size_t vlen)
{
    const char *s = *p;
    size_t k = 0, v = 0;

    while(*s == ' ') s++;
    if(!*s) return false;
    while(*s && *s != '=' && *s != ' ') {
        if(k + 1 >= klen) return false;
        key[k++] = *s++;
    }
    if(*s != '=' || k == 0) return false;
    s++;
    while(*s && *s != ' ') {
        if(v + 1 >= vlen) return false;
        val[v++] = *s++;
    }
    if(v == 0) return false;
    key[k] = '\0';
    val[v] = '\0';
    *p = s;
    return true;
}

/* A whole, optionally signed decimal integer; false for anything else. */
static bool edit_int(const char *text, long *out)
{
    char *end;
    long v;

    if(!text[0]) return false;
    v = strtol(text, &end, 10);
    if(*end != '\0') return false;
    *out = v;
    return true;
}

/* A plain decimal number (the pressure advance), read as whole thousandths:
 * an optional sign, digits and an optional point, at least one digit; no
 * exponent, no "inf", nothing else. The fourth decimal rounds half up and
 * nothing past it counts, so the two cores read a line the same way and no
 * floating point is involved. A whole part past 1000 saturates there, which the
 * range check refuses all the same. */
static bool edit_milli(const char *text, long *out)
{
    const char *p = text;
    long whole = 0, milli = 0;
    int digits = 0, frac = 0;
    bool neg = false, up = false;

    if(*p == '+' || *p == '-') neg = (*p++ == '-');
    for(; *p >= '0' && *p <= '9'; p++, digits++) {
        if(whole <= 1000) whole = whole * 10 + (*p - '0');   /* past 1000 it only has to stay past it */
    }
    if(whole > 1000) whole = 1001;
    if(*p == '.') {
        for(p++; *p >= '0' && *p <= '9'; p++, digits++, frac++) {
            if(frac < 3) milli = milli * 10 + (*p - '0');
            else if(frac == 3) up = (*p >= '5');
        }
    }
    if(*p != '\0' || digits == 0) return false;
    while(frac < 3) { milli *= 10; frac++; }
    *out = whole * 1000 + milli + (up ? 1 : 0);
    if(neg && *out != 0) *out = -*out;
    return true;
}

/* `spool edit <slot> [material=<n>] [color=<rgb>] [vendor=<n>] [remaining=<g>]
 * [initial=<g>] [pa=<x>] [then load]` -> server.openams_spoolman.edit. The
 * selects' values are the form's own: material and vendor are indices into the
 * lists the edit form offers (mmu_material_name, mmu_edit_vendor_name; the
 * count itself is "the bay's own"), color is 0xRRGGBB as a decimal number or
 * -1 for "none", the weights are grams and pa is the pressure advance itself. A
 * key left out is a field left as it is, and so is one equal to what the form
 * starts from (mmu_edit_defaults): only a change is sent. A names-with-spaces
 * vendor never appears in the line, only its index, so the line has one shape
 * for both renderers and no quoting.
 *
 * ` then load` is the one thing that may follow: the line a "Load" on an
 * unidentified bay sends, which saves what the user chose and loads in one go.
 * It is handed back in @p then_line for the caller to send once the host has
 * taken the edit. A material of -1 is that form's unanswered choice, and is
 * refused with its own words rather than as a stale list. Returns 0 when
 * @p args is no such line. */
static int spool_edit(const char *args, const mmu_model_t *m, cJSON **params,
                      char *then_line, size_t tlen, const char **why)
{
    char id[MMU_SLOT_ID_LEN], key[16], val[24];
    mmu_edit_defaults_t d;
    const mmu_slot_t *s;
    const char *p = args;
    size_t n = 0;
    int unit, slot;
    bool has[6] = { false };
    bool then_load = false;
    long material = 0, color = 0, vendor = 0, remaining = 0, initial = 0;
    long pa_milli = 0;
    long eff_remaining, eff_initial;
    int pa_x1000;
    bool create;
    cJSON *o;

    while(*p == ' ') p++;
    while(*p && *p != ' ') {
        if(n + 1 >= sizeof id) return 0;
        id[n++] = *p++;
    }
    id[n] = '\0';
    if(!n || !slot_find(m, id, &unit, &slot)) return 0;

    while(edit_token(&p, key, sizeof key, val, sizeof val)) {
        bool ok;

        if(strcmp(key, "material") == 0)       { ok = edit_int(val, &material);  has[0] = ok; }
        else if(strcmp(key, "color") == 0)     { ok = edit_int(val, &color);     has[1] = ok; }
        else if(strcmp(key, "vendor") == 0)    { ok = edit_int(val, &vendor);    has[2] = ok; }
        else if(strcmp(key, "remaining") == 0) { ok = edit_int(val, &remaining); has[3] = ok; }
        else if(strcmp(key, "initial") == 0)   { ok = edit_int(val, &initial);   has[4] = ok; }
        else if(strcmp(key, "pa") == 0)        { ok = edit_milli(val, &pa_milli); has[5] = ok; }
        else return 0;
        if(!ok) return 0;
    }
    while(*p == ' ') p++;
    if(*p) {
        if(strcmp(p, "then load") != 0) return 0;
        then_load = true;
    }

    /* what the bay can do now, the view's dimming and this refusal in one */
    /* a load the host does not offer must not follow a save, or a spool would
     * be saved for a load that cannot happen: a unit that is not ready says so
     * first, a bay list without `l` once the bay itself is known to be there */
    if(then_load && !unit_ready(m, unit, why)) return -1;
    if(!m->spoolman_edit) { *why = view_str(VS_REASON_EDIT_NO_SPOOLMAN); return -1; }
    if(!m->spoolman_online) { *why = view_str(VS_REASON_SPOOLMAN_OFFLINE); return -1; }
    s = &m->units[unit].slots[slot];
    if(s->state == MMU_SLOT_EMPTY) { *why = view_str(VS_REASON_BAY_EMPTY); return -1; }
    if(then_load && !bay_may(m, unit, slot, MMU_BAY_ACT_LOAD, why)) return -1;
    if(then_load && has[0] && material == -1) {
        *why = view_str(VS_MATERIAL_CHOOSE);
        return -1;
    }

    mmu_edit_defaults(m, unit, slot, &d);
    create = s->spool_id < 0;

    eff_remaining = has[3] ? remaining : d.remaining_g;
    eff_initial = has[4] ? initial : d.initial_g;
    pa_x1000 = has[5] ? (int)(pa_milli > 100000L ? 100000L : pa_milli) : d.pa_x1000;
    if(eff_remaining < 0 || eff_initial <= 0 || eff_remaining > 100000 || eff_initial > 100000 ||
       pa_x1000 < 0 || pa_x1000 > 2000 || (has[1] && (color < -1 || color > 0xFFFFFF))) {
        *why = view_str(VS_REASON_EDIT_RANGE);
        return -1;
    }

    o = cJSON_CreateObject();
    if(!o) return 0;
    cJSON_AddStringToObject(o, "unit", m->units[unit].name);
    cJSON_AddNumberToObject(o, "bay", (double)slot);

    /* an index one past the list is the bay's own, which is no change (and a
     * bay with none of its own has no such choice) */
    if((has[0] && (material < 0 || material > MMU_MATERIAL_COUNT ||
                   (material == MMU_MATERIAL_COUNT && !s->material[0]))) ||
       (has[2] && (vendor < 0 || vendor > mmu_edit_vendor_count(m) ||
                   (vendor == mmu_edit_vendor_count(m) && !s->brand[0])))) {
        cJSON_Delete(o);
        *why = view_str(VS_REASON_EDIT_STALE);
        return -1;
    }

    /* a new spool needs its material, so it is always sent with one */
    if(create || (has[0] && material != MMU_MATERIAL_COUNT && material != d.material)) {
        long k = has[0] ? material : d.material;
        const char *name = (k == MMU_MATERIAL_COUNT) ? s->material : mmu_material_name((int)k);

        cJSON_AddStringToObject(o, "material", name);
    }
    if(has[1] && color >= 0 && color != d.color) {
        char hex[8];

        snprintf(hex, sizeof hex, "%06X", (unsigned)(color & 0xFFFFFF));
        cJSON_AddStringToObject(o, "color_hex", hex);
    }
    if(has[2] && vendor != mmu_edit_vendor_count(m) && vendor != d.vendor)
        cJSON_AddStringToObject(o, "vendor", mmu_edit_vendor_name(m, (int)vendor));
    if((has[3] && remaining != d.remaining_g) || (has[4] && initial != d.initial_g)) {
        if(eff_remaining > eff_initial) {
            cJSON_Delete(o);
            *why = view_str(VS_REASON_EDIT_EXCEEDS);
            return -1;
        }
        if(has[3] && remaining != d.remaining_g)
            cJSON_AddNumberToObject(o, "remaining_weight", (double)remaining);
        if(has[4] && initial != d.initial_g)
            cJSON_AddNumberToObject(o, "initial_weight", (double)initial);
    }
    if(has[5] && pa_x1000 != d.pa_x1000)
        cJSON_AddNumberToObject(o, "flow_k", (double)pa_x1000 / 1000.0);

    /* the unit and bay alone are no edit; a new spool is */
    if(!create && cJSON_GetArraySize(o) <= 2) {
        cJSON_Delete(o);
        *why = view_str(VS_EDIT_NO_CHANGES);
        return -1;
    }
    /* what the caller sends once the host has taken this: the load that
     * ` then load` asked for, and nothing at all without it. */
    if(then_line && tlen) {
        if(then_load) snprintf(then_line, tlen, "load %s", id);
        else then_line[0] = '\0';
    }
    *params = o;
    return 1;
}

int actions_rpc_then(const char *line, const mmu_model_t *m,
                     char *method, size_t mlen, cJSON **params,
                     char *then_line, size_t tlen, const char **why)
{
    char id[MMU_SLOT_ID_LEN];
    int grams, unit, slot;
    char bay[48];
    const char *arg;

    if(why) *why = "";
    if(then_line && tlen) then_line[0] = '\0';
    if(!line || !m || !method || !mlen || !params) return 0;

    /* `stop` with the host's own stop → server.openams.stop {lane}: the answer
     * is "stopping" or "nothing running" */
    if(m->spoolman_stop) {
        const char *lane = NULL, *reason = "";
        int rc = stop_resolve(line, m, &lane, &reason);

        if(rc == -1) {
            if(why) *why = reason;
            return -1;
        }
        if(rc == 1) {
            cJSON *p = cJSON_CreateObject();

            if(!p) return 0;
            cJSON_AddStringToObject(p, "lane", lane);
            snprintf(method, mlen, "server.openams.stop");
            *params = p;
            return 1;
        }
    }

    /* `spool list` → server.spoolman.proxy GET /v1/spool */
    if(strcmp(line, "spool list") == 0) {
        cJSON *p = cJSON_CreateObject();
        if(!p) return 0;
        cJSON_AddStringToObject(p, "request_method", "GET");
        cJSON_AddStringToObject(p, "path", "/v1/spool");
        snprintf(method, mlen, "server.spoolman.proxy");
        *params = p;
        return 1;
    }

    /* `vendor list` → server.spoolman.proxy GET /v1/vendor: the names the
     * spool editor offers */
    if(strcmp(line, "vendor list") == 0) {
        cJSON *p = cJSON_CreateObject();
        if(!p) return 0;
        cJSON_AddStringToObject(p, "request_method", "GET");
        cJSON_AddStringToObject(p, "path", "/v1/vendor");
        snprintf(method, mlen, "server.spoolman.proxy");
        *params = p;
        return 1;
    }

    /* `spool edit <slot> ...` → server.openams_spoolman.edit */
    arg = rest(line, "spool edit");
    if(arg) {
        const char *reason = "";
        cJSON *p = NULL;
        int rc = spool_edit(arg, m, &p, then_line, tlen, &reason);

        if(rc == 1) {
            snprintf(method, mlen, "server.openams_spoolman.edit");
            *params = p;
        }
        else if(rc < 0 && why) *why = reason;
        return rc;
    }

    /* `spool confirm <slot_id> <grams>` → server.openams_spoolman.confirm */
    if(strncmp(line, "spool confirm ", 14) == 0) {
        if(sscanf(line + 14, "%39s %d", id, &grams) != 2) return 0;
        if(!slot_find(m, id, &unit, &slot)) return 0;
        snprintf(bay, sizeof bay, "%s-%d", m->units[unit].name, slot);
        {
            cJSON *p = cJSON_CreateObject();
            if(!p) return 0;
            cJSON_AddStringToObject(p, "bay", bay);
            cJSON_AddNumberToObject(p, "remaining_g", (double)grams);
            snprintf(method, mlen, "server.openams_spoolman.confirm");
            *params = p;
            return 1;
        }
    }

    /* `job metadata <filename>` → server.files.metadata {"filename": ...} */
    if(strncmp(line, "job metadata ", 13) == 0) {
        const char *fn = line + 13;
        cJSON *p;

        if(!fn[0]) return 0;
        p = cJSON_CreateObject();
        if(!p) return 0;
        cJSON_AddStringToObject(p, "filename", fn);
        snprintf(method, mlen, "server.files.metadata");
        *params = p;
        return 1;
    }

    return 0;
}

int actions_rpc_tag(const char *method, const cJSON *params)
{
    if(!method) return ACTIONS_RPC_NONE;
    if(strcmp(method, "server.openams_spoolman.edit") == 0) return ACTIONS_RPC_EDIT;
    if(strcmp(method, "server.files.metadata") == 0) return ACTIONS_RPC_METADATA;
    if(strcmp(method, "server.spoolman.proxy") == 0) {
        const cJSON *path = cJSON_GetObjectItemCaseSensitive(params, "path");

        if(cJSON_IsString(path) && strcmp(path->valuestring, "/v1/vendor") == 0)
            return ACTIONS_RPC_VENDOR_LIST;
        return ACTIONS_RPC_SPOOL_LIST;
    }
    return ACTIONS_RPC_NONE;
}

int actions_rpc_why(const char *line, const mmu_model_t *m,
                    char *method, size_t mlen, cJSON **params, const char **why)
{
    return actions_rpc_then(line, m, method, mlen, params, NULL, 0, why);
}

int actions_rpc(const char *line, const mmu_model_t *m,
                char *method, size_t mlen, cJSON **params)
{
    int rc = actions_rpc_why(line, m, method, mlen, params, NULL);

    return rc == 1 ? 1 : 0;
}

/* --------------------------------------------------------------- predicate */

int actions_check(const char *line, const mmu_model_t *m, const char **why)
{
    char buf[128];
    char method[64];
    const char *rpc_why = "";
    cJSON *params = NULL;
    int rc;

    if(why) *why = "";
    if(!line || !m) return 0;

    /* The reason comes back as a pointer to a const table entry, so every
     * action a builder fills keeps its own: a scratch buffer here would be
     * overwritten by the next action's check. */
    rc = map_line(line, m, buf, sizeof buf, why);
    if(rc == 1) return 1;
    if(rc == -1) return 0;

    /* Not a G-code line: try the RPC vocabulary ("spool confirm ..." only
     * ever resolves this way) before giving up. */
    rc = actions_rpc_why(line, m, method, sizeof method, &params, &rpc_why);
    if(rc == 1) {
        cJSON_Delete(params);
        return 1;
    }
    if(rc == -1) {
        /* "No changes to save" is about this submission, not about the action:
         * the form's own starting values are always that, and the action is
         * still there to open. */
        if(rpc_why == view_str(VS_EDIT_NO_CHANGES)) return 1;
        if(why) *why = rpc_why;
        return 0;
    }

    /* Neither map recognizes @p line at all. A builder should never dry-run
     * a line it did not itself build from the host vocabulary, so this is
     * not expected in practice; report it enabled with no reason rather than
     * silently hiding a real action behind a false negative. */
    return 1;
}
