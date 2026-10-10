/**
 * @file test_moonraker_client.c
 * The Moonraker client driven by scripted frames shaped exactly like
 * `openams/tools/mock_moonraker/server.py`'s: the handshake order, the
 * subscribe result, status-update diffs, component status, gcode results,
 * timeouts, the reconnect backoff, and the malformed/oversized rejections.
 *
 * Plain C, plain assert, no framework, no socket.
 */

/* The default CMAKE_BUILD_TYPE here is Release, which puts -DNDEBUG on the
 * command line and would compile every assert() in this file away: the CTest
 * run would then pass without checking anything. Keep them live. */
#undef NDEBUG

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend/moonraker_client.h"
#include "lib/cjson/cJSON.h"

/* The size the allocator really gave a block, for the heap accounting below:
 * each C library names it differently. */
#if defined(__APPLE__)
#include <malloc/malloc.h>
#define usable_size(p) malloc_size(p)
#elif defined(_WIN32)
#include <malloc.h>
#define usable_size(p) _msize(p)
#else
#include <malloc.h>
#define usable_size(p) malloc_usable_size(p)
#endif

#ifndef MOONRAKER_FIXTURE_DIR
#error "compile this test with -DMOONRAKER_FIXTURE_DIR=<abs path to tests/fixtures>"
#endif

/* The client copies the bound unit's name with the model's character-safe
 * copy, so the model is linked in; the model logs group edits through the
 * app's action log, and the real one lives in actions.c and drags in the UI. */
void actions_log(const char *fmt, ...)
{
    (void)fmt;
}

#define MAX_FRAMES 24
#define FRAME_MAX 4096

/* What the client sent, and the last payload of each callback. on_object is
 * one callback for every subscribed key now (see moonraker_client.h); the
 * harness still keeps one count/text pair per kind, so the tests read the
 * same as before. */
typedef struct {
    char frames[MAX_FRAMES][FRAME_MAX];
    int sent;

    int link_count;
    mc_link_t links[24];

    int object_count;      /* every on_object call, any kind */
    char last_kind[16];
    char last_name[64];

    int ui_count;           /* kind == "openams_ui" */
    char ui_unit[64];
    char ui_text[8192];

    int oams_manager_count; /* kind == "oams_manager" (the whole object, not
                              * just its "settings" key) */
    char oams_manager_text[2048];

    int print_stats_count;  /* kind == "print_stats" */
    char print_stats_text[1024];

    int toolhead_count;     /* kind == "toolhead" */
    char toolhead_text[512];

    int component_count;
    char component_text[8192];

    int gcode_count;
    int gcode_id;
    int gcode_ok;
    char gcode_msg[128];
} harness_t;

static harness_t h;

static int serialize(const cJSON *item, char *dst, size_t dst_size)
{
    char *text = cJSON_PrintUnformatted(item);
    size_t n;

    if(!text) {
        dst[0] = '\0';
        return 0;
    }
    n = strlen(text);
    if(n >= dst_size) n = dst_size - 1;
    memcpy(dst, text, n);
    dst[n] = '\0';
    free(text);
    return (int)n;
}

static bool h_send(void *ctx, const char *text, size_t len)
{
    (void)ctx;
    assert(h.sent < MAX_FRAMES);
    assert(len < FRAME_MAX);
    memcpy(h.frames[h.sent], text, len);
    h.frames[h.sent][len] = '\0';
    h.sent++;
    return true;
}

static void h_on_object(void *ctx, const char *kind, const char *name, const cJSON *obj)
{
    (void)ctx;
    h.object_count++;
    snprintf(h.last_kind, sizeof h.last_kind, "%s", kind ? kind : "");
    snprintf(h.last_name, sizeof h.last_name, "%s", name ? name : "");

    if(strcmp(kind, "openams_ui") == 0) {
        h.ui_count++;
        snprintf(h.ui_unit, sizeof h.ui_unit, "%s", name ? name : "");
        serialize(obj, h.ui_text, sizeof h.ui_text);
    }
    else if(strcmp(kind, "oams_manager") == 0) {
        h.oams_manager_count++;
        serialize(obj, h.oams_manager_text, sizeof h.oams_manager_text);
    }
    else if(strcmp(kind, "print_stats") == 0) {
        h.print_stats_count++;
        serialize(obj, h.print_stats_text, sizeof h.print_stats_text);
    }
    else if(strcmp(kind, "toolhead") == 0) {
        h.toolhead_count++;
        serialize(obj, h.toolhead_text, sizeof h.toolhead_text);
    }
}

static void h_on_component_status(void *ctx, const cJSON *status)
{
    (void)ctx;
    h.component_count++;
    serialize(status, h.component_text, sizeof h.component_text);
}

static void h_on_link(void *ctx, mc_link_t state)
{
    (void)ctx;
    assert(h.link_count < 24);
    h.links[h.link_count++] = state;
}

static void h_on_gcode_result(void *ctx, int request_id, bool ok, const char *message)
{
    (void)ctx;
    h.gcode_count++;
    h.gcode_id = request_id;
    h.gcode_ok = ok ? 1 : 0;
    snprintf(h.gcode_msg, sizeof h.gcode_msg, "%s", message ? message : "");
}

static mc_callbacks_t cbs = {
    .ctx = &h,
    .send_text = h_send,
    .on_object = h_on_object,
    .on_component_status = h_on_component_status,
    .on_link = h_on_link,
    .on_gcode_result = h_on_gcode_result,
};

static void reset(void)
{
    memset(&h, 0, sizeof h);
}

/* ------------------------------------------------------------ fixture --- */

static cJSON *load_fixture(const char *name)
{
    char path[512];
    FILE *f;
    long len;
    char *text;
    cJSON *root;

    snprintf(path, sizeof path, "%s/%s.json", MOONRAKER_FIXTURE_DIR, name);
    f = fopen(path, "rb");
    if(!f) {
        fprintf(stderr, "test_moonraker_client: cannot open %s\n", path);
        exit(1);
    }
    if(fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "test_moonraker_client: cannot size %s\n", path);
        exit(1);
    }
    text = malloc((size_t)len + 1);
    assert(text);
    if(fread(text, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "test_moonraker_client: short read on %s\n", path);
        exit(1);
    }
    text[len] = '\0';
    fclose(f);

    root = cJSON_Parse(text);
    free(text);
    if(!root) {
        fprintf(stderr, "test_moonraker_client: %s is not valid JSON\n", path);
        exit(1);
    }
    return root;
}

/** A private copy of the first `kind` event's payload in `loading.json`. */
static cJSON *fixture_payload(const char *kind)
{
    cJSON *root = load_fixture("loading");
    const cJSON *events = cJSON_GetObjectItemCaseSensitive(root, "events");
    const cJSON *ev;
    cJSON *payload = NULL;

    cJSON_ArrayForEach(ev, events) {
        const cJSON *k = cJSON_GetObjectItemCaseSensitive(ev, "kind");

        if(cJSON_IsString(k) && strcmp(k->valuestring, kind) == 0) {
            payload = cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(ev, "payload"), true);
            break;
        }
    }
    cJSON_Delete(root);
    assert(payload);
    return payload;
}

/* -------------------------------------------------------------- driving --- */

static void feed(cJSON *msg, uint32_t now_ms)
{
    char *text;
    bool ok;

    assert(msg);
    text = cJSON_PrintUnformatted(msg);
    assert(text);
    ok = mc_on_text(text, strlen(text), now_ms);
    free(text);
    cJSON_Delete(msg);
    assert(ok);
}

static void reply_result(int id, uint32_t now_ms, cJSON *result)
{
    cJSON *m = cJSON_CreateObject();

    cJSON_AddStringToObject(m, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(m, "id", (double)id);
    if(result) cJSON_AddItemToObject(m, "result", result);
    feed(m, now_ms);
}

static void reply_error(int id, uint32_t now_ms, int code, const char *message)
{
    cJSON *m = cJSON_CreateObject();
    cJSON *error = cJSON_CreateObject();

    cJSON_AddStringToObject(m, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(m, "id", (double)id);
    cJSON_AddItemToObject(m, "error", error);
    cJSON_AddNumberToObject(error, "code", (double)code);
    cJSON_AddStringToObject(error, "message", message);
    feed(m, now_ms);
}

/** Assert the shape of frame @p i: JSON-RPC 2.0, method @p method, id @p id. */
static void assert_frame_method(int i, const char *method, int id)
{
    cJSON *j;
    const cJSON *m;
    const cJSON *rid;
    const cJSON *v;

    assert(i < h.sent);
    j = cJSON_Parse(h.frames[i]);
    assert(j);
    m = cJSON_GetObjectItemCaseSensitive(j, "method");
    rid = cJSON_GetObjectItemCaseSensitive(j, "id");
    v = cJSON_GetObjectItemCaseSensitive(j, "jsonrpc");
    assert(cJSON_IsString(m) && strcmp(m->valuestring, method) == 0);
    assert(cJSON_IsNumber(rid) && rid->valueint == id);
    assert(cJSON_IsString(v) && strcmp(v->valuestring, "2.0") == 0);
    cJSON_Delete(j);
}

/* --------------------------------------------------------- the script --- */

static void test_connect_flow(void)
{
    cJSON *ui = fixture_payload("openams_ui");
    cJSON *component = fixture_payload("component_status");
    cJSON *info = cJSON_CreateObject();
    cJSON *result;
    cJSON *objects = cJSON_CreateArray();
    cJSON *status = cJSON_CreateObject();
    cJSON *print_stats = cJSON_CreateObject();

    printf("connect_flow\n");
    reset();
    mc_init(&cbs, NULL);
    assert(mc_link() == MC_LINK_DOWN);

    mc_on_connected(0);
    assert(mc_link() == MC_LINK_CONNECTING);
    assert(h.sent == 1);
    assert(h.link_count == 1 && h.links[0] == MC_LINK_CONNECTING);
    assert_frame_method(0, "server.info", 1);
    assert(mc_send_gcode("OPENAMS_LOAD") == -1); /* not READY yet */

    cJSON_AddBoolToObject(info, "klippy_connected", true);
    cJSON_AddStringToObject(info, "klippy_state", "ready");
    reply_result(1, 0, info);

    assert(h.sent == 2);
    assert_frame_method(1, "printer.objects.list", 2);

    result = cJSON_CreateObject();
    cJSON_AddItemToObject(result, "objects", objects);
    cJSON_AddItemToArray(objects, cJSON_CreateString("oams_manager"));
    cJSON_AddItemToArray(objects, cJSON_CreateString("openams_ui oams1"));
    cJSON_AddItemToArray(objects, cJSON_CreateString("print_stats"));
    cJSON_AddItemToArray(objects, cJSON_CreateString("toolhead"));
    reply_result(2, 0, result);

    assert(h.sent == 3);
    assert_frame_method(2, "printer.objects.subscribe", 3);
    /* The subscription names the chosen projection, print_stats and toolhead. */
    {
        cJSON *j = cJSON_Parse(h.frames[2]);
        const cJSON *params = cJSON_GetObjectItemCaseSensitive(j, "params");
        const cJSON *objs = cJSON_GetObjectItemCaseSensitive(params, "objects");
        const cJSON *manager = cJSON_GetObjectItemCaseSensitive(objs, "oams_manager");
        const cJSON *toolhead = cJSON_GetObjectItemCaseSensitive(objs, "toolhead");

        assert(cJSON_IsObject(objs));
        assert(cJSON_GetObjectItemCaseSensitive(objs, "openams_ui oams1") != NULL);
        assert(cJSON_GetObjectItemCaseSensitive(objs, "print_stats") != NULL);
        /* toolhead.extruder is asked for unconditionally (a base Klipper
         * object, always present). */
        assert(cJSON_IsArray(toolhead) && cJSON_GetArraySize(toolhead) == 1);
        assert(strcmp(cJSON_GetArrayItem(toolhead, 0)->valuestring, "extruder") == 0);
        /* the object list carries oams_manager, so the subscribe asks for its
         * two keys, as an attribute list rather than the whole object */
        assert(cJSON_IsArray(manager) && cJSON_GetArraySize(manager) == 2);
        assert(strcmp(cJSON_GetArrayItem(manager, 0)->valuestring, "settings") == 0);
        assert(strcmp(cJSON_GetArrayItem(manager, 1)->valuestring, "lanes_by_fps") == 0);
        assert(cJSON_GetArraySize(objs) == 4);
        cJSON_Delete(j);
    }

    cJSON_AddItemToObject(status, "openams_ui oams1", cJSON_Duplicate(ui, true));
    {
        cJSON *manager = cJSON_CreateObject();
        cJSON *settings = cJSON_CreateObject();

        cJSON_AddBoolToObject(settings, "read_tag_on_insertion", true);
        cJSON_AddBoolToObject(settings, "read_tags_at_startup", true);
        cJSON_AddBoolToObject(settings, "auto_reload_on_runout", true);
        cJSON_AddBoolToObject(settings, "auto_create_spoolman_spools", true);
        cJSON_AddBoolToObject(settings, "apply_pa_on_load", false);
        cJSON_AddItemToObject(manager, "settings", settings);
        cJSON_AddItemToObject(status, "oams_manager", manager);
    }
    cJSON_AddStringToObject(print_stats, "state", "standby");
    cJSON_AddItemToObject(status, "print_stats", print_stats);
    result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "eventtime", 0.0);
    cJSON_AddItemToObject(result, "status", status);
    reply_result(3, 0, result);

    assert(mc_link() == MC_LINK_READY);
    /* the component's bays are asked for only once the unit exists */
    assert(h.sent == 4);
    assert_frame_method(3, "server.openams_spoolman.status", 4);
    assert(h.ui_count == 1);
    assert(strcmp(h.ui_unit, "oams1") == 0);
    assert(strstr(h.ui_text, "\"unit\":\"oams1\"") != NULL);
    /* the subscribe result carries oams_manager (with a "settings" key), so
     * the callback fires once from it */
    assert(h.oams_manager_count == 1);
    assert(strstr(h.oams_manager_text, "\"read_tag_on_insertion\":true") != NULL);
    assert(h.print_stats_count == 1);
    assert(strstr(h.print_stats_text, "standby") != NULL);

    reply_result(4, 0, component);
    assert(h.component_count == 1);
    assert(strstr(h.component_text, "spoolman_online") != NULL);

    mc_deinit();
    cJSON_Delete(ui);
}

/* ------------------------------------------------ shared driving helpers --- */

/** The JSON-RPC id of sent frame @p i. */
static int frame_id_at(int i)
{
    cJSON *j;
    const cJSON *id;
    int value;

    assert(i >= 0 && i < h.sent);
    j = cJSON_Parse(h.frames[i]);
    assert(j);
    id = cJSON_GetObjectItemCaseSensitive(j, "id");
    assert(cJSON_IsNumber(id));
    value = id->valueint;
    cJSON_Delete(j);
    return value;
}

/** The JSON-RPC id of the most recently sent frame. */
static int last_frame_id(void)
{
    return frame_id_at(h.sent - 1);
}

static cJSON *notification(const char *method, cJSON *params)
{
    cJSON *m = cJSON_CreateObject();

    cJSON_AddStringToObject(m, "jsonrpc", "2.0");
    cJSON_AddStringToObject(m, "method", method);
    if(params) cJSON_AddItemToObject(m, "params", params);
    return m;
}

/**
 * Drive the handshake to READY. The objects list names two projections so the
 * bound/unbound choice is visible; @p obj_name is the one expected to be
 * picked. The component-status request is answered with a minimal status.
 * Requires a clean harness (reset() first).
 */
static void handshake_to_ready(const char *bound, const char *obj_name,
                               const cJSON *ui, uint32_t now_ms)
{
    cJSON *info = cJSON_CreateObject();
    cJSON *result;
    cJSON *objects = cJSON_CreateArray();
    cJSON *status = cJSON_CreateObject();
    cJSON *print_stats = cJSON_CreateObject();
    cJSON *component = cJSON_CreateObject();

    mc_init(&cbs, bound);
    mc_on_connected(now_ms);
    assert_frame_method(0, "server.info", 1);

    cJSON_AddBoolToObject(info, "klippy_connected", true);
    cJSON_AddStringToObject(info, "klippy_state", "ready");
    reply_result(1, now_ms, info);
    assert_frame_method(1, "printer.objects.list", 2);

    result = cJSON_CreateObject();
    cJSON_AddItemToObject(result, "objects", objects);
    cJSON_AddItemToArray(objects, cJSON_CreateString("oams_manager"));
    cJSON_AddItemToArray(objects, cJSON_CreateString("openams_ui oams2"));
    cJSON_AddItemToArray(objects, cJSON_CreateString("openams_ui oams1"));
    cJSON_AddItemToArray(objects, cJSON_CreateString("print_stats"));
    cJSON_AddItemToArray(objects, cJSON_CreateString("toolhead"));
    reply_result(2, now_ms, result);

    assert_frame_method(2, "printer.objects.subscribe", 3);
    assert(h.sent == 3);

    cJSON_AddItemToObject(status, obj_name, cJSON_Duplicate(ui, true));
    cJSON_AddStringToObject(print_stats, "state", "standby");
    cJSON_AddItemToObject(status, "print_stats", print_stats);
    result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "eventtime", 0.0);
    cJSON_AddItemToObject(result, "status", status);
    reply_result(3, now_ms, result);

    assert(mc_link() == MC_LINK_READY);
    assert_frame_method(3, "server.openams_spoolman.status", 4);
    assert(h.ui_count == 1);

    cJSON_AddBoolToObject(component, "spoolman_online", true);
    reply_result(4, now_ms, component);
}

/* --------------------------------------------------------------- tests --- */

static void test_selection(void)
{
    cJSON *ui = fixture_payload("openams_ui");

    printf("selection\n");
    reset();
    handshake_to_ready(NULL, "openams_ui oams2", ui, 0);
    assert(strcmp(h.ui_unit, "oams2") == 0);
    mc_deinit();

    reset();
    handshake_to_ready("oams1", "openams_ui oams1", ui, 0);
    assert(strcmp(h.ui_unit, "oams1") == 0);
    mc_deinit();
    cJSON_Delete(ui);
}

/* The client no longer merges a status-update diff into anything (U1b: that
 * is core_apply's job, over a copy it owns); it forwards each diff entry
 * exactly as Moonraker sent it. A diff for the bound object therefore does
 * NOT carry `unit`/`bays` unless the diff itself did. */
static void test_status_update_passthrough(void)
{
    cJSON *ui = fixture_payload("openams_ui");
    cJSON *params;
    cJSON *diff;
    cJSON *object;
    cJSON *lane;
    cJSON *manager;
    cJSON *toolhead;
    cJSON *raw;
    int ui_before, manager_before, toolhead_before;

    printf("status_update_passthrough\n");
    reset();
    handshake_to_ready("oams1", "openams_ui oams1", ui, 0);
    ui_before = h.ui_count;
    manager_before = h.oams_manager_count;
    toolhead_before = h.toolhead_count;

    params = cJSON_CreateArray();
    diff = cJSON_CreateObject();
    object = cJSON_CreateObject();
    lane = cJSON_CreateObject();
    cJSON_AddStringToObject(lane, "id", "fps");
    cJSON_AddStringToObject(lane, "op", "IDLE");
    cJSON_AddStringToObject(lane, "group", "T1");
    cJSON_AddItemToObject(object, "lane", lane);
    cJSON_AddNumberToObject(object, "extra", 7); /* a key the full object lacks */
    cJSON_AddItemToObject(diff, "openams_ui oams1", object);
    cJSON_AddItemToObject(diff, "print_stats", cJSON_CreateString("printing"));
    manager = cJSON_CreateObject();
    cJSON_AddItemToObject(manager, "lanes_by_fps", cJSON_CreateObject());
    cJSON_AddItemToObject(diff, "oams_manager", manager);
    toolhead = cJSON_CreateObject();
    cJSON_AddStringToObject(toolhead, "extruder", "extruder1");
    cJSON_AddItemToObject(diff, "toolhead", toolhead);
    cJSON_AddItemToArray(params, diff);
    cJSON_AddItemToArray(params, cJSON_CreateNumber(2.5));
    feed(notification("notify_status_update", params), 5);

    assert(h.ui_count == ui_before + 1);
    raw = cJSON_Parse(h.ui_text);
    assert(raw);
    {
        const cJSON *unit = cJSON_GetObjectItemCaseSensitive(raw, "unit");
        const cJSON *rlane = cJSON_GetObjectItemCaseSensitive(raw, "lane");
        const cJSON *op = cJSON_GetObjectItemCaseSensitive(rlane, "op");
        const cJSON *bays = cJSON_GetObjectItemCaseSensitive(raw, "bays");
        const cJSON *extra = cJSON_GetObjectItemCaseSensitive(raw, "extra");

        /* the raw diff, not a copy merged with the full object: no unit, no
         * bays, just what this diff carried */
        assert(unit == NULL);
        assert(bays == NULL);
        assert(cJSON_IsString(op) && strcmp(op->valuestring, "IDLE") == 0);
        assert(cJSON_IsNumber(extra) && extra->valueint == 7);
    }
    cJSON_Delete(raw);
    assert(h.print_stats_count >= 1);
    assert(strstr(h.print_stats_text, "printing") != NULL);
    assert(h.oams_manager_count == manager_before + 1);
    assert(strstr(h.oams_manager_text, "lanes_by_fps") != NULL);
    assert(h.toolhead_count == toolhead_before + 1);
    assert(strstr(h.toolhead_text, "extruder1") != NULL);

    mc_deinit();
    cJSON_Delete(ui);
}

static void test_component_notification(void)
{
    cJSON *ui = fixture_payload("openams_ui");
    cJSON *component = fixture_payload("component_status");
    cJSON *params;
    int before;

    printf("component_notification\n");
    reset();
    handshake_to_ready(NULL, "openams_ui oams2", ui, 0);
    before = h.component_count;

    params = cJSON_CreateArray();
    cJSON_AddItemToArray(params, component); /* owned by params now */
    feed(notification("notify_openams_spoolman_status", params), 5);

    assert(h.component_count == before + 1);
    assert(strstr(h.component_text, "spoolman_online") != NULL);
    assert(strstr(h.component_text, "oams1-0") != NULL);

    mc_deinit();
    cJSON_Delete(ui);
}

static void test_gcode(void)
{
    cJSON *ui = fixture_payload("openams_ui");
    int id;

    printf("gcode\n");
    reset();
    handshake_to_ready("oams1", "openams_ui oams1", ui, 0);

    id = mc_send_gcode("OPENAMS_LOAD");
    assert(id == 5);
    assert(h.sent == 5);
    assert_frame_method(4, "printer.gcode.script", 5);
    {
        cJSON *j = cJSON_Parse(h.frames[4]);
        const cJSON *params = cJSON_GetObjectItemCaseSensitive(j, "params");
        const cJSON *script = cJSON_GetObjectItemCaseSensitive(params, "script");
        assert(cJSON_IsString(script) && strcmp(script->valuestring, "OPENAMS_LOAD") == 0);
        cJSON_Delete(j);
    }

    reply_result(id, 0, cJSON_CreateString("ok"));
    assert(h.gcode_count == 1);
    assert(h.gcode_id == id);
    assert(h.gcode_ok == 1);
    assert(strcmp(h.gcode_msg, "") == 0);

    id = mc_send_gcode("OPENAMS_UNLOAD");
    assert(id == 6);
    reply_error(id, 0, -32000, "Unknown object: openams_ui");
    assert(h.gcode_count == 2);
    assert(h.gcode_id == id);
    assert(h.gcode_ok == 0);
    assert(strcmp(h.gcode_msg, "Unknown object: openams_ui") == 0);

    mc_deinit();
    cJSON_Delete(ui);
}

static void test_gcode_timeout(void)
{
    cJSON *ui = fixture_payload("openams_ui");
    int id;

    printf("gcode_timeout\n");
    reset();
    handshake_to_ready("oams1", "openams_ui oams1", ui, 0);

    id = mc_send_gcode("OPENAMS_LOAD");
    assert(id == 5);
    assert(h.sent == 5);

    mc_poll(9999);
    assert(h.gcode_count == 0);
    assert(h.sent == 5);

    mc_poll(10000);
    assert(h.gcode_count == 1);
    assert(h.gcode_id == id);
    assert(h.gcode_ok == 0);
    assert(strcmp(h.gcode_msg, "timeout") == 0);
    /* a gcode timeout must not restart the handshake */
    assert(mc_link() == MC_LINK_READY);
    assert(h.sent == 5);

    /* an answer after the timeout is an unknown id: ignored */
    reply_result(id, 10000, cJSON_CreateString("late"));
    assert(h.gcode_count == 1);

    mc_deinit();
    cJSON_Delete(ui);
}

/* A load or an unload is answered only once it has run, so it waits
 * MC_GCODE_RUN_TIMEOUT_MS, not the 10 s of a plain request: a working load
 * once came back as "timeout" while the printer was still feeding. */
static void test_gcode_run_timeout(void)
{
    cJSON *ui = fixture_payload("openams_ui");
    int id;

    printf("gcode_run_timeout\n");
    reset();
    handshake_to_ready("oams1", "openams_ui oams1", ui, 0);

    id = mc_send_gcode_within("OPENAMS_LOAD", MC_GCODE_RUN_TIMEOUT_MS);
    assert(id == 5);
    mc_poll(10000);
    mc_poll(60000);
    assert(h.gcode_count == 0);              /* still running a minute in */

    reply_result(id, 61000, cJSON_CreateString("ok"));
    assert(h.gcode_count == 1);
    assert(h.gcode_ok == 1);

    mc_poll(61000);
    id = mc_send_gcode_within("OPENAMS_UNLOAD", MC_GCODE_RUN_TIMEOUT_MS);
    mc_poll(61000 + MC_GCODE_RUN_TIMEOUT_MS - 1);
    assert(h.gcode_count == 1);
    mc_poll(61000 + MC_GCODE_RUN_TIMEOUT_MS);
    assert(h.gcode_count == 2);
    assert(h.gcode_id == id);
    assert(h.gcode_ok == 0);
    assert(strcmp(h.gcode_msg, "timeout") == 0);
    assert(mc_link() == MC_LINK_READY);

    mc_deinit();
    cJSON_Delete(ui);
}

static void test_internal_timeout_restarts(void)
{
    printf("internal_timeout_restarts\n");
    reset();
    mc_init(&cbs, NULL);
    mc_on_connected(0);
    assert(h.sent == 1);

    mc_poll(9999);
    assert(h.sent == 1);

    mc_poll(10000);
    assert(h.sent == 2);
    assert_frame_method(1, "server.info", 2);
    assert(mc_link() == MC_LINK_CONNECTING);

    mc_deinit();
}

static void test_disconnect_backoff(void)
{
    cJSON *ui = fixture_payload("openams_ui");
    static const uint32_t delays[] = { 2000, 4000, 8000, 16000, 30000, 30000 };
    uint32_t t;
    size_t k;
    int id;

    printf("disconnect_backoff\n");
    reset();
    handshake_to_ready("oams1", "openams_ui oams1", ui, 0);

    id = mc_send_gcode("OPENAMS_LOAD");
    assert(id == 5);

    /* Ready, so the first delay is the 1 s floor. */
    mc_on_disconnected(1000);
    assert(mc_link() == MC_LINK_DOWN);
    assert(h.gcode_count == 1);
    assert(h.gcode_ok == 0);
    assert(strcmp(h.gcode_msg, "disconnected") == 0);
    assert(!mc_reconnect_due(1000));
    assert(!mc_reconnect_due(1999));
    assert(mc_reconnect_due(2000));

    t = 3000;
    for(k = 0; k < sizeof delays / sizeof delays[0]; k++) {
        mc_on_connected(t);
        assert(!mc_reconnect_due(t));
        mc_on_disconnected(t);
        assert(!mc_reconnect_due(t + delays[k] - 1));
        assert(mc_reconnect_due(t + delays[k]));
        t += 100000;
    }

    /* Reaching READY resets the backoff to the floor. */
    mc_on_connected(t);
    {
        cJSON *info = cJSON_CreateObject();
        cJSON *result;
        cJSON *objects = cJSON_CreateArray();
        cJSON *status = cJSON_CreateObject();
        cJSON *print_stats = cJSON_CreateObject();

        cJSON_AddBoolToObject(info, "klippy_connected", true);
        cJSON_AddStringToObject(info, "klippy_state", "ready");
        reply_result(last_frame_id(), t, info);

        result = cJSON_CreateObject();
        cJSON_AddItemToObject(result, "objects", objects);
        cJSON_AddItemToArray(objects, cJSON_CreateString("openams_ui oams1"));
        reply_result(last_frame_id(), t, result);

        cJSON_AddItemToObject(status, "openams_ui oams1", cJSON_Duplicate(ui, true));
        cJSON_AddStringToObject(print_stats, "state", "standby");
        cJSON_AddItemToObject(status, "print_stats", print_stats);
        result = cJSON_CreateObject();
        cJSON_AddItemToObject(result, "status", status);

        /* objects.list is answered with the subscription request alone (the
         * component-status request follows the subscribe reply), so the
         * subscribe is the last frame. */
        reply_result(last_frame_id(), t, result);
    }
    assert(mc_link() == MC_LINK_READY);

    mc_on_disconnected(t + 10);
    assert(!mc_reconnect_due(t + 10 + 999));
    assert(mc_reconnect_due(t + 10 + 1000));

    mc_deinit();
    cJSON_Delete(ui);
}

static void test_klippy_wait(void)
{
    cJSON *info;
    int sent_before;

    printf("klippy_wait\n");
    reset();
    mc_init(&cbs, NULL);
    mc_on_connected(0);
    assert(h.sent == 1);

    info = cJSON_CreateObject();
    cJSON_AddBoolToObject(info, "klippy_connected", true);
    cJSON_AddStringToObject(info, "klippy_state", "startup");
    reply_result(1, 0, info);

    assert(mc_link() == MC_LINK_KLIPPY_WAIT);
    assert(h.sent == 1); /* no objects.list while Klippy is starting */
    assert(h.link_count >= 1 && h.links[h.link_count - 1] == MC_LINK_KLIPPY_WAIT);

    mc_poll(1999);
    assert(h.sent == 1);
    sent_before = h.sent;
    mc_poll(2000);
    assert(h.sent == sent_before + 1);
    assert_frame_method(h.sent - 1, "server.info", 2);

    /* A ready answer resumes the handshake. */
    info = cJSON_CreateObject();
    cJSON_AddBoolToObject(info, "klippy_connected", true);
    cJSON_AddStringToObject(info, "klippy_state", "ready");
    reply_result(2, 2000, info);
    assert(mc_link() == MC_LINK_CONNECTING);
    assert_frame_method(h.sent - 1, "printer.objects.list", 3);

    mc_deinit();
}

static void feed_raw(const char *text)
{
    assert(mc_on_text(text, strlen(text), 0));
}

static void test_malformed(void)
{
    const char *unknown_method =
        "{\"jsonrpc\":\"2.0\",\"method\":\"notify_unknown\",\"params\":[]}";
    const char *unknown_id =
        "{\"jsonrpc\":\"2.0\",\"id\":9999,\"result\":{}}";

    printf("malformed\n");
    reset();
    mc_init(&cbs, NULL);

    assert(!mc_on_text(NULL, 0, 0));
    assert(!mc_on_text("", 0, 0));
    assert(!mc_on_text("not json", 8, 0));
    assert(!mc_on_text("{\"id\":1,\"result\":", 17, 0));
    assert(!mc_on_text("[]", 2, 0));
    assert(!mc_on_text("42", 2, 0));

    /* Well formed, but not a message the client acts on: accepted, ignored. */
    feed_raw("{}");
    feed_raw(unknown_method);
    feed_raw(unknown_id);

    mc_deinit();
}

static void test_oversized(void)
{
    cJSON *m;
    cJSON *params;
    char *pad = malloc(20001);
    char *text;

    printf("oversized\n");
    reset();
    mc_init(&cbs, NULL);
    assert(pad);
    memset(pad, 'a', 20000);
    pad[20000] = '\0';

    params = cJSON_CreateArray();
    cJSON_AddItemToArray(params, cJSON_CreateString(pad));
    cJSON_AddItemToArray(params, cJSON_CreateNumber(0));
    m = notification("notify_status_update", params);

    text = cJSON_PrintUnformatted(m);
    assert(text);
    assert(strlen(text) > 16384);
    assert(!mc_on_text(text, strlen(text), 0));
    free(text);

    /* The same shape under the limit is accepted. */
    cJSON_ReplaceItemInArray(params, 0, cJSON_CreateString("x"));
    text = cJSON_PrintUnformatted(m);
    assert(text);
    assert(strlen(text) <= 16384);
    assert(mc_on_text(text, strlen(text), 0));
    free(text);

    cJSON_Delete(m);
    free(pad);
    mc_deinit();
}


/* ---------------------------------------------- raw scan and broadcasts --- */

/* A cJSON allocator that counts, and refuses an allocation that would take
 * the live total over a budget: the stand-in for the ESP32's small heap. */
static size_t g_alloc_budget;
static size_t g_alloc_live;
static int g_alloc_calls;
static int g_alloc_refused;

static void *budget_malloc(size_t n)
{
    void *p;

    g_alloc_calls++;
    if(g_alloc_budget && g_alloc_live + n > g_alloc_budget) {
        g_alloc_refused++;
        return NULL;
    }
    p = malloc(n);
    if(p) g_alloc_live += usable_size(p);
    return p;
}

static void budget_free(void *p)
{
    /* The client frees printed requests with plain free(), so this must be
     * the plain allocator underneath: only the accounting is ours. */
    if(p) {
        size_t n = usable_size(p);

        g_alloc_live = g_alloc_live > n ? g_alloc_live - n : 0;
    }
    free(p);
}

static void budget_begin(size_t budget)
{
    cJSON_Hooks hooks;

    g_alloc_budget = budget;
    g_alloc_live = 0;
    g_alloc_calls = 0;
    g_alloc_refused = 0;
    hooks.malloc_fn = budget_malloc;
    hooks.free_fn = budget_free;
    cJSON_InitHooks(&hooks);
}

static void budget_end(void)
{
    cJSON_InitHooks(NULL);
    g_alloc_budget = 0;
}

/** Feed raw text; returns mc_on_text's answer. */
static bool text_in(const char *text, uint32_t now_ms)
{
    return mc_on_text(text, strlen(text), now_ms);
}

/** Connect and answer server.info, leaving the list request (id 2) pending. */
static void start_to_list(const char *bound)
{
    reset();
    mc_init(&cbs, bound);
    mc_on_connected(0);
    assert_frame_method(0, "server.info", 1);
    assert(text_in("{\"jsonrpc\": \"2.0\", \"result\": {\"klippy_state\": \"ready\"}, \"id\": 1}", 0));
    assert_frame_method(1, "printer.objects.list", 2);
}

/** Answer the list request with `{"objects": [names_json]}`. */
static void list_reply(const char *names_json)
{
    char text[24000];

    snprintf(text, sizeof text,
             "{\"jsonrpc\": \"2.0\", \"result\": {\"objects\": [%s]}, \"id\": 2}", names_json);
    assert(text_in(text, 0));
}

static void assert_subscribed_to(const char *object, bool manager, bool toolhead)
{
    assert(h.sent == 3);
    assert_frame_method(2, "printer.objects.subscribe", 3);
    assert(strstr(h.frames[2], object) != NULL);
    assert((strstr(h.frames[2], "\"oams_manager\"") != NULL) == manager);
    assert((strstr(h.frames[2], "\"toolhead\"") != NULL) == toolhead);
}

static void test_list_scan(void)
{
    char big[24000];
    char name[200];
    size_t o = 0;
    int i;

    printf("list_scan\n");

    /* Bound: the named unit wins over an earlier one. */
    start_to_list("oams1");
    list_reply("\"gcode\", \"openams_ui oams2\", \"oams_manager\", \"openams_ui oams1\", \"toolhead\"");
    assert_subscribed_to("openams_ui oams1", true, true);
    assert(mc_link() == MC_LINK_CONNECTING);
    mc_deinit();

    /* Unbound: the first openams_ui object. */
    start_to_list(NULL);
    list_reply("\"toolhead\", \"openams_ui oams2\", \"openams_ui oams1\"");
    assert_subscribed_to("openams_ui oams2", false, true);
    mc_deinit();

    /* The id may come first, and whitespace is free. */
    start_to_list(NULL);
    assert(text_in("{\"id\":2,\"result\":{\"objects\":[\"openams_ui a\"]},\"jsonrpc\":\"2.0\"}", 0));
    assert_subscribed_to("openams_ui a", false, false);
    mc_deinit();

    /* No openams_ui object at all: wait for Klippy, send nothing more. */
    start_to_list(NULL);
    list_reply("\"gcode\", \"toolhead\", \"oams_manager\"");
    assert(h.sent == 2);
    assert(mc_link() == MC_LINK_KLIPPY_WAIT);
    mc_deinit();

    /* The bound unit is not in the list (another unit is): same. */
    start_to_list("oams9");
    list_reply("\"openams_ui oams1\", \"toolhead\"");
    assert(h.sent == 2);
    assert(mc_link() == MC_LINK_KLIPPY_WAIT);
    mc_deinit();

    /* A list that omits oams_manager and toolhead subscribes without them. */
    start_to_list("oams1");
    list_reply("\"print_stats\", \"openams_ui oams1\"");
    assert_subscribed_to("openams_ui oams1", false, false);
    mc_deinit();

    /* Escapes:   is a space, so the escaped name is the real one; an
     * escaped quote inside an unrelated name must not end the string early;
     * a surrogate pair decodes (and matches nothing). */
    start_to_list("oams1");
    list_reply("\"a\\\"b, \\\"openams_ui oams1\", \"x\\\\\", \"\\ud83d\\ude00\", "
               "\"openams_ui\\u0020oams1\", \"tool\\u0068ead\"");
    assert_subscribed_to("openams_ui oams1", false, true);
    mc_deinit();

    /* An escaped NUL never truncates into a false match. */
    start_to_list("oams1");
    list_reply("\"openams_ui oams1\\u0000junk\", \"toolhead\"");
    assert(h.sent == 2);
    assert(mc_link() == MC_LINK_KLIPPY_WAIT);
    mc_deinit();

    /* A name longer than the buffers is skipped, never cut into a match:
     * 63 visible characters fit, 64 do not. */
    memset(name, 'u', 80);
    name[80] = '\0';
    start_to_list(NULL);
    snprintf(big, sizeof big, "\"openams_ui %s\", \"openams_ui ok\"", name);
    list_reply(big);
    assert_subscribed_to("openams_ui ok", false, false);
    mc_deinit();

    start_to_list(NULL);
    snprintf(big, sizeof big, "\"oams_manager%*s\", \"toolhead\"", 60, "");
    list_reply(big); /* a long name that starts like oams_manager */
    assert(h.sent == 2);
    assert(mc_link() == MC_LINK_KLIPPY_WAIT);
    mc_deinit();

    /* 1000 names under an allocator with a small live-heap budget:
     * the scan path must not need heap in proportion to the list. The same
     * text through cJSON fails under the same budget, proving the limit
     * bites. */
    for(i = 0; i < 1000; i++) {
        o += (size_t)snprintf(big + o, sizeof big - o, "%s\"obj_%03d\"", i ? "," : "", i);
    }
    o += (size_t)snprintf(big + o, sizeof big - o, ",\"openams_ui oams1\",\"toolhead\"");
    assert(o < 16000);
    start_to_list("oams1");
    {
        static const char head[] = "{\"jsonrpc\":\"2.0\",\"result\":{\"objects\":[";
        static const char tail[] = "]},\"id\":2}";
        char *text = malloc(sizeof head + o + sizeof tail);
        size_t n = sizeof head - 1;
        cJSON *tree;
        bool ok;

        assert(text);
        memcpy(text, head, n);
        memcpy(text + n, big, o);
        n += o;
        memcpy(text + n, tail, sizeof tail);
        n += sizeof tail - 1;

        /* The scan runs on a 4 KB live-heap budget ... */
        budget_begin(4096);
        ok = mc_on_text(text, n, 0);
        assert(ok);
        assert(g_alloc_refused == 0);
        budget_end();

        /* ... which is far too little for the tree cJSON would build. */
        budget_begin(4096);
        tree = cJSON_ParseWithLength(text, n);
        assert(tree == NULL);
        assert(g_alloc_refused > 0);
        budget_end();
        free(text);
    }
    assert_subscribed_to("openams_ui oams1", false, true);
    mc_deinit();

    /* An error reply to the list is not scanned: the normal path restarts. */
    start_to_list(NULL);
    assert(text_in("{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-1,\"message\":\"x\"},\"id\":2}", 0));
    assert_frame_method(2, "server.info", 3);
    mc_deinit();

    /* A reply for an id nobody is waiting on is ignored. */
    start_to_list(NULL);
    assert(text_in("{\"jsonrpc\":\"2.0\",\"result\":{\"objects\":[\"openams_ui a\"]},\"id\":77}", 0));
    assert(h.sent == 2);
    mc_deinit();

    /* A malformed list (unterminated array) is refused, not half-applied. */
    start_to_list(NULL);
    assert(!text_in("{\"jsonrpc\":\"2.0\",\"result\":{\"objects\":[\"openams_ui a\"},\"id\":2}", 0));
    assert(h.sent == 2);
    mc_deinit();
}

static void test_dropped_broadcasts(void)
{
    static const char *const dropped[] = {
        "{\"jsonrpc\": \"2.0\", \"method\": \"notify_proc_stat_update\", \"params\": [{\"moonraker_stats\": {\"time\": 1.5, \"cpu_usage\": 3.2}, \"cpu_temp\": 48.0}]}",
        "{\"jsonrpc\":\"2.0\",\"method\":\"notify_gcode_response\",\"params\":[\"ok\"]}",
        "{\"jsonrpc\":\"2.0\",\"method\":\"notify_filelist_changed\",\"params\":[{}]}",
        "{\"jsonrpc\":\"2.0\",\"method\":\"notify_klippy_ready\"}",
    };
    size_t i;
    int before;

    printf("dropped_broadcasts\n");
    reset();
    mc_init(&cbs, "oams1");
    mc_on_connected(0);
    before = h.sent;

    for(i = 0; i < sizeof dropped / sizeof dropped[0]; i++) {
        budget_begin(0);
        assert(text_in(dropped[i], 0));
        assert(g_alloc_calls == 0); /* not even a parse was tried */
        budget_end();
    }
    assert(h.sent == before);
    assert(h.object_count == 0);
    assert(h.component_count == 0);

    /* The two methods the display reads are still parsed and delivered. */
    budget_begin(0);
    assert(text_in("{\"jsonrpc\":\"2.0\",\"method\":\"notify_openams_spoolman_status\","
                    "\"params\":[{\"spoolman_online\":true}]}", 0));
    assert(g_alloc_calls > 0);
    budget_end();
    assert(h.component_count == 1);
    mc_deinit();
}

/* openams_spoolman is optional: a host without it answers the component
 * request with "Method not found", which must not restart the handshake. */
static void test_component_status_error(void)
{
    printf("component_status_error\n");
    start_to_list("oams1");
    list_reply("\"openams_ui oams1\"");
    assert_subscribed_to("openams_ui oams1", false, false);
    assert(text_in("{\"jsonrpc\":\"2.0\",\"result\":{\"eventtime\":0,\"status\":{}},\"id\":3}", 0));
    assert(mc_link() == MC_LINK_READY);
    assert(h.sent == 4);
    assert_frame_method(3, "server.openams_spoolman.status", 4);
    reply_error(4, 0, -32601, "Method not found");
    assert(h.sent == 4); /* nothing restarted */
    assert(mc_link() == MC_LINK_READY);
    mc_deinit();
}

int main(void)
{
    test_connect_flow();
    test_selection();
    test_status_update_passthrough();
    test_component_notification();
    test_gcode();
    test_gcode_timeout();
    test_gcode_run_timeout();
    test_internal_timeout_restarts();
    test_disconnect_backoff();
    test_klippy_wait();
    test_malformed();
    test_oversized();
    test_list_scan();
    test_dropped_broadcasts();
    test_component_status_error();
    printf("test_moonraker_client: all tests passed\n");
    return 0;
}
