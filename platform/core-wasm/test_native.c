/**
 * @file test_native.c
 * The wasm shim (core_wasm.c) compiled natively, checked against the one
 * thing the browser cannot show: that nothing on its path reads or writes the
 * g_mmu singleton. The shim's model is its own (s_model); g_mmu is still
 * linked in (mmu_model.c defines it), so this test fills it with something
 * else on purpose and replays every recorded fixture through the shim:
 *
 *   - g_mmu poisoned with 0xA5 bytes,
 *   - g_mmu holding each of the display's demo scenarios in turn,
 *
 * and each time the view must equal the golden and g_mmu must be untouched.
 * Any function that reaches g_mmu shows up as a diff or a crash here.
 *
 *   bash platform/core-wasm/test_native.sh
 */
#undef NDEBUG

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/cjson/cJSON.h"
#include "model/mmu_model.h"

#ifndef FIXTURE_DIR
#error "compile with -DFIXTURE_DIR=<abs path to tests/fixtures/recorded>"
#endif

/* the shim (core_wasm.c) */
void core_wasm_init(void);
void core_wasm_reset(int keep_alerts);
int core_wasm_apply(const char *kind, const char *name, const char *json);
char *core_wasm_view(void);
char *core_wasm_action(const char *line, const char *form_json);
void core_wasm_free(char *p);

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    long len;
    char *text;

    if(!f) {
        fprintf(stderr, "test_native: cannot open %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = malloc((size_t)len + 1);
    assert(text && fread(text, 1, (size_t)len, f) == (size_t)len);
    text[len] = '\0';
    fclose(f);
    return text;
}

static cJSON *load(const char *name, const char *suffix)
{
    char path[512];
    char *text;
    cJSON *j;

    snprintf(path, sizeof path, "%s/%s%s", FIXTURE_DIR, name, suffix);
    text = slurp(path);
    j = cJSON_Parse(text);
    free(text);
    assert(j);
    return j;
}

/* Replay one fixture through the shim the way web/core/test/helpers.mjs does. */
static void replay(const cJSON *fixture)
{
    const cJSON *events = cJSON_GetObjectItemCaseSensitive(fixture, "events");
    const cJSON *ev;

    cJSON_ArrayForEach(ev, events) {
        const cJSON *kind = cJSON_GetObjectItemCaseSensitive(ev, "kind");
        const cJSON *unit = cJSON_GetObjectItemCaseSensitive(ev, "unit");
        const cJSON *payload = cJSON_GetObjectItemCaseSensitive(ev, "payload");
        const char *k;
        char *json;

        if(!cJSON_IsString(kind) || !payload) continue;
        k = strcmp(kind->valuestring, "c2_status") == 0 ? "oams_manager" : kind->valuestring;
        json = cJSON_PrintUnformatted(payload);
        assert(core_wasm_apply(k, cJSON_IsString(unit) ? unit->valuestring : NULL, json) >= 0);
        free(json);
    }
}

static const char *const k_fixtures[] = {
    "ams1_unverified", "idle", "loaded", "loading", "positioning", "two_lanes",
};

/* One pass over every fixture with g_mmu in whatever state the caller set. */
static void pass(const char *label)
{
    static mmu_model_t snapshot;
    unsigned i;

    memcpy(&snapshot, &g_mmu, sizeof snapshot);
    for(i = 0; i < sizeof k_fixtures / sizeof k_fixtures[0]; i++) {
        cJSON *fixture = load(k_fixtures[i], ".json");
        cJSON *golden = load(k_fixtures[i], ".view.json");
        char *text;
        cJSON *got;

        core_wasm_reset(0);
        replay(fixture);
        text = core_wasm_view();
        assert(text);
        got = cJSON_Parse(text);
        core_wasm_free(text);
        assert(got);
        if(!cJSON_Compare(got, golden, 1)) {
            fprintf(stderr, "test_native: %s / %s: the view differs from its golden\n",
                    label, k_fixtures[i]);
            exit(1);
        }
        cJSON_Delete(got);
        cJSON_Delete(golden);
        cJSON_Delete(fixture);
    }

    /* an action resolves against the shim's model, not g_mmu */
    {
        char *a = core_wasm_action("unload fps1", NULL);
        assert(a && strstr(a, "\"gcode\"") && strstr(a, "OPENAMS_UNLOAD FPS=fps1"));
        core_wasm_free(a);
    }

    if(memcmp(&snapshot, &g_mmu, sizeof snapshot) != 0) {
        fprintf(stderr, "test_native: %s: g_mmu was written\n", label);
        exit(1);
    }
}

int main(void)
{
    int n = mmu_model_scenario_count();
    int i;

    core_wasm_init();

    memset(&g_mmu, 0xA5, sizeof g_mmu);
    pass("g_mmu poisoned");

    for(i = 0; i < n; i++) {
        const char *name = mmu_scenario_names[i];

        mmu_model_set_scenario(name);
        pass(name);
        printf("test_native: g_mmu = %s: ok\n", name);
    }

    printf("test_native: ok (%d demo scenarios and a poisoned g_mmu)\n", n);
    return 0;
}
