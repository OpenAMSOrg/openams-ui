/**
 * @file core_wasm.c
 * The C shim of the openams-core WebAssembly module (docs/design/UNIFIED_UI.md
 * 8): one model and one core, static inside the module, driven through six
 * functions that take and return C strings. web/core/index.js is the only
 * caller.
 *
 * Every returned string is malloc'd: the caller frees it with core_free().
 * Nothing here reads g_mmu; the module's model is s_model, and the
 * view/actions/core code all take a model pointer.
 */

#include "backend/actions_map.h"
#include "backend/status_map.h"
#include "core/core.h"
#include "lib/cjson/cJSON.h"
#include "model/mmu_model.h"
#include "view/strings.h"
#include "view/view_json.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The module's exports are core_init/core_reset/core_apply/core_view/
 * core_action/core_free (docs/design/UNIFIED_UI.md 8). Two of those names are
 * already core.h's C API (core_reset(core_t *, bool), core_apply(core_t *,
 * ...)), so the C functions here are core_wasm_*() (Module._core_wasm_init,
 * ...) and web/core/index.js presents them under the design's names. */
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

/* The one model and the one core. */
static mmu_model_t s_model;
static core_t *s_core;

/* mmu_model.c logs a group edit through actions.c, which is the display's
 * own (it feeds the console and the Moonraker sender). The web host sends
 * what the user picks itself, from core_action(). */
void actions_log(const char *fmt, ...)
{
    (void)fmt;
}

/* The longest resolved action line / mapped script. */
#define LINE_MAX_LEN   256
#define SCRIPT_MAX_LEN 512

/* Drain the alerts the appliers queued into the model's history, the way the
 * simulator does after every apply (the model is its own "live" copy). */
static void drain(void)
{
    mmu_model_merge_live(&s_model, &s_model);
}

/** Set the model's clock in milliseconds (the host's Date.now() modulo 2^32):
 *  it stamps alerts as they are raised and decides when a read one leaves
 *  the toolhead's badge. Call before apply() and view(). */
EMSCRIPTEN_KEEPALIVE
void core_wasm_set_clock(unsigned int ms)
{
    s_model.now_ms = (uint32_t)ms;
}

/** Start over: a reset model and a fresh core. Safe to call again. */
EMSCRIPTEN_KEEPALIVE
void core_wasm_init(void)
{
    if(s_core) {
        core_destroy(s_core);
        s_core = NULL;
    }
    status_map_reset(&s_model);
    s_core = core_create(&s_model);
}

/** Drop every owned copy and reset the model (core_reset()), keeping the
 *  alert history when @p keep_alerts is non-zero. */
EMSCRIPTEN_KEEPALIVE
void core_wasm_reset(int keep_alerts)
{
    if(!s_core) core_wasm_init();
    if(!s_core) return;
    core_reset(s_core, keep_alerts != 0);
}

/**
 * Parse @p json and apply it as the object (@p kind, @p name).
 * Returns 1 when the core's copy changed, 0 when it did not (or the kind is
 * unknown), -1 when an argument is missing, @p json is not valid JSON, or
 * there was no memory.
 */
EMSCRIPTEN_KEEPALIVE
int core_wasm_apply(const char *kind, const char *name, const char *json)
{
    cJSON *obj;
    bool changed;

    if(!kind || !json) return -1;
    if(!s_core) core_wasm_init();
    if(!s_core) return -1;

    obj = cJSON_Parse(json);
    if(!obj) return -1;
    changed = core_apply(s_core, kind, (name && name[0]) ? name : NULL, obj);
    cJSON_Delete(obj);
    drain();
    return changed ? 1 : 0;
}

/** The panel tree as compact JSON (view_json()); NULL on no memory. */
EMSCRIPTEN_KEEPALIVE
char *core_wasm_view(void)
{
    if(!s_core) core_wasm_init();
    return view_json(&s_model);
}

/* Replace every {key} in @p line with the form's value for key, into @p out.
 * Returns NULL and fills @p why when a value is missing or the result does
 * not fit. A '{' with no closing '}' is copied as it is. */
static bool fill_line(const char *line, const cJSON *form, char *out, size_t out_len,
                      const char **why, char *why_buf, size_t why_len)
{
    size_t o = 0;
    const char *p = line;

    while(*p) {
        const char *end;
        char key[32];
        size_t klen;
        const cJSON *v;
        char num[32];
        const char *val;
        size_t vlen;

        if(*p != '{' || !(end = strchr(p, '}'))) {
            if(o + 1 >= out_len) goto too_long;
            out[o++] = *p++;
            continue;
        }
        klen = (size_t)(end - p - 1);
        if(klen == 0 || klen >= sizeof key) {
            if(o + 1 >= out_len) goto too_long;
            out[o++] = *p++;
            continue;
        }
        memcpy(key, p + 1, klen);
        key[klen] = '\0';

        v = form ? cJSON_GetObjectItemCaseSensitive(form, key) : NULL;
        if(cJSON_IsString(v) && v->valuestring) {
            val = v->valuestring;
        }
        else if(cJSON_IsNumber(v)) {
            double d = v->valuedouble;
            if(d == (double)(long)d) snprintf(num, sizeof num, "%ld", (long)d);
            else snprintf(num, sizeof num, "%g", d);
            val = num;
        }
        else if(cJSON_IsBool(v)) {
            val = cJSON_IsTrue(v) ? "1" : "0";
        }
        else {
            vs_format(VS_REASON_FORM_MISSING, why_buf, why_len, (vs_arg_t[]){ VS_ARG_T("key", key) }, 1);
            *why = why_buf;
            return false;
        }
        vlen = strlen(val);
        if(o + vlen >= out_len) goto too_long;
        memcpy(out + o, val, vlen);
        o += vlen;
        p = end + 1;
    }
    out[o] = '\0';
    return true;

too_long:
    *why = view_str(VS_REASON_LINE_TOO_LONG);
    return false;
}

static char *print_and_free(cJSON *o)
{
    char *text = o ? cJSON_PrintUnformatted(o) : NULL;
    cJSON_Delete(o);
    return text;
}

static char *error_json(const char *reason)
{
    cJSON *o = cJSON_CreateObject();

    if(!o) return NULL;
    cJSON_AddStringToObject(o, "kind", "error");
    cJSON_AddStringToObject(o, "reason", reason ? reason : "");
    return print_and_free(o);
}

/**
 * Resolve @p line (the form values fill its {placeholders}) against the
 * model and say where it goes, as JSON:
 *   {"kind":"gcode","script":"..."}           G-code lines joined by '\n'
 *   {"kind":"rpc","method":"...","params":{}} a Moonraker RPC call
 *   {"kind":"error","reason":"..."}           a host line the model cannot
 *                                             address (actions_map's -1) or a
 *                                             form value that is missing
 *   {"kind":"local"}                          a display-only line: nothing to
 *                                             send
 * @p form_json is an object of field id -> value, or NULL / "" for none.
 * NULL on no memory.
 */
EMSCRIPTEN_KEEPALIVE
char *core_wasm_action(const char *line, const char *form_json)
{
    char resolved[LINE_MAX_LEN];
    char script[SCRIPT_MAX_LEN];
    char method[64];
    char why_buf[160];
    const char *why = NULL;
    cJSON *form = NULL;
    cJSON *params = NULL;
    cJSON *o;
    char *text;
    int rc;

    if(!line) return error_json("no action line");
    if(!s_core) core_wasm_init();

    if(form_json && form_json[0]) {
        form = cJSON_Parse(form_json);
        if(!form) return error_json("form is not valid JSON");
    }
    if(!fill_line(line, form, resolved, sizeof resolved, &why, why_buf, sizeof why_buf)) {
        cJSON_Delete(form);
        return error_json(why);
    }
    cJSON_Delete(form);

    script[0] = '\0';
    rc = actions_map(resolved, &s_model, script, sizeof script);
    if(rc == 1) {
        o = cJSON_CreateObject();
        if(!o) return NULL;
        cJSON_AddStringToObject(o, "kind", "gcode");
        cJSON_AddStringToObject(o, "script", script);
        return print_and_free(o);
    }
    if(rc < 0) return error_json(script);

    method[0] = '\0';
    rc = actions_rpc_why(resolved, &s_model, method, sizeof method, &params, &why);
    if(rc < 0) return error_json(why);
    if(rc == 1) {
        o = cJSON_CreateObject();
        if(!o) {
            cJSON_Delete(params);
            return NULL;
        }
        cJSON_AddStringToObject(o, "kind", "rpc");
        cJSON_AddStringToObject(o, "method", method);
        cJSON_AddItemToObject(o, "params", params ? params : cJSON_CreateObject());
        return print_and_free(o);
    }

    text = strdup("{\"kind\":\"local\"}");
    return text;
}

/** Show the view's text in @p code ("en", "qps"); every later view() uses it.
 *  Returns 1 when the language exists and was set, 0 when it does not (the
 *  current one is kept), -1 when @p code is missing. */
EMSCRIPTEN_KEEPALIVE
int core_wasm_set_language(const char *code)
{
    if(!s_core) core_wasm_init();
    if(!code) return -1;
    return core_set_language(s_core, code) ? 1 : 0;
}

/** Free a string returned by core_view() or core_action(). */
EMSCRIPTEN_KEEPALIVE
void core_wasm_free(char *p)
{
    free(p);
}
