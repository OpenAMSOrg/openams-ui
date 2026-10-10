/**
 * @file status_map.c
 * OpenAMS status -> mmu_model_t. See status_map.h and docs/contracts/C2_status.md 6.
 */

#include "backend/status_map.h"

#include <ctype.h>
#include <stdint.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "model/mmu_model.h"
#include "view/strings.h"

/* ------------------------------------------------------------------ cJSON */

static const cJSON *field(const cJSON *obj, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(obj, key);
}

/* The string value of @p key, or NULL when it is absent or not a string
 * (an explicit JSON null included). */
static const char *jstring(const cJSON *obj, const char *key)
{
    const cJSON *j = field(obj, key);
    return cJSON_IsString(j) ? j->valuestring : NULL;
}

/* The integer value of @p key; false when it is absent, a JSON null, or not a
 * number. */
static bool jint(const cJSON *obj, const char *key, int *out)
{
    const cJSON *j = field(obj, key);
    if(!cJSON_IsNumber(j)) return false;
    if(out) *out = (int)j->valuedouble;
    return true;
}

/* The value of @p key as whole thousandths, the unit the edit form works in
 * (edit_milli rounds the fourth decimal the same way); false when it is
 * absent, a JSON null, or not a number. */
static bool jmilli(const cJSON *obj, const char *key, int *out)
{
    const cJSON *j = field(obj, key);
    double v;

    if(!cJSON_IsNumber(j)) return false;
    v = j->valuedouble * 1000.0;
    if(out) *out = (int)(v >= 0.0 ? v + 0.5 : v - 0.5);
    return true;
}

/* True when @p key is a number from @p lo to @p hi (a guard before a cast). */
static bool jnum_in(const cJSON *obj, const char *key, double lo, double hi)
{
    const cJSON *j = field(obj, key);

    return cJSON_IsNumber(j) && j->valuedouble >= lo && j->valuedouble <= hi;
}

/* Copy the boolean @p key into @p out when it is present. */
static bool jbool(const cJSON *obj, const char *key, bool *out)
{
    const cJSON *j = field(obj, key);
    if(!cJSON_IsBool(j)) return false;
    *out = cJSON_IsTrue(j);
    return true;
}

/* Whether the string array @p arr holds @p want. */
static bool array_has(const cJSON *arr, const char *want)
{
    const cJSON *j;

    if(!cJSON_IsArray(arr)) return false;
    cJSON_ArrayForEach(j, arr) {
        if(cJSON_IsString(j) && strcmp(j->valuestring, want) == 0) return true;
    }
    return false;
}

/* ---------------------------------------------------------------- helpers */

/* A slot with nothing known about it. The model's own slot_empty() marks a bay
 * "configured", which is right for the dummy scenarios and wrong here, where
 * the host has not said anything yet. */
static void slot_clear(mmu_slot_t *s)
{
    memset(s, 0, sizeof(*s));
    s->remaining_pct = -1;
    s->nozzle_min_c = -1;
    s->nozzle_max_c = -1;
    s->k_value_x1000 = -1;
    s->spool_id = -1;
    s->remaining_g = -1;
    s->initial_g = -1;
    s->flow_k_x1000 = -1;
    s->state = MMU_SLOT_EMPTY;
    s->info_source = MMU_INFO_UNKNOWN;
    s->cal_state = MMU_CAL_UNKNOWN;
    s->global_id = -1;
}

/* A unit is found by its whole name. The stored name is the host's, cut to
 * MMU_UNIT_NAME_LEN at a character boundary, so the key is cut the same way
 * before it is compared: an over-long name is one unit, every time, and never
 * a prefix of another. */
static int unit_index(const mmu_model_t *m, const char *name)
{
    char key[MMU_UNIT_NAME_LEN];
    int i;

    if(!name) return -1;
    mmu_copy_text(key, sizeof key, name);
    for(i = 0; i < (int)m->unit_count; i++) {
        if(strcmp(m->units[i].name, key) == 0) return i;
    }
    return -1;
}

/* The unit @p name, created when it is new. A unit first seen through its
 * Spoolman links has no `family`, so it starts on the AMS four-slot defaults
 * until a projection describes it. */
static mmu_unit_t *unit_get_or_create(mmu_model_t *m, const char *name, int min_slots)
{
    mmu_unit_t *u;
    int idx = unit_index(m, name);

    if(idx >= 0) {
        u = &m->units[idx];
    }
    else {
        if(m->unit_count >= MMU_MAX_UNITS) return NULL;

        idx = (int)m->unit_count;
        u = &m->units[idx];
        memset(u, 0, sizeof(*u));
        mmu_copy_text(u->name, sizeof(u->name), name);
        u->kind = MMU_UNIT_4SLOT;
        u->variant = MMU_VARIANT_AMS;
        u->side = MMU_SIDE_NONE;
        u->connected = true;
        u->humidity_level = -1;
        u->humidity_pct = -1;
        u->temp_c = -100;
        u->ambient_c = 24;
        u->dry_state = MMU_DRY_IDLE;
        u->normal_motion = true;
        u->follower_uncertain = false;
        u->host_idx = -1;   /* no host `idx` seen yet; `lane` stays "" */
        u->toolhead = -1;   /* no lane.id seen yet either */
        m->unit_count++;
    }

    if(min_slots > MMU_MAX_SLOTS_PER_UNIT) min_slots = MMU_MAX_SLOTS_PER_UNIT;
    while(u->slot_count < min_slots) {
        slot_clear(&u->slots[u->slot_count]);
        u->slot_count++;
    }

    return u;
}

/* The toolhead (FPS lane) named @p id, created when it is new (C2 6.5): a
 * multi-toolhead host keys its lanes by lane.id, so each gets its own copy of
 * what used to be the model's single set of globals, and one lane's
 * projection no longer overwrites another's. */
static mmu_toolhead_t *toolhead_get_or_create(mmu_model_t *m, const char *id)
{
    mmu_toolhead_t *th;
    int i;

    char key[MMU_LANE_ID_LEN];

    if(!id) id = "";
    mmu_copy_text(key, sizeof key, id);   /* stored ids are cut the same way */
    for(i = 0; i < (int)m->toolhead_count; i++) {
        if(strcmp(m->toolheads[i].id, key) == 0) return &m->toolheads[i];
    }
    if(m->toolhead_count >= MMU_MAX_TOOLHEADS) return NULL;

    th = &m->toolheads[m->toolhead_count];
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
    m->toolhead_count++;
    return th;
}

/* "<unit>-<bay>": the name up to the last dash and the bay after it. */
static bool ref_split(const char *ref, char *name, size_t name_len, int *out_slot)
{
    const char *dash;
    const char *p;
    size_t n;

    if(out_slot) *out_slot = -1;
    if(!ref) return false;

    dash = strrchr(ref, '-');
    if(!dash || !dash[1]) return false;
    for(p = dash + 1; *p; p++) {
        if(*p < '0' || *p > '9') return false;
    }

    n = (size_t)(dash - ref);
    if(n == 0 || name_len == 0) return false;

    /* a name too long for @p name is cut at a character boundary, as the model
     * cuts its own, so the reference still finds its unit */
    if(n >= name_len) {
        n = name_len - 1;
        while(n > 0 && ((unsigned char)ref[n] & 0xC0) == 0x80) n--;
    }
    memcpy(name, ref, n);
    name[n] = '\0';
    if(out_slot) *out_slot = atoi(dash + 1);
    return true;
}

/* The unit index of @p ref, or -1 when no unit of the model has that name. The
 * bay is filled in either way: a lane aimed at a unit that is not being
 * displayed should still move the lane state. */
static int ref_resolve(const mmu_model_t *m, const char *ref, int *out_slot)
{
    char name[MMU_UNIT_NAME_LEN];

    if(!ref_split(ref, name, sizeof(name), out_slot)) return -1;
    return unit_index(m, name);
}

/* ------------------------------------------------------------------- codes */

/* The klipper-mmu families OpenAMS now sends, and the product name each one
 * carries. They are proper nouns, not sentences: they are never translated and
 * never reach the string tables. A klipper-mmu family only names the unit, so
 * the variant stays whatever it is (AMS by default) and every other behavior
 * - the tile kind, the dryer limits - is the variant's. */
static const char *family_title(const char *family)
{
    static const struct {
        const char *family;
        const char *title;
    } k_titles[] = {
        { "boxturtle",        "BoxTurtle" },
        { "nightowl",         "NightOwl" },
        { "emu",              "EMU" },
        { "kms",              "KMS" },
        { "qidi_box",         "QIDI Box" },
        { "angry_beaver",     "Angry Beaver" },
        { "3ms",              "3MS" },
        { "quattrobox_1_0",   "QuattroBox 1.0" },
        { "quattrobox_1_1",   "QuattroBox 1.1" },
        { "quattrobox_2",     "QuattroBox 2" },
        { "custom",           "Custom MMU" },
        { "ercf_1_1",         "ERCF 1.1" },
        { "ercf_2_0",         "ERCF 2.0" },
        { "ercf_3_0",         "ERCF 3.0" },
        { "tradrack",         "Tradrack" },
        { "3d_chameleon",     "3D Chameleon" },
        { "htlf",             "HTLF" },
        { "mmx6",             "MMX6" },
        { "mmx",              "MMX" },
        { "low_rider",        "Low Rider" },
        { "pico_mmu",         "Pico MMU" },
        { "btt_vivid",        "BTT ViViD" },
        { "claymore",         "Claymore" },
    };
    size_t i;

    for(i = 0; i < sizeof(k_titles) / sizeof(k_titles[0]); i++) {
        if(strcmp(k_titles[i].family, family) == 0) return k_titles[i].title;
    }
    return NULL;
}

static void map_family(mmu_unit_t *u, const char *family)
{
    const char *title = family ? family_title(family) : NULL;

    /* A projection is authoritative about the family's name: one that names no
     * product (an AMS family, or one this build does not know) leaves no title
     * behind, so the unit shows its variant's name as it did before. */
    if(title) mmu_copy_text(u->title, sizeof(u->title), title);
    else u->title[0] = '\0';

    if(!family) return;

    if(strcmp(family, "ams1") == 0) {
        u->variant = MMU_VARIANT_AMS;
        u->kind = MMU_UNIT_4SLOT;
    }
    else if(strcmp(family, "ams2") == 0) {
        u->variant = MMU_VARIANT_AMS2_PRO;
        u->kind = MMU_UNIT_4SLOT;
    }
    else if(strcmp(family, "ams_ht") == 0) {
        u->variant = MMU_VARIANT_AMS_HT;
        u->kind = MMU_UNIT_HT;
    }
    else if(strcmp(family, "inline_follower") == 0) {
        u->variant = MMU_VARIANT_LANE;
        u->kind = MMU_UNIT_LANE;
    }
}

/* `bays[i].s`: empty, ready, loaded, error, or positioning. A positioning bay
 * is one the firmware is inserting into right now (C2 6.2, NEW-v2/H16), so it
 * reads as ready with the flag set rather than as a state of its own. */
static mmu_slot_state_t map_bay_state(const char *code, bool *positioning)
{
    if(positioning) *positioning = false;
    if(!code) return MMU_SLOT_EMPTY;

    switch(code[0]) {
        case 'r': return MMU_SLOT_READY;
        case 'l': return MMU_SLOT_LOADED;
        case 'x': return MMU_SLOT_ERROR;
        case 'p':
            if(positioning) *positioning = true;
            return MMU_SLOT_READY;
        default: return MMU_SLOT_EMPTY;   /* 'e' and anything newer */
    }
}

static mmu_cal_state_t map_cal(const char *code)
{
    if(!code) return MMU_CAL_UNKNOWN;

    switch(code[0]) {
        case 'c': return MMU_CAL_CONFIGURED;
        case 'u': return MMU_CAL_UNCALIBRATED;
        case 'p': return MMU_CAL_CALIBRATING;
        default: return MMU_CAL_UNKNOWN;
    }
}

static mmu_info_source_t map_link_source(const char *src)
{
    if(!src) return MMU_INFO_UNKNOWN;
    if(strcmp(src, "t") == 0) return MMU_INFO_TAG;
    if(strcmp(src, "m") == 0) return MMU_INFO_MANUAL;
    return MMU_INFO_UNKNOWN;
}

/* The C2 5.9 dryer state_name vocabulary. */
static mmu_dry_state_t map_dry_state(const char *name)
{
    if(!name) return MMU_DRY_IDLE;
    if(strcmp(name, "preheat") == 0) return MMU_DRY_HEATING;
    if(strcmp(name, "hold") == 0) return MMU_DRY_DEHUMIDIFYING;
    if(strcmp(name, "timed_dry") == 0) return MMU_DRY_DEHUMIDIFYING;
    if(strcmp(name, "cooldown") == 0) return MMU_DRY_COOLING;
    if(strcmp(name, "fault") == 0) return MMU_DRY_ERROR;
    return MMU_DRY_IDLE;   /* "off", and anything newer */
}

static mmu_severity_t map_severity(const char *sev)
{
    if(!sev) return MMU_SEV_INFO;
    if(strcmp(sev, "stop") == 0) return MMU_SEV_STOP;
    if(strcmp(sev, "pause") == 0) return MMU_SEV_PAUSE;
    return MMU_SEV_INFO;
}

/* "0A2989" -> 0x0A2989 in @p out. False (@p out untouched) for an empty string
 * or anything that is not up to six hex digits: that is a color nobody told us,
 * not black. */
static bool map_hex_color(const char *hex, uint32_t *out)
{
    uint32_t v = 0;
    int n;

    if(!hex || !hex[0]) return false;

    for(n = 0; hex[n]; n++) {
        char c = hex[n];
        uint32_t d;

        if(c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if(c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
        else if(c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
        else return false;

        if(n >= 6) return false;
        v = (v << 4) | d;
    }
    *out = v;
    return true;
}

/* ---------------------------------------------------------------- groups */

/* Built directly instead of through mmu_group_create()/mmu_group_assign():
 * those log every edit as a user action and work on g_mmu alone, while this
 * mapper must honor the model it was handed. Replaying one projection twice
 * then also cannot duplicate a member or reorder a group, which the direct
 * form gives for free. */
static int group_of_slot(const mmu_model_t *m, int unit, int slot)
{
    int g, k;

    for(g = 0; g < (int)m->group_count; g++) {
        for(k = 0; k < (int)m->groups[g].count; k++) {
            if(m->groups[g].members[k].unit == (int8_t)unit &&
               m->groups[g].members[k].slot == (int8_t)slot)
                return g;
        }
    }
    return -1;
}

/* The group named @p name, or -1 when the table does not hold one. */
static int group_index(const mmu_model_t *m, const char *name)
{
    int i;

    if(!name) return -1;
    for(i = 0; i < (int)m->group_count; i++) {
        if(strcmp(m->groups[i].name, name) == 0) return i;
    }
    return -1;
}

static int group_get_or_create(mmu_model_t *m, const char *name)
{
    mmu_group_t *g;
    int i;

    for(i = 0; i < (int)m->group_count; i++) {
        if(strcmp(m->groups[i].name, name) == 0) return i;
    }
    if(m->group_count >= MMU_MAX_GROUPS) return -1;

    g = &m->groups[m->group_count];
    memset(g, 0, sizeof(*g));
    mmu_copy_text(g->name, sizeof(g->name), name);
    return (int)m->group_count++;
}

static void group_add_member(mmu_model_t *m, int group, int unit, int slot)
{
    mmu_group_t *g = &m->groups[group];

    if(g->count >= MMU_MAX_GROUP_MEMBERS) return;
    if(group_of_slot(m, unit, slot) >= 0) return;   /* already a member */

    g->members[g->count].unit = (int8_t)unit;
    g->members[g->count].slot = (int8_t)slot;
    g->count++;
}

/* Remove one bay from a group, the other members keeping their order. */
static void group_remove_member(mmu_model_t *m, int group, int unit, int slot)
{
    mmu_group_t *g = &m->groups[group];
    int i, k = 0;

    for(i = 0; i < (int)g->count; i++) {
        if(g->members[i].unit == (int8_t)unit && g->members[i].slot == (int8_t)slot) continue;
        g->members[k++] = g->members[i];
    }
    g->count = (uint8_t)k;
}

/* Every bay of @p unit leaves every group, the other members keeping their
 * order: an update that lists groups is the host's whole answer for the unit
 * it came from, so a bay it does not list is in no group of that unit. */
static void group_drop_unit(mmu_model_t *m, int unit)
{
    int g, i, k;

    for(g = 0; g < (int)m->group_count; g++) {
        mmu_group_t *grp = &m->groups[g];

        k = 0;
        for(i = 0; i < (int)grp->count; i++) {
            if(grp->members[i].unit == (int8_t)unit) continue;
            grp->members[k++] = grp->members[i];
        }
        grp->count = (uint8_t)k;
    }
}

/* Drop the group at @p group, the table keeping its order, and repair every
 * stored group index: a deleted index reads -1 now, a higher one drops by
 * one. */
static void group_delete(mmu_model_t *m, int group)
{
    int i;

    for(i = group; i + 1 < (int)m->group_count; i++)
        m->groups[i] = m->groups[i + 1];
    m->group_count--;

    for(i = 0; i < (int)m->toolhead_count; i++) {
        int8_t *cur = &m->toolheads[i].current_group;

        if(*cur > group) (*cur)--;
        else if(*cur == group) *cur = -1;
    }
    if(m->current_group > group) m->current_group--;
    else if(m->current_group == group) m->current_group = -1;
}

/* The order groups are listed in: by name, a run of digits read as a number,
 * so "T2" comes before "T10"; anything else compares byte by byte. */
static int group_name_cmp(const char *a, const char *b)
{
    while(*a && *b) {
        if(isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            char *ea, *eb;
            unsigned long x = strtoul(a, &ea, 10);
            unsigned long y = strtoul(b, &eb, 10);

            if(x != y) return x < y ? -1 : 1;
            a = ea;
            b = eb;
            continue;
        }
        if(*a != *b) return (unsigned char)*a < (unsigned char)*b ? -1 : 1;
        a++;
        b++;
    }
    return *a ? 1 : (*b ? -1 : 0);
}

/* Put the table in name order (a stable insertion sort: equal names keep
 * their order) and repair every stored group index, so a group the host lists
 * again takes its place by name instead of the end of the table. */
static void groups_sort(mmu_model_t *m)
{
    int8_t where[MMU_MAX_GROUPS];   /* where[old index] = new index */
    int8_t order[MMU_MAX_GROUPS];   /* order[new index] = old index */
    mmu_group_t sorted[MMU_MAX_GROUPS];
    int n = (int)m->group_count, i, j;

    for(i = 0; i < n; i++) {
        int8_t cur = (int8_t)i;

        for(j = i; j > 0 && group_name_cmp(m->groups[order[j - 1]].name, m->groups[cur].name) > 0; j--)
            order[j] = order[j - 1];
        order[j] = cur;
    }
    for(i = 0; i < n; i++) {
        sorted[i] = m->groups[order[i]];
        where[order[i]] = (int8_t)i;
    }
    for(i = 0; i < n; i++) m->groups[i] = sorted[i];

    for(i = 0; i < (int)m->toolhead_count; i++) {
        int8_t *cur = &m->toolheads[i].current_group;

        if(*cur >= 0 && *cur < n) *cur = where[*cur];
    }
    if(m->current_group >= 0 && m->current_group < n) m->current_group = where[m->current_group];
}

/* The `groups` of one unit's update: the host's whole answer for that unit, so
 * its bays are rebuilt from scratch and a group it stopped listing leaves the
 * table once it holds no bay at all. Without a `groups` field the update is a
 * delta and the groups are left alone. */
static void map_groups(mmu_model_t *m, int unit, const cJSON *groups)
{
    uint8_t listed[MMU_MAX_GROUPS];
    const cJSON *g;
    int gi;

    if(!cJSON_IsArray(groups)) return;

    group_drop_unit(m, unit);
    memset(listed, 0, sizeof(listed));

    cJSON_ArrayForEach(g, groups) {
        const cJSON *members;
        const char *name;

        if(!cJSON_IsObject(g)) continue;
        name = jstring(g, "n");
        if(!name || !name[0]) continue;

        gi = group_get_or_create(m, name);
        if(gi < 0) continue;
        listed[gi] = 1;

        members = field(g, "b");
        /* No `b`: the group holds none of this unit's bays, so whatever it
         * still carries belongs to other units and stays. */
        if(!cJSON_IsArray(members)) continue;

        m->groups[gi].count = 0;   /* this list is the whole membership */
        {
            const cJSON *ref;
            cJSON_ArrayForEach(ref, members) {
                int other, runit, slot;

                if(!cJSON_IsString(ref)) continue;
                runit = ref_resolve(m, ref->valuestring, &slot);
                /* A member on a unit this model does not carry is skipped:
                 * the group keeps the members it can show, in order. */
                if(runit < 0) continue;
                if(slot < 0 || slot >= (int)m->units[runit].slot_count) continue;
                /* A bay another unit's older update left in a different
                 * group moves here: the newest answer wins. */
                other = group_of_slot(m, runit, slot);
                if(other >= 0 && other != gi)
                    group_remove_member(m, other, runit, slot);
                group_add_member(m, gi, runit, slot);
            }
        }
    }

    /* A group the host stopped listing and that holds no bay at all is gone
     * from the lane. Walked from the top, so that no index below one already
     * deleted moves out from under the flags. */
    for(gi = (int)m->group_count - 1; gi >= 0; gi--) {
        if(listed[gi] || m->groups[gi].count > 0) continue;
        group_delete(m, gi);
    }
    groups_sort(m);
}

/* --------------------------------------------------------------- one unit */

static void map_bays(mmu_unit_t *u, const cJSON *bays)
{
    const cJSON *bay;
    int i, n = cJSON_GetArraySize(bays);

    for(i = 0; i < n && i < (int)u->slot_count; i++) {
        const cJSON *sp;
        mmu_slot_t *s = &u->slots[i];

        bay = cJSON_GetArrayItem(bays, i);
        if(!cJSON_IsObject(bay)) continue;

        s->state = map_bay_state(jstring(bay, "s"), &s->positioning);
        if(jstring(bay, "cal")) s->cal_state = map_cal(jstring(bay, "cal"));

        /* `id` is the host's own bay id, what OPENAMS_LOAD wants as SLOT (C2
         * 6.2); it is not the bay index the projection orders `bays` by. */
        {
            int id;
            s->global_id = jint(bay, "id", &id) ? (int16_t)id : -1;
        }

        /* `a` is the host's own list of what this bay offers (C2 6.2). A string
         * (even an empty one) is a list; anything else is "not said", which
         * withholds nothing. */
        {
            const char *letters = jstring(bay, "a");

            s->acts_known = letters != NULL;
            s->acts = 0;
            for(; letters && *letters; letters++) {
                switch(*letters) {
                case 'l': s->acts |= MMU_BAY_ACT_LOAD; break;
                case 'u': s->acts |= MMU_BAY_ACT_UNLOAD; break;
                case 'c': s->acts |= MMU_BAY_ACT_CALIBRATE; break;
                case 'a': s->acts |= MMU_BAY_ACT_ASSIGN; break;
                case 'd': s->acts |= MMU_BAY_ACT_DETACH; break;
                default: s->acts |= MMU_BAY_ACT_OTHER; break;
                }
            }
        }

        /* `sp` is the link only; null means "no spool linked here" (C2 6.2). */
        sp = field(bay, "sp");
        if(cJSON_IsObject(sp)) {
            int id;
            if(jint(sp, "id", &id)) s->spool_id = id;
            s->info_source = map_link_source(jstring(sp, "src"));
        }
        else {
            s->spool_id = -1;
            s->info_source = MMU_INFO_UNKNOWN;
        }
    }
}

static void map_env(mmu_unit_t *u, const cJSON *env)
{
    int v;

    if(cJSON_IsObject(env)) {
        u->has_humidity_pct = jint(env, "rh", &v);
        u->humidity_pct = u->has_humidity_pct ? (int16_t)v : -1;
        u->has_temp = jint(env, "t", &v);
        u->temp_c = u->has_temp ? (int16_t)v : -100;
    }
    else if(env) {   /* explicit null: no environment sensor */
        u->has_humidity_pct = false;
        u->humidity_pct = -1;
        u->has_temp = false;
        u->temp_c = -100;
    }
}

static void map_dryer(mmu_unit_t *u, const cJSON *dryer)
{
    const cJSON *j;
    int v;

    if(cJSON_IsObject(dryer)) {
        u->dry_state = map_dry_state(jstring(dryer, "state"));
        u->can_dry = true;
        u->dry_target_c = jint(dryer, "target", &v) ? (int16_t)v : 0;
        u->dry_remaining_min = jint(dryer, "min", &v) ? (int16_t)v : 0;
        j = field(dryer, "adapter");
        /* A boolean only: null or absent (the AMS HT has no supply monitor)
         * is "unknown", which the view must not show as "adapter missing". */
        u->has_power_adapter = cJSON_IsBool(j) ? true : false;
        if(u->has_power_adapter) u->power_adapter = cJSON_IsTrue(j);
    }
    else if(dryer) {   /* explicit null: this unit has no dryer */
        u->dry_state = MMU_DRY_IDLE;
        u->dry_target_c = 0;
        u->dry_remaining_min = 0;
        u->dry_rotate = false;
        u->can_dry = false;
        u->power_adapter = false;
        u->has_power_adapter = false;
    }
}

/* The unit's own `ready` and `act` (C2 6.2): what the host says the unit can
 * do. Both are optional on the wire as far as this reader goes (an older host
 * sends neither): absent means nothing is withheld, not that everything is.
 * Drying also follows `act`: a unit whose list lacks `dryer_start` cannot dry
 * whatever `dryer` holds, and the reason is the model's `cannot_dry_reason`
 * (the view re-words it in the current language: view_unit_dry_reason()). */
static void map_unit_caps(mmu_unit_t *u, const cJSON *ready, const cJSON *act)
{
    static const struct { const char *name; uint8_t bit; } k_act[] = {
        { "dryer_start", MMU_UNIT_ACT_DRYER_START },
        { "dryer_stop", MMU_UNIT_ACT_DRYER_STOP },
        { "clear_fault", MMU_UNIT_ACT_CLEAR_FAULT },
        { "rfid_scan", MMU_UNIT_ACT_RFID_SCAN },
    };
    size_t i;

    u->ready_known = cJSON_IsBool(ready);
    u->ready = u->ready_known ? cJSON_IsTrue(ready) : true;

    u->act_known = cJSON_IsArray(act);
    u->acts = 0;
    for(i = 0; u->act_known && i < sizeof(k_act) / sizeof(k_act[0]); i++) {
        if(array_has(act, k_act[i].name)) u->acts |= k_act[i].bit;
    }

    u->dry_unlisted = u->act_known && !(u->acts & MMU_UNIT_ACT_DRYER_START);
    if(u->dry_unlisted) {
        mmu_copy_text(u->cannot_dry_reason, sizeof(u->cannot_dry_reason),
                      view_str(u->has_power_adapter && !u->power_adapter ? VS_ALERT_ADAPTER
                                                                         : VS_REASON_NOT_SUPPORTED));
    }
    else {
        u->cannot_dry_reason[0] = '\0';
    }
}

/* `lane.stage` is null or [plan, index, failed, detail, labels] (C2 5.3, H2,
 * H3): `plan` is the ordered list of step names, `index` the running step or
 * null, `failed` the name of the step that failed or null, `detail` the
 * running step's own detail or null, and `labels` a {name: label} object for
 * the steps that carry one. A 3-element array (an older printer) is read the
 * same way and simply has no detail and no labels. A null or a malformed value
 * clears the steps. */
static void map_stage(mmu_toolhead_t *th, const cJSON *stage)
{
    const cJSON *jplan, *jidx, *jfail, *jlabels;
    int i, count;

    memset(th->steps, 0, sizeof(th->steps));
    th->step_count = 0;
    th->step_current = -1;
    th->step_failed = -1;
    th->step_detail[0] = '\0';

    if(!cJSON_IsArray(stage) || cJSON_GetArraySize(stage) < 3) return;

    jplan = cJSON_GetArrayItem(stage, 0);
    jidx = cJSON_GetArrayItem(stage, 1);
    jfail = cJSON_GetArrayItem(stage, 2);
    if(!cJSON_IsArray(jplan)) return;

    /* The labels of the five-element form; an older printer's array has no
     * room for them, and every step then shows its own name. */
    jlabels = cJSON_GetArraySize(stage) >= 5 ? cJSON_GetArrayItem(stage, 4) : NULL;
    if(!cJSON_IsObject(jlabels)) jlabels = NULL;

    count = cJSON_GetArraySize(jplan);
    if(count > MMU_MAX_STEPS) count = MMU_MAX_STEPS;
    for(i = 0; i < count; i++) {
        const cJSON *j = cJSON_GetArrayItem(jplan, i);
        const cJSON *jl;
        const char *text;

        if(!cJSON_IsString(j)) continue;
        text = j->valuestring;
        /* A label written by the user (H3) is the step's display text as it
         * stands; the view never translates it. */
        jl = jlabels ? field(jlabels, text) : NULL;
        if(cJSON_IsString(jl) && jl->valuestring[0]) text = jl->valuestring;
        mmu_copy_text(th->steps[i], sizeof(th->steps[i]), text);
    }
    th->step_count = (uint8_t)count;

    if(cJSON_IsNumber(jidx)) th->step_current = (int8_t)(int)jidx->valuedouble;

    /* `failed` names a plan entry: its position is the failed step, so the
     * load screen can mark it (H2). A name not in the plan is not a step. */
    if(cJSON_IsString(jfail)) {
        for(i = 0; i < count; i++) {
            const cJSON *j = cJSON_GetArrayItem(jplan, i);
            if(cJSON_IsString(j) && strcmp(j->valuestring, jfail->valuestring) == 0) {
                th->step_failed = (int8_t)i;
                break;
            }
        }
    }

    /* The detail belongs to the running step (H3); the view is what puts it
     * beside the step's own text. */
    if(cJSON_GetArraySize(stage) >= 4) {
        const cJSON *j = cJSON_GetArrayItem(stage, 3);
        if(cJSON_IsString(j))
            mmu_copy_text(th->step_detail, sizeof(th->step_detail), j->valuestring);
    }
}

static void map_runout(mmu_model_t *m, mmu_toolhead_t *th, const cJSON *lane)
{
    const char *runout = jstring(lane, "runout");
    int unit, slot;

    th->runout_active = (runout && strcmp(runout, "idle") != 0);

    unit = ref_resolve(m, jstring(lane, "from"), &slot);
    th->runout_from_unit = (unit >= 0) ? (int8_t)unit : -1;
    th->runout_from_slot = (unit >= 0 && slot >= 0) ? (int8_t)slot : -1;

    unit = ref_resolve(m, jstring(lane, "to"), &slot);
    th->runout_to_unit = (unit >= 0) ? (int8_t)unit : -1;
    th->runout_to_slot = (unit >= 0 && slot >= 0) ? (int8_t)slot : -1;
}

/* Returns true when the lane published `ms`, so the caller leaves
 * motion_settled alone instead of deriving it. Writes into @p th (the
 * toolhead looked up by lane.id, C2 6.5), not the model's legacy globals, so
 * one lane's projection cannot overwrite another's. */
static bool map_lane(mmu_model_t *m, mmu_toolhead_t *th, const cJSON *lane)
{
    const char *op;
    const char *lgroup;
    const cJSON *ms;
    int unit, slot, gi;

    if(!cJSON_IsObject(lane)) return false;

    /* `lane.ext` names the lane's extruder (C2 6.2/19); "" when absent. */
    {
        const char *ext = jstring(lane, "ext");
        if(ext) mmu_copy_text(th->extruder, sizeof(th->extruder), ext);
    }

    /* `lane.ms` is the host's own motion-safety predicate (C2 4.2/6.5); a UI
     * takes it as published rather than re-deriving the rule. */
    ms = field(lane, "ms");
    if(cJSON_IsBool(ms)) th->motion_settled = cJSON_IsTrue(ms);

    op = jstring(lane, "op");
    unit = ref_resolve(m, jstring(lane, "bay"), &slot);

    /* `lane.group` names the group loaded in the toolhead (C2 6.5); it is only
     * published while something is loaded. -1 when the table does not hold it,
     * and the loaded bay's own membership is the fallback. */
    lgroup = jstring(lane, "group");
    gi = group_index(m, lgroup);

    /* lane.op owns what the toolhead holds and whether a transfer is running
     * (C2 6.5). Everything but LOADED leaves the toolhead empty in the model;
     * the projection does not carry the previous filament. */
    if(op && strcmp(op, "LOADED") == 0) {
        th->busy = MMU_BUSY_NONE;
        th->busy_unit = -1;
        th->busy_slot = -1;

        if(unit >= 0 && slot >= 0 && slot < (int)m->units[unit].slot_count) {
            th->loaded = true;
            th->loaded_unit = (int8_t)unit;
            th->loaded_slot = (int8_t)slot;
            th->loaded_ext = -1;
            th->toolhead_sensor = true;
            th->current_group = (int8_t)(gi >= 0 ? gi : group_of_slot(m, unit, slot));
        }
        else {
            th->loaded = false;
            th->loaded_unit = -1;
            th->loaded_slot = -1;
            th->loaded_ext = -1;
            th->toolhead_sensor = false;
            th->current_group = -1;
        }
    }
    else {
        if(op && strcmp(op, "LOADING") == 0) {
            th->busy = MMU_BUSY_LOAD;
        }
        else if(op && strcmp(op, "UNLOADING") == 0) {
            th->busy = MMU_BUSY_UNLOAD;
        }
        else {
            th->busy = MMU_BUSY_NONE;   /* UNLOADED, CALIBRATING, unknown */
        }

        th->busy_unit = (th->busy != MMU_BUSY_NONE && unit >= 0) ? (int8_t)unit : -1;
        th->busy_slot = (th->busy != MMU_BUSY_NONE && th->busy_unit >= 0 && slot >= 0)
                       ? (int8_t)slot : -1;

        th->loaded = false;
        th->loaded_unit = -1;
        th->loaded_slot = -1;
        th->loaded_ext = -1;
        th->toolhead_sensor = false;
        th->current_group = -1;

        /* A lane calibration is not a load/unload in the model's terms; the
         * bay's own cal code is what the UI shows. */
        if(op && strcmp(op, "CALIBRATING") == 0 && unit >= 0 && slot >= 0 &&
           slot < (int)m->units[unit].slot_count) {
            m->units[unit].slots[slot].cal_state = MMU_CAL_CALIBRATING;
        }
    }

    map_stage(th, field(lane, "stage"));
    map_runout(m, th, lane);
    return cJSON_IsBool(ms);
}

static void map_fault(mmu_model_t *m, mmu_toolhead_t *th, const cJSON *fault, int nf)
{
    if(cJSON_IsObject(fault)) {
        const char *code = jstring(fault, "code");
        const char *text = jstring(fault, "text");
        const cJSON *act = field(fault, "act");
        const cJSON *tok;
        int bay = -1;
        /* A fault whose code differs from the one already held is new to us
         * and joins the alert history; the same fault repeated in a later
         * status does not. */
        bool fresh = !(th->has_error && code && strcmp(th->error_code, code) == 0);

        th->has_error = true;
        th->error_severity = map_severity(jstring(fault, "sev"));
        if(code) mmu_copy_text(th->error_code, sizeof(th->error_code), code);
        if(text) mmu_copy_text(th->error_text, sizeof(th->error_text), text);
        th->error_unit = (int8_t)unit_index(m, jstring(fault, "unit"));
        th->error_slot = jint(fault, "bay", &bay) ? (int8_t)bay : -1;

        /* The fault's first two support-action tokens, in the host's order
         * (C2 5.7): the UI turns them into buttons. */
        th->error_act_count = 0;
        th->error_act[0][0] = '\0';
        th->error_act[1][0] = '\0';
        if(cJSON_IsArray(act)) {
            cJSON_ArrayForEach(tok, act) {
                if(th->error_act_count >= 2) break;
                if(!cJSON_IsString(tok)) continue;
                mmu_copy_text(th->error_act[th->error_act_count],
                              sizeof(th->error_act[0]), tok->valuestring);
                th->error_act_count++;
            }
        }
        th->error_count = (nf > 0) ? (uint8_t)(nf > 255 ? 255 : nf) : 0;

        if(fresh) {
            /* Append to the pending queue; the caller's merge step (or the
             * simulator's self-merge) drains it into the authoritative UI
             * history under the right lock, avoiding a cross-task write. The
             * entry records the lane's toolhead and that it is a lane fault
             * (the live fault stays on the toolhead; history only remembers). */
            mmu_model_push_alert_pending_at(m, text ? text : code ? code : "", th->error_severity,
                                            th->error_unit, (int)(th - m->toolheads), true);
        }
    }
    else if(fault) {   /* explicit null: no active fault on this lane */
        th->has_error = false;
        th->error_severity = MMU_SEV_INFO;
        th->error_code[0] = '\0';
        th->error_text[0] = '\0';
        th->error_unit = -1;
        th->error_slot = -1;
        th->error_act_count = 0;
        th->error_act[0][0] = '\0';
        th->error_act[1][0] = '\0';
        th->error_count = 0;
    }
}

/* -------------------------------------------------------------- appliers */

bool status_map_apply_openams_ui(mmu_model_t *m, const cJSON *obj)
{
    const cJSON *bays;
    const char *unit_name;
    mmu_unit_t *u;
    mmu_toolhead_t *th;
    int nbay;

    if(!m || !cJSON_IsObject(obj)) return false;

    unit_name = jstring(obj, "unit");
    bays = field(obj, "bays");
    if(!unit_name || !unit_name[0] || !cJSON_IsArray(bays)) return false;

    nbay = cJSON_GetArraySize(bays);
    u = unit_get_or_create(m, unit_name, nbay);
    if(!u) return false;

    /* `bays` is authoritative for the bay count (C2 6.5). */
    if(nbay > MMU_MAX_SLOTS_PER_UNIT) nbay = MMU_MAX_SLOTS_PER_UNIT;
    if(nbay > 0) u->slot_count = (uint8_t)nbay;

    map_family(u, jstring(obj, "family"));

    /* The host identifiers the action mapper needs (C2 6.2): the unit's own
     * `idx` (OAMS=) and its FPS lane id (FPS=). Absent means unknown, so both
     * are overwritten rather than left from an earlier projection. */
    {
        int idx;
        const char *lane;

        u->host_idx = jint(obj, "idx", &idx) ? (int16_t)idx : -1;
        lane = jstring(field(obj, "lane"), "id");
        mmu_copy_text(u->lane, sizeof(u->lane), lane);
    }

    /* The toolhead this unit's lane belongs to (C2 6.5): looked up or created
     * by lane.id, so several units on the same FPS lane share one toolhead
     * and a different lane never overwrites it. */
    th = toolhead_get_or_create(m, u->lane);
    u->toolhead = th ? (int8_t)(th - m->toolheads) : -1;

    {
        const cJSON *online = field(obj, "online");
        if(cJSON_IsBool(online)) u->connected = cJSON_IsTrue(online);
    }

    map_env(u, field(obj, "env"));
    map_dryer(u, field(obj, "dryer"));
    map_unit_caps(u, field(obj, "ready"), field(obj, "act"));
    u->rfid = array_has(field(obj, "act"), "rfid_scan");
    map_bays(u, bays);

    /* Groups before the lane: a LOADED lane resolves its current_group by
     * looking the bay up in the groups that were just built. */
    map_groups(m, (int)(u - m->units), field(obj, "groups"));
    if(th) {
        bool has_ms = map_lane(m, th, field(obj, "lane"));
        int nf = 0;

        jint(obj, "nf", &nf);   /* the lane's fault count, a sibling of `fault` */
        map_fault(m, th, field(obj, "fault"), nf);

        /* Only an older host that does not publish `lane.ms` needs the rule
         * the model's own scenarios use (C2 4.2/5.6). */
        if(!has_ms) th->motion_settled = (th->busy == MMU_BUSY_NONE && !th->has_error);
    }

    mmu_model_refresh_this(m);

    return true;
}

bool status_map_apply_component_status(mmu_model_t *m, const cJSON *obj)
{
    const cJSON *bays, *bay;

    if(!m || !cJSON_IsObject(obj)) return false;

    bays = field(obj, "bays");
    if(!cJSON_IsObject(bays)) return false;

    {
        const cJSON *online = field(obj, "spoolman_online");
        const cJSON *edit = field(obj, "edit");

        if(cJSON_IsBool(online)) m->spoolman_online = cJSON_IsTrue(online);
        /* a component that predates the edit endpoint sends no `edit`: it can't */
        m->spoolman_edit = cJSON_IsBool(edit) && cJSON_IsTrue(edit);
        /* likewise a component with no sequence stop sends no `stop`: the
         * display keeps the cancel G-code */
        m->spoolman_stop = cJSON_IsTrue(field(obj, "stop"));
    }

    cJSON_ArrayForEach(bay, bays) {
        const cJSON *spool, *pc;
        mmu_slot_t *s;
        char name[MMU_UNIT_NAME_LEN];
        int slot;
        mmu_unit_t *u;

        if(!bay->string || !cJSON_IsObject(bay)) continue;
        if(!ref_split(bay->string, name, sizeof(name), &slot)) continue;
        if(slot < 0 || slot >= MMU_MAX_SLOTS_PER_UNIT) continue;

        /* A unit named only by its Spoolman links still shows up, sized to the
         * highest bay the status mentions. */
        u = unit_get_or_create(m, name, slot + 1);
        if(!u || slot >= (int)u->slot_count) continue;
        s = &u->slots[slot];

        pc = field(bay, "pending_confirmation");
        if(cJSON_IsBool(pc)) s->pending_confirmation = cJSON_IsTrue(pc);

        spool = field(bay, "spool");
        if(cJSON_IsObject(spool)) {
            const char *material = jstring(spool, "material");
            const char *vendor = jstring(spool, "vendor");
            const char *hex = jstring(spool, "color_hex");
            int v;

            if(material) mmu_copy_text(s->material, sizeof(s->material), material);
            if(vendor) mmu_copy_text(s->brand, sizeof(s->brand), vendor);
            /* a summary with no (or an unreadable) color_hex has no color */
            s->color = 0;
            s->color_known = map_hex_color(hex, &s->color);
            if(jint(spool, "remaining_g", &v)) s->remaining_g = (int16_t)v;
            /* the spool's own size and pressure advance, when the host sends
             * them; a missing, null or non-number one is unknown, as it is
             * for every other field of the summary */
            /* only values the contract allows (a positive weight that fits the
             * model, a pressure advance of 0-2): anything else stays unknown
             * rather than wrapping into a wrong default */
            s->initial_g = -1;
            s->flow_k_x1000 = -1;
            if(jint(spool, "initial_g", &v) && v >= 1 && v <= INT16_MAX)
                s->initial_g = (int16_t)v;
            if(jnum_in(spool, "flow_k", 0.0, 2.0) && jmilli(spool, "flow_k", &v))
                s->flow_k_x1000 = (int16_t)v;
            if(jint(spool, "remaining_pct", &v)) {
                int16_t old_pct = s->remaining_pct;
                s->remaining_pct = (int16_t)v;
                /* Push a low-filament alert the first time a slot crosses
                 * below 15 %: from >= 15 or from unknown (-1). */
                if(s->remaining_pct >= 0 && s->remaining_pct < 15 &&
                   (old_pct < 0 || old_pct >= 15)) {
                    /* about one unit's bay: it lives on that unit's alert; the
                     * sentence is made when the view is built */
                    mmu_model_push_alert_pending_tpl(m, MMU_ALERT_LOW_FILAMENT, MMU_SEV_INFO,
                                                     (int)(u - m->units), -1, slot,
                                                     (int)s->remaining_g, 0);
                }
            }
        }
        else {
            /* No spool linked here any more (C2 6.2): the bay's own numbers go
             * with it, so the tile reads like one that never held a spool
             * instead of keeping the removed spool's color and percentage. */
            s->color = 0;
            s->color_known = false;
            s->material[0] = '\0';
            s->brand[0] = '\0';
            s->remaining_pct = -1;
            s->remaining_g = -1;
            s->initial_g = -1;
            s->flow_k_x1000 = -1;
        }
    }

    mmu_model_refresh_this(m);

    return true;
}

bool status_map_apply_settings(mmu_model_t *m, const cJSON *obj)
{
    if(!m || !cJSON_IsObject(obj)) return false;

    jbool(obj, "read_tag_on_insertion", &m->settings.read_tag_on_insertion);
    jbool(obj, "read_tags_at_startup", &m->settings.read_tags_at_startup);
    jbool(obj, "auto_reload_on_runout", &m->settings.auto_reload_on_runout);
    jbool(obj, "auto_create_spoolman_spools", &m->settings.auto_create_spoolman_spools);
    jbool(obj, "apply_pa_on_load", &m->settings.apply_pa_on_load);
    m->settings.known = true;

    mmu_model_refresh_this(m);

    return true;
}

bool status_map_apply_lanes(mmu_model_t *m, const cJSON *lanes_by_fps)
{
    const cJSON *lane;

    if(!m || !cJSON_IsObject(lanes_by_fps)) return false;

    cJSON_ArrayForEach(lane, lanes_by_fps) {
        mmu_toolhead_t *th;
        const cJSON *v;

        if(!lane->string || !cJSON_IsObject(lane)) continue;
        th = toolhead_get_or_create(m, lane->string);
        if(!th) continue;

        v = field(lane, "pressure");
        th->pressure = cJSON_IsNumber(v) ? (float)v->valuedouble : -1.0f;

        v = field(lane, "set_point");
        th->set_point = cJSON_IsNumber(v) ? (float)v->valuedouble : -1.0f;

        /* [low, high]: the band the unit's regulator keeps the pressure in.
         * Anything else (absent, null, not two numbers) is unknown. */
        v = field(lane, "pressure_band");
        th->band_low = th->band_high = -1.0f;
        if(cJSON_IsArray(v) && cJSON_GetArraySize(v) == 2 &&
           cJSON_IsNumber(cJSON_GetArrayItem(v, 0)) && cJSON_IsNumber(cJSON_GetArrayItem(v, 1))) {
            th->band_low = (float)cJSON_GetArrayItem(v, 0)->valuedouble;
            th->band_high = (float)cJSON_GetArrayItem(v, 1)->valuedouble;
        }
    }

    mmu_model_refresh_this(m);

    return true;
}

bool status_map_apply_fixture_event(mmu_model_t *m, const cJSON *event)
{
    const char *kind;

    if(!m || !cJSON_IsObject(event)) return false;

    kind = jstring(event, "kind");
    if(!kind) return false;

    if(strcmp(kind, "openams_ui") == 0) {
        return status_map_apply_openams_ui(m, field(event, "payload"));
    }
    if(strcmp(kind, "component_status") == 0) {
        return status_map_apply_component_status(m, field(event, "payload"));
    }
    return true;   /* a recorded kind the display does not read */
}

void status_map_reset(mmu_model_t *m)
{
    if(!m) return;

    memset(m, 0, sizeof(*m));

    m->motion_settled = true;
    m->spoolman_online = false;
    mmu_settings_defaults(&m->settings);

    m->this_unit = 0;
    m->loaded_unit = -1;
    m->loaded_slot = -1;
    m->loaded_ext = -1;
    m->busy_unit = -1;
    m->busy_slot = -1;
    m->step_current = -1;
    m->step_failed = -1;
    m->print_unit = -1;
    m->print_slot = -1;
    m->error_unit = -1;
    m->error_slot = -1;
    m->runout_from_unit = -1;
    m->runout_from_slot = -1;
    m->runout_to_unit = -1;
    m->runout_to_slot = -1;
    m->current_group = -1;
    m->selected_unit = -1;
    m->selected_slot = -1;
    m->selected_ext = -1;
    m->scenario_name = NULL;
}

bool status_map_apply_spool_list(mmu_model_t *m, const cJSON *arr)
{
    const cJSON *spool;
    int n = 0;

    if(!m || !cJSON_IsArray(arr)) return false;

    m->spool_count = 0;
    cJSON_ArrayForEach(spool, arr) {
        const cJSON *archived_j, *filament, *vendor_obj;
        int id;
        mmu_spool_t *sp;

        if(!cJSON_IsObject(spool)) continue;
        archived_j = field(spool, "archived");
        if(cJSON_IsBool(archived_j) && cJSON_IsTrue(archived_j)) continue;
        if(!jint(spool, "id", &id)) continue;
        if(n >= MMU_MAX_SPOOLS) break;

        sp = &m->spools[n++];
        memset(sp, 0, sizeof(*sp));
        sp->id = (int32_t)id;
        sp->remaining_g = -1;

        {
            const cJSON *rw = field(spool, "remaining_weight");
            if(cJSON_IsNumber(rw)) sp->remaining_g = (int32_t)rw->valuedouble;
        }

        filament = field(spool, "filament");
        if(cJSON_IsObject(filament)) {
            const char *mat = jstring(filament, "material");
            const char *hex = jstring(filament, "color_hex");

            if(mat) mmu_copy_text(sp->material, sizeof(sp->material), mat);
            sp->color_known = map_hex_color(hex, &sp->color);
            vendor_obj = field(filament, "vendor");
            if(cJSON_IsObject(vendor_obj)) {
                const char *vname = jstring(vendor_obj, "name");
                if(vname) mmu_copy_text(sp->vendor, sizeof(sp->vendor), vname);
            }
        }
    }
    m->spool_count = (uint8_t)n;
    mmu_model_refresh_this(m);
    return true;
}

/* A vendor name the edit form can carry whole: 1 to MMU_VENDOR_LEN - 1 bytes. */
static bool vendor_fits(const char *name)
{
    return name && name[0] && strlen(name) < MMU_VENDOR_LEN;
}

/* The brands a fresh Spoolman has none of, so the editor would otherwise
 * offer "Generic" and nothing else. Merged with Spoolman's own list by
 * status_map_apply_vendor_list(); the same list, in the same order, is in
 * web/logic/model.ts, and the differential tests are what keeps the two
 * honest. Brand names are not translated: they are the same in every
 * language, and a host that has one keeps its own spelling. */
static const char *const curated_vendors[] = {
    MMU_VENDOR_GENERIC, "Bambu Lab", "Polymaker", "Prusament", "eSUN", "Sunlu", "Elegoo",
    "Overture", "Hatchbox", "Creality", "Anycubic", "Jayo", "Eryone", "Inland", "Fillamentum",
    "Fiberlogy", "ColorFabb", "Extrudr", "Protopasta", "Siraya Tech", "Atomic Filament",
    "MatterHackers", "Spectrum", "Azurefilm", "Kingroon", "Geeetech", "Voxelab", "Amolen",
    "3DXTech", "Das Filament",
};
#define CURATED_VENDOR_COUNT ((int)(sizeof(curated_vendors) / sizeof(curated_vendors[0])))

/* ASCII case-insensitive order, then the first to arrive: the same in both
 * cores, which a locale-aware sort would not be. */
static int vendor_cmp(const char *a, const char *b)
{
    for(; *a && *b; a++, b++) {
        int x = (unsigned char)*a, y = (unsigned char)*b;

        if(x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if(y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if(x != y) return x - y;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

bool status_map_apply_vendor_list(mmu_model_t *m, const cJSON *arr)
{
    const cJSON *vendor;
    int n = 0, i, j;

    if(!m || !cJSON_IsArray(arr)) return false;

    memset(m->vendors, 0, sizeof(m->vendors));
    cJSON_ArrayForEach(vendor, arr) {
        const char *name = cJSON_IsObject(vendor) ? jstring(vendor, "name") : NULL;
        bool dup = false;

        if(!vendor_fits(name)) continue;
        for(i = 0; i < n; i++) {
            if(vendor_cmp(m->vendors[i], name) == 0) dup = true;
        }
        if(dup) continue;
        if(n >= MMU_MAX_VENDORS) break;
        mmu_copy_text(m->vendors[n++], MMU_VENDOR_LEN, name);
    }

    /* insertion sort: stable, and the list is a handful of names */
    for(i = 1; i < n; i++) {
        char key[MMU_VENDOR_LEN];

        memcpy(key, m->vendors[i], sizeof key);
        for(j = i - 1; j >= 0 && vendor_cmp(m->vendors[j], key) > 0; j--)
            memcpy(m->vendors[j + 1], m->vendors[j], sizeof key);
        memcpy(m->vendors[j + 1], key, sizeof key);
    }

    /* Then the curated brands, each only if the host's list does not already
     * name it - so Spoolman's own spelling of a brand wins, and a brand only
     * Spoolman knows stays. "Generic" leads either way: it is the answer for a
     * spool nobody has attributed yet, and a fresh host has none at all. */
    for(i = 0; i < CURATED_VENDOR_COUNT && n < MMU_MAX_VENDORS; i++) {
        const char *name = curated_vendors[i];
        bool dup = false;

        for(j = 0; j < n; j++) {
            if(vendor_cmp(m->vendors[j], name) == 0) dup = true;
        }
        if(dup) continue;
        mmu_copy_text(m->vendors[n++], MMU_VENDOR_LEN, name);
    }

    /* ... and "Generic" first, which is the one place this list is not in
     * name order: everything above it is sorted or curated, and the editor
     * steps through it, so the commonest answer is the first tap. */
    for(i = 0; i < n; i++) {
        if(vendor_cmp(m->vendors[i], MMU_VENDOR_GENERIC) == 0) break;
    }
    if(i < n && i > 0) {
        char generic[MMU_VENDOR_LEN];

        memcpy(generic, m->vendors[i], sizeof generic);
        for(j = i; j > 0; j--)
            memcpy(m->vendors[j], m->vendors[j - 1], sizeof generic);
        memcpy(m->vendors[0], generic, sizeof generic);
    }

    m->vendor_count = (uint8_t)n;
    mmu_model_refresh_this(m);
    return true;
}

bool status_map_apply_print_stats(mmu_model_t *m, const cJSON *obj)
{
    const char *fn;

    if(!m || !cJSON_IsObject(obj)) return false;
    fn = jstring(obj, "filename");
    if(fn) mmu_copy_text(m->print_filename, sizeof(m->print_filename), fn);
    mmu_model_refresh_this(m);
    return true;
}

bool status_map_apply_metadata(mmu_model_t *m, const cJSON *obj)
{
    const cJSON *weights, *w;
    int i, n;

    if(!m || !cJSON_IsObject(obj)) return false;
    weights = field(obj, "filament_weights");
    if(!cJSON_IsArray(weights)) return false;

    /* Clear previous job needs. */
    for(i = 0; i < MMU_MAX_TOOLS; i++) m->job_need_g[i] = 0;
    m->job_short = 0;

    n = 0;
    cJSON_ArrayForEach(w, weights) {
        double val;
        int need;

        if(n >= MMU_MAX_TOOLS) break;
        val  = cJSON_IsNumber(w) ? w->valuedouble : 0.0;
        /* Round up to whole grams. */
        need = (val > 0.0) ? (int)(val + 0.9999999) : 0;
        m->job_need_g[n] = (int16_t)need;
        n++;
    }

    /* Compute shortfall for each tool with a non-zero need. */
    for(i = 0; i < n; i++) {
        char gname[MMU_GROUP_NAME_LEN];
        int gi, j;
        int32_t have = 0;
        bool all_unknown = true;
        int need = (int)m->job_need_g[i];

        if(need == 0) continue;

        snprintf(gname, sizeof gname, "T%d", i);
        gi = -1;
        for(j = 0; j < (int)m->group_count; j++) {
            if(strcmp(m->groups[j].name, gname) == 0) { gi = j; break; }
        }
        if(gi < 0) continue; /* no such group */

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

        if(all_unknown) continue; /* don't alert on fully-unknown groups */

        if(have < (int32_t)need) {
            m->job_short |= (uint8_t)(1u << i);
            /* about the job on the group's lane: the toolhead's alert */
            {
                int th_idx = -1;

                if(gi >= 0 && m->groups[gi].count > 0) {
                    int mu = m->groups[gi].members[0].unit;

                    if(mu >= 0 && mu < (int)m->unit_count) th_idx = m->units[mu].toolhead;
                }
                /* a caution, not a failure: the same tone as the message row */
                mmu_model_push_alert_pending_tpl(m, MMU_ALERT_SHORTFALL, MMU_SEV_INFO, -1, th_idx,
                                                 i, (int)need, (int)have);
            }
        }
    }

    mmu_model_refresh_this(m);

    return true;
}
