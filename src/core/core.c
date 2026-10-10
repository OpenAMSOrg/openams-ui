/**
 * @file core.c
 * See core.h.
 */

#include "core/core.h"

#include "backend/actions_map.h"
#include "backend/status_map.h"
#include "view/strings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One openams_ui copy per unit, plus one singleton copy each for
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
    cJSON *copy;        /**< heap; NULL only right after a failed duplicate */
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

/* "spool_list"/"metadata": whole results, not diffs, so the owned copy is
 * replaced outright. @p want_array picks spool_list's shape (an array) over
 * every other kind's (an object). Returns whether the copy's content changed. */
static bool apply_replace(core_t *core, const char *kind, const char *name,
                          const cJSON *obj, bool want_array)
{
    core_entry_t *e;
    cJSON *before;
    bool changed;

    if(want_array ? !cJSON_IsArray(obj) : !cJSON_IsObject(obj)) return false;

    e = entry_get_or_create(core, kind, name);
    if(!e) return false;

    before = e->copy;
    e->copy = cJSON_Duplicate(obj, true);
    if(!e->copy) {
        e->copy = before;   /* keep the old copy rather than lose it */
        return false;
    }
    changed = !(before && cJSON_Compare(before, e->copy, true));
    cJSON_Delete(before);

    if(strcmp(kind, "spool_list") == 0) status_map_apply_spool_list(core->model, e->copy);
    else if(strcmp(kind, "vendor_list") == 0) status_map_apply_vendor_list(core->model, e->copy);
    else if(strcmp(kind, "metadata") == 0) status_map_apply_metadata(core->model, e->copy);

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
    }
    else if(strcmp(kind, "component_status") == 0) {
        status_map_apply_component_status(core->model, e->copy);
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

    if(kind < MMU_ACTION_OTHER || kind > MMU_ACTION_UNLOAD) return false;

    mmu_model_action_result(core->model, (mmu_action_kind_t)kind, cJSON_IsTrue(ok),
                            cJSON_IsString(msg) ? msg->valuestring : "");
    return true;
}

bool core_apply(core_t *core, const char *kind, const char *name, const cJSON *obj)
{
    const char *ename = name ? name : "";

    if(!core || !core->model || !kind || !obj) return false;

    if(strcmp(kind, "action_result") == 0) return apply_action_result(core, obj);
    if(strcmp(kind, "spool_list") == 0) return apply_replace(core, kind, ename, obj, true);
    if(strcmp(kind, "vendor_list") == 0) return apply_replace(core, kind, ename, obj, true);
    if(strcmp(kind, "metadata") == 0) return apply_replace(core, kind, ename, obj, false);

    if(strcmp(kind, "openams_ui") != 0 && strcmp(kind, "oams_manager") != 0 &&
       strcmp(kind, "component_status") != 0 && strcmp(kind, "print_stats") != 0 &&
       strcmp(kind, "toolhead") != 0)
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
