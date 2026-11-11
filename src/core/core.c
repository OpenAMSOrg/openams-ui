/**
 * @file core.c
 * See core.h.
 */

#include "core/core.h"

#include "backend/actions_map.h"
#include "backend/status_map.h"
#include "view/strings.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One openams_ui entry per unit, plus one singleton entry each for
 * oams_manager, component_status, print_stats, toolhead, spool_list,
 * vendor_list and metadata. */
#define CORE_MAX_ENTRIES (MMU_MAX_UNITS + 7)
/* Every kind the core applies must fit whole: a kind cut short is never found
 * again, so each apply would take a new entry until the table is full and
 * later updates are dropped ("component_status" is 16 characters and did
 * exactly that when this was 16). */
#define CORE_KIND_LEN 24
_Static_assert(sizeof("component_status") <= CORE_KIND_LEN, "a core kind must fit CORE_KIND_LEN");

#define CORE_NAME_LEN 48   /* the client's own MC_UNIT_NAME_MAX: a unit's object name, whole */
#define CORE_EXTRUDER_LEN 24

typedef struct {
    char kind[CORE_KIND_LEN];
    char name[CORE_NAME_LEN];
    /** A kind sent as diffs: the merged copy (heap). NULL for a kind sent
     *  whole, after a failed duplicate, and after core_drop_copies(). */
    cJSON *copy;
    /** A kind sent whole: what the last one hashed to, so an unchanged one
     *  reports no change without a copy to compare against. */
    uint32_t hash;
    bool hashed;
} core_entry_t;

struct core {
    mmu_model_t *model;         /**< not owned */

    core_entry_t entries[CORE_MAX_ENTRIES];
    int entry_count;

    /* The last `toolhead.extruder` seen (the active toolhead, C2 19); no
     * consumer yet, kept for the view builders (U1c) to read later. */
    char active_extruder[CORE_EXTRUDER_LEN];
};

/* ------------------------------------------------------------- the table */

static core_entry_t *entry_find(core_t *core, const char *kind, const char *name)
{
    char key[CORE_NAME_LEN];
    int i;

    /* the stored name is cut to CORE_NAME_LEN: cut the key the same way, so a
     * name too long to fit is still found again and never takes a new entry */
    snprintf(key, sizeof key, "%s", name);
    for(i = 0; i < core->entry_count; i++) {
        if(strcmp(core->entries[i].kind, kind) == 0 &&
           strcmp(core->entries[i].name, key) == 0)
            return &core->entries[i];
    }
    return NULL;
}

static core_entry_t *entry_get_or_create(core_t *core, const char *kind, const char *name)
{
    core_entry_t *e = entry_find(core, kind, name);

    if(e) return e;
    if(core->entry_count >= CORE_MAX_ENTRIES) return NULL;

    e = &core->entries[core->entry_count++];
    memset(e, 0, sizeof(*e));
    snprintf(e->kind, sizeof(e->kind), "%s", kind);
    snprintf(e->name, sizeof(e->name), "%s", name);
    return e;
}

static void entry_clear_all(core_t *core)
{
    int i;

    for(i = 0; i < core->entry_count; i++) cJSON_Delete(core->entries[i].copy);
    memset(core->entries, 0, sizeof(core->entries));
    core->entry_count = 0;
}

/* ------------------------------------------------------------- the hash */

/* FNV-1a over a cJSON tree: each item's type, key and value, and the end of
 * each object or array. It allocates nothing, which is the point: a kind
 * that arrives whole is mapped straight from the message, and this is all
 * that is kept of it to tell whether the next one differs. A collision only
 * costs one skipped redraw of a change the next update shows anyway. */
static uint32_t hash_bytes(uint32_t h, const void *data, size_t n)
{
    const unsigned char *p = data;

    while(n--) {
        h ^= *p++;
        h *= 16777619u;
    }
    return h;
}

static uint32_t hash_json(uint32_t h, const cJSON *item)
{
    const cJSON *child;
    unsigned char type = (unsigned char)(item->type & 0xff);

    h = hash_bytes(h, &type, 1);
    if(item->string) h = hash_bytes(h, item->string, strlen(item->string) + 1);
    if(cJSON_IsNumber(item)) h = hash_bytes(h, &item->valuedouble, sizeof item->valuedouble);
    else if(cJSON_IsString(item) && item->valuestring)
        h = hash_bytes(h, item->valuestring, strlen(item->valuestring) + 1);
    for(child = item->child; child; child = child->next) h = hash_json(h, child);
    return hash_bytes(h, "}", 1);
}

/* ------------------------------------------------------------- the merge */

/* Add or replace every key of @p update on @p dst (moonraker_client's own
 * mc_merge_into(), moved here: the client no longer merges anything itself,
 * so every consumer of a raw Moonraker diff - the ESP32 and, later, a web
 * host's own store - goes through the same rule). */
static bool merge_into(cJSON *dst, const cJSON *update)
{
    const cJSON *item;
    bool changed = false;

    if(!dst) return false;
    cJSON_ArrayForEach(item, update) {
        cJSON *copy;
        cJSON *old;

        if(!item->string) continue;
        /* Compare key by key instead of duplicating the whole object: a
         * status diff arrives several times a second, and on the ESP32 a
         * whole-object copy per update is a few KB of heap churn. */
        old = cJSON_GetObjectItemCaseSensitive(dst, item->string);
        if(old && cJSON_Compare(old, item, true)) continue;
        copy = cJSON_Duplicate(item, true);
        if(!copy) continue;
        if(old)
            cJSON_ReplaceItemInObjectCaseSensitive(dst, item->string, copy);
        else
            cJSON_AddItemToObject(dst, item->string, copy);
        changed = true;
    }
    return changed;
}

/* ---------------------------------------------------------------- lifecycle */

core_t *core_create(mmu_model_t *model)
{
    core_t *core;

    if(!model) return NULL;
    core = calloc(1, sizeof(*core));
    if(!core) return NULL;
    core->model = model;
    return core;
}

void core_destroy(core_t *core)
{
    if(!core) return;
    entry_clear_all(core);
    free(core);
}

void core_drop_copies(core_t *core)
{
    int i;

    if(!core) return;
    for(i = 0; i < core->entry_count; i++) {
        cJSON_Delete(core->entries[i].copy);
        core->entries[i].copy = NULL;
    }
}

void core_reset(core_t *core, bool keep_alerts)
{
    mmu_alert_t saved[MMU_MAX_ALERTS];
    uint8_t saved_count = 0, saved_unread = 0;
    uint32_t saved_now;

    if(!core) return;

    entry_clear_all(core);
    core->active_extruder[0] = '\0';

    if(!core->model) return;

    if(keep_alerts) {
        saved_count = core->model->alert_count;
        saved_unread = core->model->alert_unread;
        memcpy(saved, core->model->alerts, sizeof(saved));
    }

    saved_now = core->model->now_ms;           /* the clock is the platform's, not printer state */
    status_map_reset(core->model);
    core->model->now_ms = saved_now;

    if(keep_alerts) {
        core->model->alert_count = saved_count;
        core->model->alert_unread = saved_unread;
        memcpy(core->model->alerts, saved, sizeof(saved));
    }
}

/* ------------------------------------------------------------------ apply */

/* "component_status", "spool_list", "vendor_list" and "metadata" arrive
 * whole, never as diffs, so nothing has to be merged and no copy is kept:
 * each is mapped straight from @p obj, and only its hash is remembered. On
 * the 4 inch board (no PSRAM) a kept copy of the Spoolman status alone was
 * about 7 KB of heap with eight bays, a spool list far more, and the board
 * has about 10 KB to spare. @p want_array picks the list kinds' shape (an
 * array) over the others' (an object). Returns whether it differs from the
 * last one. */
static bool apply_whole(core_t *core, const char *kind, const char *name,
                        const cJSON *obj, bool want_array)
{
    core_entry_t *e;
    uint32_t hash;
    bool changed;

    if(want_array ? !cJSON_IsArray(obj) : !cJSON_IsObject(obj)) return false;

    e = entry_get_or_create(core, kind, name);
    if(!e) return false;

    hash = hash_json(2166136261u, obj);
    changed = !e->hashed || e->hash != hash;
    e->hash = hash;
    e->hashed = true;

    if(strcmp(kind, "spool_list") == 0) status_map_apply_spool_list(core->model, obj);
    else if(strcmp(kind, "vendor_list") == 0) status_map_apply_vendor_list(core->model, obj);
    else if(strcmp(kind, "metadata") == 0) status_map_apply_metadata(core->model, obj);
    else if(strcmp(kind, "component_status") == 0) status_map_apply_component_status(core->model, obj);

    mmu_model_refresh_this(core->model);
    return changed;
}

/* Every other kind: merge @p obj's top-level keys into the owned copy, then
 * map the merged copy. Returns whether the copy's content changed. */
static bool apply_merge(core_t *core, const char *kind, const char *name, const cJSON *obj)
{
    core_entry_t *e;
    bool changed;

    if(!cJSON_IsObject(obj)) return false;

    e = entry_get_or_create(core, kind, name);
    if(!e) return false;

    if(!e->copy) e->copy = cJSON_CreateObject();
    if(!e->copy) return false;
    changed = merge_into(e->copy, obj);

    if(strcmp(kind, "openams_ui") == 0) {
        status_map_apply_openams_ui(core->model, e->copy);
    }
    else if(strcmp(kind, "oams_manager") == 0) {
        const cJSON *settings = cJSON_GetObjectItemCaseSensitive(e->copy, "settings");
        const cJSON *lanes = cJSON_GetObjectItemCaseSensitive(e->copy, "lanes_by_fps");

        if(cJSON_IsObject(settings)) status_map_apply_settings(core->model, settings);
        if(cJSON_IsObject(lanes)) status_map_apply_lanes(core->model, lanes);
        /* absent on an older host: then no retry word, and no Retry */
        status_map_apply_commands(core->model, cJSON_GetObjectItemCaseSensitive(e->copy, "commands"));
    }
    else if(strcmp(kind, "print_stats") == 0) {
        status_map_apply_print_stats(core->model, e->copy);
    }
    else if(strcmp(kind, "toolhead") == 0) {
        /* No applier: the active toolhead has no consumer yet (U1c). */
        const cJSON *ext = cJSON_GetObjectItemCaseSensitive(e->copy, "extruder");

        if(cJSON_IsString(ext))
            snprintf(core->active_extruder, sizeof(core->active_extruder),
                     "%s", ext->valuestring);
    }
    else {
        /* An unrecognized kind still gets its copy kept (harmless), but
         * nothing here knows how to map it into the model. */
        return changed;
    }

    mmu_model_refresh_this(core->model);
    return changed;
}

/* The host's answer to an action the display sent: {kind, ok, message}. It is
 * not a status kind, so it owns no copy - it goes straight to the model. */
static bool apply_action_result(core_t *core, const cJSON *obj)
{
    const cJSON *k = cJSON_GetObjectItemCaseSensitive(obj, "kind");
    const cJSON *ok = cJSON_GetObjectItemCaseSensitive(obj, "ok");
    const cJSON *msg = cJSON_GetObjectItemCaseSensitive(obj, "message");
    int kind = cJSON_IsNumber(k) ? k->valueint : MMU_ACTION_OTHER;

    if(kind < MMU_ACTION_OTHER || kind > MMU_ACTION_RETRY) return false;

    mmu_model_action_result(core->model, (mmu_action_kind_t)kind, cJSON_IsTrue(ok),
                            cJSON_IsString(msg) ? msg->valuestring : "");
    return true;
}

bool core_apply(core_t *core, const char *kind, const char *name, const cJSON *obj)
{
    const char *ename = name ? name : "";

    if(!core || !core->model || !kind || !obj) return false;

    if(strcmp(kind, "action_result") == 0) return apply_action_result(core, obj);
    if(strcmp(kind, "spool_list") == 0) return apply_whole(core, kind, ename, obj, true);
    if(strcmp(kind, "vendor_list") == 0) return apply_whole(core, kind, ename, obj, true);
    if(strcmp(kind, "metadata") == 0) return apply_whole(core, kind, ename, obj, false);
    if(strcmp(kind, "component_status") == 0) return apply_whole(core, kind, ename, obj, false);

    if(strcmp(kind, "openams_ui") != 0 && strcmp(kind, "oams_manager") != 0 &&
       strcmp(kind, "print_stats") != 0 && strcmp(kind, "toolhead") != 0)
        return false;

    return apply_merge(core, kind, ename, obj);
}

bool core_action_sent(core_t *core, mmu_action_kind_t kind)
{
    if(!core || !core->model) return false;
    if(kind == MMU_ACTION_OTHER) return false;
    mmu_model_action_sent(core->model, kind);
    return true;
}

bool core_retry_sent(core_t *core, const char *lane_id)
{
    if(!core || !core->model || !lane_id) return false;
    mmu_model_retry_sent(core->model, lane_id);
    return true;
}

bool core_rpc_result(core_t *core, int tag, const cJSON *result, const char *error)
{
    if(!core || !core->model) return false;

    if(tag == ACTIONS_RPC_EDIT) {
        mmu_model_edit_result(core->model, error ? (error[0] ? error : "error") : "");
        return true;
    }
    if(error || !result) return false;

    switch(tag) {
    case ACTIONS_RPC_SPOOL_LIST:
    case ACTIONS_RPC_VENDOR_LIST:
        if(!cJSON_IsArray(result)) return false;
        core_apply(core, tag == ACTIONS_RPC_SPOOL_LIST ? "spool_list" : "vendor_list", NULL, result);
        return true;
    case ACTIONS_RPC_METADATA:
        if(!cJSON_IsObject(result)) return false;
        core_apply(core, "metadata", NULL, result);
        return true;
    default:
        return false;
    }
}

bool core_set_language(core_t *core, const char *code)
{
    if(!core || !code) return false;
    return view_set_language(code);
}
