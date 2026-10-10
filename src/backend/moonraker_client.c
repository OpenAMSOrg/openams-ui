/**
 * @file moonraker_client.c
 * The Moonraker JSON-RPC state machine (see moonraker_client.h).
 *
 * The shapes here are the ones `openams/tools/mock_moonraker/server.py`
 * speaks: JSON-RPC 2.0 with integer ids, `server.info` before
 * `printer.objects.list` before `printer.objects.subscribe`, the
 * `notify_status_update` diff as `[diff, eventtime]`, and
 * `notify_openams_spoolman_status` as `[payload]`.
 */

#include "backend/moonraker_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "model/mmu_model.h"

/* 16 KB: the largest frame the client will look at. */
#define MC_MAX_TEXT 16384

/* An unanswered request fails after 10 s. */
#define MC_REQUEST_TIMEOUT_MS 10000

/* While Klippy is not ready, `server.info` is retried on this cadence. */
#define MC_INFO_RETRY_MS 2000

#define MC_BACKOFF_MIN_MS 1000
#define MC_BACKOFF_MAX_MS 30000

#define MC_UI_PREFIX "openams_ui "
#define MC_OBJECT_NAME_MAX 64
#define MC_UNIT_NAME_MAX 48
#define MC_MAX_PENDING 8

/** Which request an id belongs to. */
typedef enum {
    MC_REQ_SERVER_INFO,
    MC_REQ_OBJECTS_LIST,
    MC_REQ_SUBSCRIBE,
    MC_REQ_COMPONENT_STATUS,
    MC_REQ_GCODE,
    MC_REQ_RPC  /**< generic RPC via mc_send_rpc(); result goes to on_rpc_result */
} mc_req_kind_t;

/** One in-flight request. `id == 0` is an empty slot. */
typedef struct {
    int id;
    mc_req_kind_t kind;
    int tag;              /**< caller tag for MC_REQ_RPC; 0 for all others */
    uint32_t deadline_ms;
} mc_pending_t;

typedef struct {
    mc_callbacks_t cb;
    mc_link_t link;

    /* Configured by mc_init. */
    bool bound;
    char bound_unit[MC_UNIT_NAME_MAX];

    /* Chosen from printer.objects.list. */
    char object_name[MC_OBJECT_NAME_MAX];
    char unit[MC_UNIT_NAME_MAX];
    bool have_object;

    /* oams_manager (C2 4.9/6.5, settings and lanes_by_fps) is only fetched
     * when the host's object list carries it. */
    bool have_oams_manager;

    /* toolhead is a base Klipper object and every real host has one, but the
     * same defensive gate as oams_manager costs nothing and keeps a host
     * that does not publish it (a display-only mock, a bench rig) working. */
    bool have_toolhead;

    mc_pending_t pending[MC_MAX_PENDING];
    int next_id;

    /* Klippy-wait retry and the reconnect clock. */
    mc_wait_t wait_why;
    char wait_detail[MC_UNIT_NAME_MAX];
    uint32_t next_info_retry_ms;
    bool reconnect_pending;
    uint32_t reconnect_at_ms;
    uint32_t backoff_ms;

    /* The last timestamp any entry point was called with; the only clock
     * mc_send_gcode() can use, since its signature carries none. */
    uint32_t now_ms;
} mc_t;

static mc_t g_mc;

/* ------------------------------------------------------------ link state */

static void mc_set_link(mc_link_t state)
{
    if(g_mc.link == state) return;
    g_mc.link = state;
    if(g_mc.cb.on_link) g_mc.cb.on_link(g_mc.cb.ctx, state);
}

mc_link_t mc_link(void)
{
    return g_mc.link;
}

mc_wait_t mc_wait_reason(char *detail, size_t n)
{
    if(detail && n) snprintf(detail, n, "%s", g_mc.wait_detail);
    return g_mc.wait_why;
}

bool mc_reconnect_due(uint32_t now_ms)
{
    return g_mc.reconnect_pending && (int32_t)(now_ms - g_mc.reconnect_at_ms) >= 0;
}

/* -------------------------------------------------------------- lifecycle */

void mc_init(const mc_callbacks_t *cb, const char *bound_unit)
{
    mc_deinit();

    if(cb) g_mc.cb = *cb;
    g_mc.link = MC_LINK_DOWN;
    g_mc.bound = bound_unit != NULL && bound_unit[0] != '\0';
    if(g_mc.bound) {
        mmu_copy_text(g_mc.bound_unit, sizeof(g_mc.bound_unit), bound_unit);
    }
    g_mc.backoff_ms = MC_BACKOFF_MIN_MS;
}

void mc_deinit(void)
{
    memset(&g_mc, 0, sizeof g_mc);
}

/* ------------------------------------------------------------- requests */

static void mc_pending_clear_kind(mc_req_kind_t kind)
{
    int i;

    for(i = 0; i < MC_MAX_PENDING; i++) {
        if(g_mc.pending[i].id != 0 && g_mc.pending[i].kind == kind)
            g_mc.pending[i].id = 0;
    }
}

static void mc_pending_clear_all(void)
{
    memset(g_mc.pending, 0, sizeof g_mc.pending);
}

static int mc_pending_find(int id)
{
    int i;

    for(i = 0; i < MC_MAX_PENDING; i++) {
        if(g_mc.pending[i].id == id) return i;
    }
    return -1;
}

static void mc_pending_add(int id, mc_req_kind_t kind, int tag, uint32_t deadline_ms)
{
    int i;

    if(kind != MC_REQ_GCODE && kind != MC_REQ_RPC) mc_pending_clear_kind(kind);
    for(i = 0; i < MC_MAX_PENDING; i++) {
        if(g_mc.pending[i].id == 0) {
            g_mc.pending[i].id = id;
            g_mc.pending[i].kind = kind;
            g_mc.pending[i].tag = tag;
            g_mc.pending[i].deadline_ms = deadline_ms;
            return;
        }
    }
    /* The table is full only if the peer stopped answering every request and
     * eight gcodes are stacked; drop the oldest answer rather than grow. */
    g_mc.pending[0].id = id;
    g_mc.pending[0].kind = kind;
    g_mc.pending[0].tag = tag;
    g_mc.pending[0].deadline_ms = deadline_ms;
}

/**
 * Build and send one JSON-RPC request, taking ownership of @p params, and wait
 * @p timeout_ms for its answer. Returns the id sent, or -1 when the frame could
 * not be built or queued.
 */
static int mc_send_request_within(const char *method, cJSON *params, mc_req_kind_t kind,
                                  uint32_t timeout_ms)
{
    cJSON *root;
    char *text;
    int id;
    size_t len;

    id = ++g_mc.next_id;
    root = cJSON_CreateObject();
    if(!root) {
        cJSON_Delete(params);
        return -1;
    }
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddStringToObject(root, "method", method);
    cJSON_AddNumberToObject(root, "id", (double)id);
    if(params) cJSON_AddItemToObject(root, "params", params);

    text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if(!text) return -1;

    len = strlen(text);
    if(!g_mc.cb.send_text || !g_mc.cb.send_text(g_mc.cb.ctx, text, len)) {
        free(text);
        return -1;
    }
    free(text);

    mc_pending_add(id, kind, 0, g_mc.now_ms + timeout_ms);
    return id;
}

static int mc_send_request(const char *method, cJSON *params, mc_req_kind_t kind)
{
    return mc_send_request_within(method, params, kind, MC_REQUEST_TIMEOUT_MS);
}

static void mc_start_handshake(uint32_t now_ms)
{
    g_mc.now_ms = now_ms;
    mc_set_link(MC_LINK_CONNECTING);
    mc_send_request("server.info", NULL, MC_REQ_SERVER_INFO);
}

static void mc_enter_klippy_wait(uint32_t now_ms, mc_wait_t why, const char *detail)
{
    /* Set before the link callback, so it can say why. */
    g_mc.wait_why = why;
    snprintf(g_mc.wait_detail, sizeof g_mc.wait_detail, "%s", detail ? detail : "");
    mc_set_link(MC_LINK_KLIPPY_WAIT);
    g_mc.next_info_retry_ms = now_ms + MC_INFO_RETRY_MS;
}

static void mc_send_objects_list(uint32_t now_ms)
{
    g_mc.now_ms = now_ms;
    mc_send_request("printer.objects.list", NULL, MC_REQ_OBJECTS_LIST);
}

static void mc_send_subscribe(uint32_t now_ms)
{
    cJSON *params = cJSON_CreateObject();
    cJSON *objects = cJSON_CreateObject();

    if(!params || !objects) {
        cJSON_Delete(params);
        cJSON_Delete(objects);
        return;
    }
    cJSON_AddItemToObject(params, "objects", objects);
    cJSON_AddNullToObject(objects, g_mc.object_name);
    cJSON_AddNullToObject(objects, "print_stats");

    /* toolhead.extruder names the currently active extruder (C2 19). Every
     * real Klipper host has a toolhead object, but subscribe to it only when
     * printer.objects.list actually named one, the same defensive gate
     * oams_manager gets below. */
    if(g_mc.have_toolhead) {
        cJSON *fields = cJSON_CreateArray();

        cJSON_AddItemToArray(fields, cJSON_CreateString("extruder"));
        cJSON_AddItemToObject(objects, "toolhead", fields);
    }

    /* settings and lanes_by_fps live on oams_manager, which a display-only
     * host may not publish at all (C2 6.1): subscribe to it only when it was
     * listed, and then to the two keys wanted, so the reply stays small. */
    if(g_mc.have_oams_manager) {
        cJSON *fields = cJSON_CreateArray();

        cJSON_AddItemToArray(fields, cJSON_CreateString("settings"));
        cJSON_AddItemToArray(fields, cJSON_CreateString("lanes_by_fps"));
        cJSON_AddItemToObject(objects, "oams_manager", fields);
    }

    g_mc.now_ms = now_ms;
    mc_send_request("printer.objects.subscribe", params, MC_REQ_SUBSCRIBE);
}

static void mc_send_component_status(uint32_t now_ms)
{
    g_mc.now_ms = now_ms;
    mc_send_request("server.openams_spoolman.status", NULL, MC_REQ_COMPONENT_STATUS);
}

/* ------------------------------------------------------ connect/disconnect */

void mc_on_connected(uint32_t now_ms)
{
    g_mc.now_ms = now_ms;
    g_mc.reconnect_pending = false;
    mc_pending_clear_all();
    mc_start_handshake(now_ms);
}

void mc_on_disconnected(uint32_t now_ms)
{
    int i;

    g_mc.now_ms = now_ms;
    for(i = 0; i < MC_MAX_PENDING; i++) {
        mc_pending_t *p = &g_mc.pending[i];
        int id;

        if(p->id == 0) continue;
        id = p->id;
        {
            int ptag = p->tag;
            mc_req_kind_t pkind = p->kind;
            p->id = 0;
            if(pkind == MC_REQ_GCODE && g_mc.cb.on_gcode_result)
                g_mc.cb.on_gcode_result(g_mc.cb.ctx, id, false, "disconnected");
            else if(pkind == MC_REQ_RPC && g_mc.cb.on_rpc_result)
                g_mc.cb.on_rpc_result(g_mc.cb.ctx, ptag, NULL, "disconnected");
            continue;
        }
    }

    mc_set_link(MC_LINK_DOWN);
    g_mc.reconnect_pending = true;
    g_mc.reconnect_at_ms = now_ms + g_mc.backoff_ms;
    if(g_mc.backoff_ms < MC_BACKOFF_MAX_MS) {
        g_mc.backoff_ms *= 2;
        if(g_mc.backoff_ms > MC_BACKOFF_MAX_MS) g_mc.backoff_ms = MC_BACKOFF_MAX_MS;
    }
}

/* ------------------------------------------------------------------ gcode */

int mc_send_gcode_within(const char *script, uint32_t timeout_ms)
{
    cJSON *params;

    if(g_mc.link != MC_LINK_READY || !script) return -1;
    params = cJSON_CreateObject();
    if(!params) return -1;
    cJSON_AddStringToObject(params, "script", script);
    return mc_send_request_within("printer.gcode.script", params, MC_REQ_GCODE, timeout_ms);
}

int mc_send_gcode(const char *script)
{
    return mc_send_gcode_within(script, MC_REQUEST_TIMEOUT_MS);
}

int mc_send_rpc(const char *method, cJSON *params, int tag)
{
    cJSON *root;
    char *text;
    int id;
    size_t len;

    if(g_mc.link != MC_LINK_READY || !method) {
        cJSON_Delete(params);
        return -1;
    }
    id = ++g_mc.next_id;
    root = cJSON_CreateObject();
    if(!root) {
        cJSON_Delete(params);
        return -1;
    }
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddStringToObject(root, "method", method);
    cJSON_AddNumberToObject(root, "id", (double)id);
    if(params) cJSON_AddItemToObject(root, "params", params);

    text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root); /* also frees params if it was added */
    if(!text) return -1;

    len = strlen(text);
    if(!g_mc.cb.send_text || !g_mc.cb.send_text(g_mc.cb.ctx, text, len)) {
        free(text);
        return -1;
    }
    free(text);
    mc_pending_add(id, MC_REQ_RPC, tag, g_mc.now_ms + MC_REQUEST_TIMEOUT_MS);
    return id;
}

/* --------------------------------------------------------------- receive */

/** Copy @p prefix + @p suffix into @p dst, or fail when they do not fit. */
static bool mc_join(char *dst, size_t dst_size, const char *prefix, const char *suffix)
{
    size_t plen = strlen(prefix);
    size_t slen = strlen(suffix);

    if(plen + slen + 1 > dst_size) return false;
    memcpy(dst, prefix, plen);
    memcpy(dst + plen, suffix, slen + 1);
    return true;
}

/**
 * What printer.objects.list's names say, folded as they go by so the list
 * itself is never held: the wanted `openams_ui <unit>` object (the bound
 * unit, or the first one when unbound), and whether `oams_manager` and
 * `toolhead` are present. Both the raw scan and the cJSON fallback feed it.
 */
typedef struct {
    char wanted[MC_OBJECT_NAME_MAX]; /**< bound: the exact name; else "" */
    bool wanted_ok;                  /**< false: the bound name cannot fit */
    bool found;
    char ui_name[MC_OBJECT_NAME_MAX];
    bool have_oams_manager;
    bool have_toolhead;
} mc_list_t;

static void mc_list_begin(mc_list_t *l)
{
    memset(l, 0, sizeof *l);
    l->wanted_ok = !g_mc.bound ||
                   mc_join(l->wanted, sizeof l->wanted, MC_UI_PREFIX, g_mc.bound_unit);
}

/** Fold one decoded object name (NUL-terminated, never truncated). */
static void mc_list_add(mc_list_t *l, const char *name)
{
    if(strcmp(name, "oams_manager") == 0) l->have_oams_manager = true;
    else if(strcmp(name, "toolhead") == 0) l->have_toolhead = true;

    if(l->found) return;
    if(g_mc.bound) {
        if(!l->wanted_ok || strcmp(name, l->wanted) != 0) return;
    } else if(strncmp(name, MC_UI_PREFIX, strlen(MC_UI_PREFIX)) != 0) {
        return;
    }
    l->found = true;
    memcpy(l->ui_name, name, strlen(name) + 1);
}

/** Adopt the list's outcome: the chosen object, or "no projection yet". */
static void mc_list_finish(const mc_list_t *l, uint32_t now_ms)
{
    const char *unit = l->ui_name + strlen(MC_UI_PREFIX);

    if(!l->found || unit[0] == '\0' || strlen(unit) >= sizeof g_mc.unit) {
        /* No projection yet; the retry loop will look again. */
        if(g_mc.bound) mc_enter_klippy_wait(now_ms, MC_WAIT_NO_UNIT, g_mc.bound_unit);
        else mc_enter_klippy_wait(now_ms, MC_WAIT_NO_OPENAMS, NULL);
        return;
    }
    memcpy(g_mc.object_name, l->ui_name, strlen(l->ui_name) + 1);
    memcpy(g_mc.unit, unit, strlen(unit) + 1);
    g_mc.have_oams_manager = l->have_oams_manager;
    g_mc.have_toolhead = l->have_toolhead;
    mc_send_subscribe(now_ms);
}

static void mc_handle_server_info(const cJSON *result, uint32_t now_ms)
{
    const cJSON *state = result ? cJSON_GetObjectItemCaseSensitive(result, "klippy_state") : NULL;

    if(cJSON_IsString(state) && strcmp(state->valuestring, "ready") == 0) {
        mc_set_link(MC_LINK_CONNECTING);
        mc_send_objects_list(now_ms);
        return;
    }
    /* Klippy is connected but not ready (or the reply was odd): wait it out. */
    mc_enter_klippy_wait(now_ms, MC_WAIT_KLIPPY,
                         cJSON_IsString(state) ? state->valuestring : "unknown");
}

/* The cJSON route: only reached when the raw scan declined the reply (an
 * odd shape); the normal route is mc_scan_objects_list(). */
static void mc_handle_objects_list(const cJSON *result, uint32_t now_ms)
{
    const cJSON *objects = result ? cJSON_GetObjectItemCaseSensitive(result, "objects") : NULL;
    const cJSON *item;
    mc_list_t l;

    if(!cJSON_IsArray(objects)) {
        mc_enter_klippy_wait(now_ms, MC_WAIT_NO_OPENAMS, NULL);
        return;
    }
    mc_list_begin(&l);
    cJSON_ArrayForEach(item, objects) {
        if(cJSON_IsString(item) && item->valuestring && strlen(item->valuestring) < MC_OBJECT_NAME_MAX)
            mc_list_add(&l, item->valuestring);
    }
    mc_list_finish(&l, now_ms);
}

static void mc_handle_subscribe(const cJSON *result, uint32_t now_ms)
{
    const cJSON *status = result ? cJSON_GetObjectItemCaseSensitive(result, "status") : NULL;
    const cJSON *obj = status ? cJSON_GetObjectItemCaseSensitive(status, g_mc.object_name) : NULL;
    const cJSON *print_stats = status ? cJSON_GetObjectItemCaseSensitive(status, "print_stats") : NULL;
    const cJSON *toolhead = status && g_mc.have_toolhead
                               ? cJSON_GetObjectItemCaseSensitive(status, "toolhead")
                               : NULL;
    const cJSON *manager = status && g_mc.have_oams_manager
                               ? cJSON_GetObjectItemCaseSensitive(status, "oams_manager")
                               : NULL;

    /* READY fires first (a no-op the very first time; on a reconnect it is
     * the consumer's cue - see on_link's contract - to drop whatever it owns
     * before this fresh result repopulates it), then the fresh objects. */
    g_mc.wait_why = MC_WAIT_NONE;
    g_mc.wait_detail[0] = '\0';
    mc_set_link(MC_LINK_READY);
    g_mc.backoff_ms = MC_BACKOFF_MIN_MS;

    if(cJSON_IsObject(obj)) {
        g_mc.have_object = true;
        if(g_mc.cb.on_object) g_mc.cb.on_object(g_mc.cb.ctx, "openams_ui", g_mc.unit, obj);
    }
    if(cJSON_IsObject(manager) && g_mc.cb.on_object)
        g_mc.cb.on_object(g_mc.cb.ctx, "oams_manager", "", manager);
    if(cJSON_IsObject(print_stats) && g_mc.cb.on_object)
        g_mc.cb.on_object(g_mc.cb.ctx, "print_stats", "", print_stats);
    if(cJSON_IsObject(toolhead) && g_mc.cb.on_object)
        g_mc.cb.on_object(g_mc.cb.ctx, "toolhead", "", toolhead);

    /* Only now ask the Spoolman component for its bays: Moonraker answers it
     * at once, while the subscribe needs a round trip to Klippy, so a request
     * sent beside the subscribe was answered before this unit existed in the
     * model, and its spools were dropped until the component next changed. */
    mc_send_component_status(now_ms);
}

static void mc_handle_result(mc_req_kind_t kind, int id, int tag, const cJSON *result,
                             uint32_t now_ms)
{
    switch(kind) {
    case MC_REQ_SERVER_INFO:
        mc_handle_server_info(result, now_ms);
        break;
    case MC_REQ_OBJECTS_LIST:
        mc_handle_objects_list(result, now_ms);
        break;
    case MC_REQ_SUBSCRIBE:
        mc_handle_subscribe(result, now_ms);
        break;
    case MC_REQ_COMPONENT_STATUS:
        if(result && g_mc.cb.on_component_status)
            g_mc.cb.on_component_status(g_mc.cb.ctx, result);
        break;
    case MC_REQ_GCODE:
        if(g_mc.cb.on_gcode_result) g_mc.cb.on_gcode_result(g_mc.cb.ctx, id, true, NULL);
        break;
    case MC_REQ_RPC:
        if(g_mc.cb.on_rpc_result) g_mc.cb.on_rpc_result(g_mc.cb.ctx, tag, result, NULL);
        break;
    }
}

static void mc_handle_error(mc_req_kind_t kind, int id, int tag, const char *message,
                            uint32_t now_ms)
{
    if(kind == MC_REQ_GCODE) {
        if(g_mc.cb.on_gcode_result) g_mc.cb.on_gcode_result(g_mc.cb.ctx, id, false, message);
        return;
    }
    if(kind == MC_REQ_RPC) {
        if(g_mc.cb.on_rpc_result) g_mc.cb.on_rpc_result(g_mc.cb.ctx, tag, NULL, message);
        return;
    }
    if(kind == MC_REQ_SERVER_INFO) {
        mc_enter_klippy_wait(now_ms, MC_WAIT_KLIPPY, "unknown");
        return;
    }
    if(kind == MC_REQ_COMPONENT_STATUS) {
        /* The openams_spoolman component is optional: a host without it
         * answers "Method not found", and the display still works (Spoolman
         * is simply offline). Restarting here would loop forever. */
        return;
    }
    /* The subscription could not be established; start over. */
    mc_start_handshake(now_ms);
}

static bool mc_handle_response(const cJSON *root, int id, uint32_t now_ms)
{
    const cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    mc_req_kind_t kind;
    int idx, tag;

    idx = mc_pending_find(id);
    if(idx < 0) return true; /* an id we never sent (or already answered) */
    kind = g_mc.pending[idx].kind;
    tag  = g_mc.pending[idx].tag;
    g_mc.pending[idx].id = 0;

    if(cJSON_IsObject(error)) {
        const cJSON *msg = cJSON_GetObjectItemCaseSensitive(error, "message");
        mc_handle_error(kind, id, tag, cJSON_IsString(msg) ? msg->valuestring : "error", now_ms);
    } else {
        mc_handle_result(kind, id, tag, result, now_ms);
    }
    return true;
}

static bool mc_handle_notification(const cJSON *root, const char *method, uint32_t now_ms)
{
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");

    /* Klipper restarting under a running Moonraker: Moonraker drops this
     * client's subscriptions, so the link waits for Klipper (and says why) and
     * subscribes again. The KLIPPY_WAIT retry finds Klipper ready by itself;
     * notify_klippy_ready only makes that sooner. */
    if(strcmp(method, "notify_klippy_disconnected") == 0 ||
       strcmp(method, "notify_klippy_shutdown") == 0) {
        if(g_mc.link != MC_LINK_DOWN)
            mc_enter_klippy_wait(now_ms, MC_WAIT_KLIPPY,
                                 strcmp(method, "notify_klippy_shutdown") == 0 ? "shutdown" : "disconnected");
        return true;
    }
    if(strcmp(method, "notify_klippy_ready") == 0) {
        if(g_mc.link == MC_LINK_KLIPPY_WAIT) mc_start_handshake(now_ms);
        return true;
    }

    if(strcmp(method, "notify_status_update") == 0) {
        const cJSON *diff = cJSON_IsArray(params) ? cJSON_GetArrayItem(params, 0) : NULL;
        const cJSON *entry;

        if(!cJSON_IsObject(diff)) return true;
        if(!g_mc.cb.on_object) return true;
        cJSON_ArrayForEach(entry, diff) {
            if(!entry->string) continue;
            if(g_mc.have_object && strcmp(entry->string, g_mc.object_name) == 0) {
                g_mc.cb.on_object(g_mc.cb.ctx, "openams_ui", g_mc.unit, entry);
            } else if(g_mc.have_oams_manager && strcmp(entry->string, "oams_manager") == 0) {
                g_mc.cb.on_object(g_mc.cb.ctx, "oams_manager", "", entry);
            } else if(strcmp(entry->string, "print_stats") == 0) {
                g_mc.cb.on_object(g_mc.cb.ctx, "print_stats", "", entry);
            } else if(g_mc.have_toolhead && strcmp(entry->string, "toolhead") == 0) {
                g_mc.cb.on_object(g_mc.cb.ctx, "toolhead", "", entry);
            }
        }
        return true;
    }

    if(strcmp(method, "notify_openams_spoolman_status") == 0) {
        const cJSON *payload = cJSON_IsArray(params) ? cJSON_GetArrayItem(params, 0) : NULL;
        if(payload && g_mc.cb.on_component_status)
            g_mc.cb.on_component_status(g_mc.cb.ctx, payload);
        return true;
    }

    return true; /* unknown method: ignored */
}

/* ------------------------------------------------------------ raw scan
 *
 * Two replies are too costly to build a cJSON tree for on a small heap: the
 * `printer.objects.list` answer (a string per Klipper object, 3 KB and 140
 * names on a real printer) and Moonraker's unrequested broadcasts. A tiny
 * tolerant scanner walks the text in place instead. It allocates nothing;
 * it only has to find a few top-level keys, skip the values it does not
 * want, and decode the list's strings one at a time. It is not a validator:
 * any shape it does not expect makes it decline (false/NULL) and the caller
 * takes the normal cJSON route, which then also does the validating. Keys
 * are compared raw, unescaped: the ones looked up contain no escapes.
 */

static const char *mc_skip_ws(const char *p, const char *end)
{
    while(p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

/** @p p is at an opening quote; return the closing quote, or NULL. */
static const char *mc_str_end(const char *p, const char *end)
{
    for(p++; p < end; p++) {
        if(*p == '\\') p++;
        else if(*p == '"') return p;
    }
    return NULL;
}

/** Return the pointer just past the JSON value at @p p, or NULL. */
static const char *mc_skip_value(const char *p, const char *end)
{
    int depth = 0;

    p = mc_skip_ws(p, end);
    if(p >= end) return NULL;
    if(*p == '"') {
        p = mc_str_end(p, end);
        return p ? p + 1 : NULL;
    }
    if(*p != '{' && *p != '[') {
        while(p < end && *p != ',' && *p != '}' && *p != ']' && *p != ' ' &&
              *p != '\t' && *p != '\n' && *p != '\r') p++;
        return p;
    }
    for(; p < end; p++) {
        if(*p == '"') {
            p = mc_str_end(p, end);
            if(!p) return NULL;
        } else if(*p == '{' || *p == '[') {
            depth++;
        } else if(*p == '}' || *p == ']') {
            if(--depth == 0) return p + 1;
        }
    }
    return NULL;
}

/** One `"key": value` member of an object. */
typedef struct {
    const char *key;   /**< first byte inside the quotes */
    size_t key_len;
    const char *val;   /**< first byte of the value, past whitespace */
    const char *val_end;
} mc_member_t;

/** Position @p *p (at a `{`) on the first member; see mc_next_member(). */
static bool mc_enter_object(const char **p, const char *end)
{
    *p = mc_skip_ws(*p, end);
    if(*p >= end || **p != '{') return false;
    (*p)++;
    return true;
}

/** Read the next member at @p *p. 1 = got one, 0 = end of object, -1 = odd. */
static int mc_next_member(const char **p, const char *end, mc_member_t *m)
{
    const char *q = mc_skip_ws(*p, end);
    const char *k_end;

    if(q < end && *q == ',') q = mc_skip_ws(q + 1, end);
    if(q >= end) return -1;
    if(*q == '}') {
        *p = q + 1;
        return 0;
    }
    if(*q != '"') return -1;
    k_end = mc_str_end(q, end);
    if(!k_end) return -1;
    m->key = q + 1;
    m->key_len = (size_t)(k_end - q - 1);
    q = mc_skip_ws(k_end + 1, end);
    if(q >= end || *q != ':') return -1;
    m->val = mc_skip_ws(q + 1, end);
    m->val_end = mc_skip_value(m->val, end);
    if(!m->val_end) return -1;
    *p = m->val_end;
    return 1;
}

static bool mc_key_is(const mc_member_t *m, const char *name)
{
    return m->key_len == strlen(name) && memcmp(m->key, name, m->key_len) == 0;
}

/** The value at @p m as a plain (escape-free) JSON integer. */
static bool mc_member_int(const mc_member_t *m, int *out)
{
    const char *p = m->val;
    long v = 0;
    bool neg = false;

    if(p < m->val_end && *p == '-') {
        neg = true;
        p++;
    }
    if(p >= m->val_end || m->val_end - p > 9) return false;
    for(; p < m->val_end; p++) {
        if(*p < '0' || *p > '9') return false;
        v = v * 10 + (*p - '0');
    }
    *out = (int)(neg ? -v : v);
    return true;
}

/** Append the UTF-8 of @p cp at @p *o; false when it would not fit. */
static bool mc_put_utf8(char *dst, size_t cap, size_t *o, uint32_t cp)
{
    int n = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;

    if(*o + (size_t)n >= cap) return false;
    if(n == 1) {
        dst[(*o)++] = (char)cp;
    } else if(n == 2) {
        dst[(*o)++] = (char)(0xC0 | (cp >> 6));
        dst[(*o)++] = (char)(0x80 | (cp & 0x3F));
    } else if(n == 3) {
        dst[(*o)++] = (char)(0xE0 | (cp >> 12));
        dst[(*o)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[(*o)++] = (char)(0x80 | (cp & 0x3F));
    } else {
        dst[(*o)++] = (char)(0xF0 | (cp >> 18));
        dst[(*o)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        dst[(*o)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[(*o)++] = (char)(0x80 | (cp & 0x3F));
    }
    return true;
}

static bool mc_hex4(const char *p, const char *end, uint32_t *out)
{
    uint32_t v = 0;
    int i;

    if(end - p < 4) return false;
    for(i = 0; i < 4; i++) {
        char c = p[i];

        v <<= 4;
        if(c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
        else if(c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
        else if(c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
        else return false;
    }
    *out = v;
    return true;
}

typedef enum { MC_STR_OK, MC_STR_SKIP, MC_STR_BAD } mc_str_t;

/**
 * Decode the string whose opening quote is at @p p into @p dst. SKIP: a
 * valid string that does not fit (or holds a NUL), which the caller must
 * ignore rather than truncate. BAD: malformed. @p *next is past the closing
 * quote in both OK and SKIP.
 */
static mc_str_t mc_decode_string(const char *p, const char *end, char *dst, size_t cap,
                                 const char **next)
{
    const char *close = mc_str_end(p, end);
    size_t o = 0;
    bool fits = true;

    if(!close) return MC_STR_BAD;
    *next = close + 1;
    for(p++; p < close;) {
        uint32_t cp;

        if(*p != '\\') {
            cp = (unsigned char)*p++;
            if(cp < 0x20) return MC_STR_BAD;
            if(fits && o + 1 >= cap) fits = false;
            if(fits) dst[o++] = (char)cp;
            continue;
        }
        p++; /* the escape letter; mc_str_end() guarantees one exists */
        switch(*p) {
        case '"': cp = '"'; break;
        case '\\': cp = '\\'; break;
        case '/': cp = '/'; break;
        case 'b': cp = '\b'; break;
        case 'f': cp = '\f'; break;
        case 'n': cp = '\n'; break;
        case 'r': cp = '\r'; break;
        case 't': cp = '\t'; break;
        case 'u':
            if(!mc_hex4(p + 1, close, &cp)) return MC_STR_BAD;
            p += 4;
            if(cp >= 0xD800 && cp < 0xDC00) { /* a surrogate pair */
                uint32_t lo;

                if(close - p < 7 || p[1] != '\\' || p[2] != 'u' ||
                   !mc_hex4(p + 3, close, &lo) || lo < 0xDC00 || lo > 0xDFFF)
                    return MC_STR_BAD;
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                p += 6;
            } else if(cp >= 0xDC00 && cp <= 0xDFFF) {
                return MC_STR_BAD;
            }
            break;
        default:
            return MC_STR_BAD;
        }
        p++;
        if(cp == 0) fits = false; /* a NUL would end the name early */
        else if(fits && !mc_put_utf8(dst, cap, &o, cp)) fits = false;
    }
    if(!fits) return MC_STR_SKIP;
    dst[o] = '\0';
    return MC_STR_OK;
}

/** Fold the names of the string array at @p arr (a `[`) into @p l. */
static bool mc_scan_names(const char *arr, const char *end, mc_list_t *l)
{
    const char *p = mc_skip_ws(arr, end);
    char name[MC_OBJECT_NAME_MAX];

    if(p >= end || *p != '[') return false;
    p++;
    for(;;) {
        p = mc_skip_ws(p, end);
        if(p >= end) return false;
        if(*p == ']') return true;
        if(*p == '"') {
            mc_str_t r = mc_decode_string(p, end, name, sizeof name, &p);

            if(r == MC_STR_BAD) return false;
            if(r == MC_STR_OK) mc_list_add(l, name);
        } else {
            p = mc_skip_value(p, end); /* not a name: nothing to fold */
            if(!p) return false;
        }
        p = mc_skip_ws(p, end);
        if(p < end && *p == ',') p++;
        else if(p >= end || *p != ']') return false;
    }
}

/** What the top level of one message says, found without a parse. */
typedef struct {
    bool ok;            /**< the top level walked cleanly */
    bool has_id;        /**< an integer `"id"` */
    int id;
    bool has_error;
    const char *method; /**< raw, inside the quotes; NULL when absent */
    size_t method_len;
    const char *result; /**< the `{...}` of an object `"result"`, else NULL */
    const char *result_end;
} mc_top_t;

static void mc_scan_top(const char *text, size_t len, mc_top_t *t)
{
    const char *p = text, *end = text + len;
    mc_member_t m;
    int r;

    memset(t, 0, sizeof *t);
    if(!mc_enter_object(&p, end)) return;
    while((r = mc_next_member(&p, end, &m)) > 0) {
        if(mc_key_is(&m, "id")) {
            t->has_id = mc_member_int(&m, &t->id);
        } else if(mc_key_is(&m, "error")) {
            t->has_error = true;
        } else if(mc_key_is(&m, "method") && *m.val == '"') {
            t->method = m.val + 1;
            t->method_len = (size_t)(m.val_end - m.val - 2);
        } else if(mc_key_is(&m, "result") && *m.val == '{') {
            t->result = m.val;
            t->result_end = m.val_end;
        }
    }
    t->ok = r == 0;
}

/** True when the message is a notification the display consumes. */
static bool mc_method_is_read(const mc_top_t *t)
{
    static const char *const read[] = { "notify_status_update",
                                        "notify_openams_spoolman_status",
                                        "notify_klippy_disconnected",
                                        "notify_klippy_shutdown",
                                        "notify_klippy_ready" };
    size_t i;

    for(i = 0; i < sizeof read / sizeof read[0]; i++) {
        if(t->method_len == strlen(read[i]) && memcmp(t->method, read[i], t->method_len) == 0)
            return true;
    }
    return false;
}

/**
 * Answer a pending printer.objects.list request straight from the text.
 * False (nothing consumed) when the reply is not the expected shape, so the
 * cJSON route still sees it.
 */
static bool mc_scan_objects_list(const mc_top_t *t, uint32_t now_ms)
{
    const char *p = t->result, *end = t->result_end;
    const char *arr = NULL;
    mc_member_t m;
    mc_list_t l;
    int r;

    if(!mc_enter_object(&p, end)) return false;
    while((r = mc_next_member(&p, end, &m)) > 0) {
        if(mc_key_is(&m, "objects")) arr = m.val;
    }
    if(r != 0 || !arr) return false;

    mc_list_begin(&l);
    if(!mc_scan_names(arr, end, &l)) return false;
    mc_pending_clear_kind(MC_REQ_OBJECTS_LIST);
    mc_list_finish(&l, now_ms);
    return true;
}

bool mc_on_text(const char *text, size_t len, uint32_t now_ms)
{
    cJSON *root;
    const cJSON *method;
    const cJSON *id;
    bool ok;
    mc_top_t top;

    g_mc.now_ms = now_ms;
    if(!text || len > MC_MAX_TEXT) return false;

    /* Cheap checks first: no heap, and they spare the parse for the traffic
     * the display never reads (Moonraker's once-a-second
     * notify_proc_stat_update and the like) and for the long object list. */
    mc_scan_top(text, len, &top);
    if(top.ok && !top.has_id && top.method && !mc_method_is_read(&top)) return true;
    if(top.ok && top.has_id && !top.has_error && top.result) {
        int idx = mc_pending_find(top.id);

        if(idx >= 0 && g_mc.pending[idx].kind == MC_REQ_OBJECTS_LIST &&
           mc_scan_objects_list(&top, now_ms))
            return true;
    }

    root = cJSON_ParseWithLength(text, len);
    if(!root) return false;
    if(!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    method = cJSON_GetObjectItemCaseSensitive(root, "method");
    id = cJSON_GetObjectItemCaseSensitive(root, "id");

    if(cJSON_IsNumber(id))
        ok = mc_handle_response(root, id->valueint, now_ms);
    else if(cJSON_IsString(method))
        ok = mc_handle_notification(root, method->valuestring, now_ms);
    else
        ok = true; /* valid JSON, but neither a response nor a notification */

    cJSON_Delete(root);
    return ok;
}

void mc_poll(uint32_t now_ms)
{
    int i;
    int restart = 0;

    g_mc.now_ms = now_ms;

    for(i = 0; i < MC_MAX_PENDING; i++) {
        mc_pending_t *p = &g_mc.pending[i];
        int id;

        if(p->id == 0) continue;
        if((int32_t)(now_ms - p->deadline_ms) < 0) continue;

        id = p->id;
        {
            int ptag = p->tag;
            mc_req_kind_t pkind = p->kind;
            p->id = 0;
            if(pkind == MC_REQ_GCODE) {
                if(g_mc.cb.on_gcode_result)
                    g_mc.cb.on_gcode_result(g_mc.cb.ctx, id, false, "timeout");
            } else if(pkind == MC_REQ_RPC) {
                if(g_mc.cb.on_rpc_result)
                    g_mc.cb.on_rpc_result(g_mc.cb.ctx, ptag, NULL, "timeout");
            } else {
                /* The handshake stalled; redo it from the top. */
                restart = 1;
            }
        }
    }

    if(restart) {
        mc_start_handshake(now_ms);
        return;
    }

    if(g_mc.link == MC_LINK_KLIPPY_WAIT &&
       (int32_t)(now_ms - g_mc.next_info_retry_ms) >= 0) {
        mc_send_request("server.info", NULL, MC_REQ_SERVER_INFO);
        g_mc.next_info_retry_ms = now_ms + MC_INFO_RETRY_MS;
    }
}
