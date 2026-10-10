/**
 * @file test_actions_map.c
 * The display's action -> G-code table, one row at a time.
 *
 * Plain C, plain assert, no framework. The model is built from a fixture the
 * way --replay builds it (status_map over the recorded events), so the slot
 * ids, the lane and the host ids the mapper resolves are the recorded ones.
 */

/* The default CMAKE_BUILD_TYPE here is Release, which puts -DNDEBUG on the
 * command line and would compile every assert() in this file away: the CTest
 * run would then pass without checking anything. Keep them live. */
#undef NDEBUG

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend/actions_map.h"
#include "view/strings.h"
#include "backend/status_map.h"
#include "lib/cjson/cJSON.h"
#include "model/mmu_model.h"

#ifndef ACTIONS_MAP_FIXTURE_DIR
#error "compile this test with -DACTIONS_MAP_FIXTURE_DIR=<abs path to tests/fixtures/recorded>"
#endif

/* mmu_model.c logs group edits through actions.c; the test does not care. */
void actions_log(const char *fmt, ...)
{
    (void)fmt;
}

/* ---------------------------------------------------------------- the model */

/* The fixtures carry the host's own action lists (`bays[i].a`, `act`, `ready`),
 * which say what is allowed in the state the fixture was recorded in. Most of
 * these tests move the model through other states by hand and are about the
 * mapper's own predicates, so build() drops the lists unless a test asks to
 * keep them (the capability tests below). */
static bool s_keep_host_lists = true;
static int s_event_limit = -1;   /* apply only this many events; -1 = all */

static void forget_host_lists(mmu_model_t *m)
{
    unsigned i, j;

    for(i = 0; i < m->unit_count; i++) {
        m->units[i].ready_known = false;
        m->units[i].act_known = false;
        m->units[i].dry_unlisted = false;
        m->units[i].cannot_dry_reason[0] = '\0';
        for(j = 0; j < MMU_MAX_SLOTS_PER_UNIT; j++) m->units[i].slots[j].acts_known = false;
    }
}

/* Apply every event of tests/fixtures/recorded/<name>.json onto a reset model. */
static void build(const char *name)
{
    char path[512];
    FILE *f;
    long len;
    char *text;
    cJSON *root, *events, *ev;

    snprintf(path, sizeof path, "%s/%s.json", ACTIONS_MAP_FIXTURE_DIR, name);
    f = fopen(path, "rb");
    if(!f) {
        fprintf(stderr, "test_actions_map: cannot open %s\n", path);
        exit(1);
    }
    if(fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "test_actions_map: cannot size %s\n", path);
        exit(1);
    }
    text = malloc((size_t)len + 1);
    assert(text);
    if(fread(text, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "test_actions_map: short read on %s\n", path);
        exit(1);
    }
    text[len] = '\0';
    fclose(f);

    root = cJSON_Parse(text);
    free(text);
    if(!root) {
        fprintf(stderr, "test_actions_map: %s is not valid JSON\n", path);
        exit(1);
    }

    events = cJSON_GetObjectItemCaseSensitive(root, "events");
    assert(cJSON_IsArray(events));

    status_map_reset(&g_mmu);
    cJSON_ArrayForEach(ev, events) {
        if(s_event_limit == 0) break;
        if(s_event_limit > 0) s_event_limit--;
        assert(status_map_apply_fixture_event(&g_mmu, ev));
    }
    s_event_limit = -1;

    cJSON_Delete(root);
    if(!s_keep_host_lists) forget_host_lists(&g_mmu);
}

/* ------------------------------------------------------------------- checks */

static void expect(const char *line, const char *want)
{
    char out[256];
    int rc;

    out[0] = '\0';
    rc = actions_map(line, &g_mmu, out, sizeof out);
    if(rc != 1 || strcmp(out, want) != 0) {
        fprintf(stderr, "test_actions_map: \"%s\" -> %d \"%s\", want 1 \"%s\"\n",
                line, rc, out, want);
    }
    assert(rc == 1);
    assert(strcmp(out, want) == 0);
}

/* A local line: nothing reaches the host and nothing is written. */
static void expect_local(const char *line)
{
    char out[256];
    int rc;

    out[0] = '\0';
    rc = actions_map(line, &g_mmu, out, sizeof out);
    if(rc != 0) fprintf(stderr, "test_actions_map: \"%s\" -> %d, want 0\n", line, rc);
    assert(rc == 0);
    assert(out[0] == '\0');
}

/* A host line the model cannot address: -1, and the reason in @p out. */
static void expect_unaddressable(const char *line, const char *why)
{
    char out[256];
    int rc;

    rc = actions_map(line, &g_mmu, out, sizeof out);
    if(rc != -1 || strcmp(out, why) != 0) {
        fprintf(stderr, "test_actions_map: \"%s\" -> %d \"%s\", want -1 \"%s\"\n",
                line, rc, out, why);
    }
    assert(rc == -1);
    assert(strcmp(out, why) == 0);
}

/* -------------------------------------------------------------------- tests */

static void test_model_is_the_fixture(void)
{
    build("loaded");

    /* tests/fixtures/recorded/loaded.json: one unit oams1, host idx 1, lane fps1,
     * four bays 0..3, T0 = bay 0 and T1 = bay 1. */
    assert(g_mmu.unit_count == 1);
    assert(strcmp(g_mmu.units[0].name, "oams1") == 0);
    assert(g_mmu.units[0].host_idx == 1);
    assert(strcmp(g_mmu.units[0].lane, "fps1") == 0);
    assert(g_mmu.units[0].slot_count == 4);
    assert(g_mmu.units[0].slots[0].global_id == 0);

    /* the display's slot ids: name + (bay + 1) */
    {
        char id[32];
        mmu_slot_id(0, 0, id, sizeof id);
        assert(strcmp(id, "oams11") == 0);
        mmu_slot_id(0, 3, id, sizeof id);
        assert(strcmp(id, "oams14") == 0);
    }

    assert(mmu_slot_group(0, 0) == 0);   /* T0 */
    assert(mmu_slot_group(0, 1) == 1);   /* T1 */
    assert(mmu_slot_group(0, 2) == -1);  /* in no group */

    /* the fixture ends LOADED: nothing is moving */
    assert(g_mmu.busy == MMU_BUSY_NONE);
    assert(g_mmu.busy_unit < 0);
}

static void test_load_unload(void)
{
    /* the one predicate: what the bay's state allows, with a reason for the user */
    expect_unaddressable("load oams11", "This spool is already loaded");
    /* the host's own list for the loaded lane omits `l` on the ready bay: the
     * state predicates pass, so the list is what refuses it */
    expect_unaddressable("load oams12", "Not available right now");
    expect("unload oams11", "OPENAMS_UNLOAD FPS=fps1");
    expect_unaddressable("unload oams14", "This spool is not loaded");
    expect_unaddressable("load oams13", "No spool is inserted");
    expect_unaddressable("pause and load oams12", "Not available right now");

    /* from here the tests move the model through states the recorded lists do
     * not describe: they are about the mapper's own predicates */
    forget_host_lists(&g_mmu);
    expect("load oams12", "OPENAMS_LOAD GROUP=T1 SLOT=1");
    expect("pause and load oams12", "PAUSE\nOPENAMS_LOAD GROUP=T1 SLOT=1");

    /* a spool in a bay of no group cannot be loaded (or unassigned) */
    g_mmu.units[0].slots[2].state = MMU_SLOT_READY;
    expect_unaddressable("load oams13", "Assign this spool to a tool first");
    g_mmu.units[0].slots[2].state = MMU_SLOT_EMPTY;
    expect_unaddressable("unassign oams13", "Assign this spool to a tool first");

    /* the toolhead busy with this very bay */
    g_mmu.toolheads[0].busy = MMU_BUSY_LOAD;
    g_mmu.toolheads[0].busy_unit = 0;
    g_mmu.toolheads[0].busy_slot = 1;
    expect_unaddressable("load oams12", "The toolhead is busy with this spool");
    expect("unload oams11", "OPENAMS_UNLOAD FPS=fps1");           /* another bay: unaffected */
    g_mmu.toolheads[0].busy_slot = 0;
    expect_unaddressable("unload oams11", "The toolhead is busy with this spool");
    g_mmu.toolheads[0].busy = MMU_BUSY_NONE;
}

static void test_stop(void)
{
    expect_unaddressable("stop", "Nothing is loading or unloading");

    g_mmu.busy = MMU_BUSY_LOAD;
    g_mmu.busy_unit = 0;
    expect("stop", "OAMSM_LOAD_FILAMENT_CANCEL FPS=fps1");

    /* `stop drying` is the alert button, not a cancel */
    expect_local("stop drying");

    g_mmu.busy = MMU_BUSY_NONE;
    g_mmu.busy_unit = -1;
}

/* U1c: the panel addresses a toolhead directly, by its own id (the FPS
 * lane), instead of a bay - several toolheads can be busy independently. */
static void test_toolhead_targeted(void)
{
    /* tests/fixtures/recorded/loaded.json's one lane is "fps1" (oams1's own). */
    assert(g_mmu.toolhead_count >= 1);
    assert(strcmp(g_mmu.toolheads[0].id, "fps1") == 0);

    expect("unload fps1", "OPENAMS_UNLOAD FPS=fps1");
    g_mmu.toolheads[0].busy = MMU_BUSY_LOAD;     /* poked: the vector export skips these lines */
    g_mmu.toolheads[0].loaded = false;
    expect_unaddressable("unload fps1", "No filament is loaded");
    g_mmu.toolheads[0].loaded = true;
    g_mmu.toolheads[0].busy = MMU_BUSY_NONE;

    expect_unaddressable("stop fps1", "Nothing is loading or unloading");
    g_mmu.toolheads[0].busy = MMU_BUSY_LOAD;
    expect("stop fps1", "OAMSM_LOAD_FILAMENT_CANCEL FPS=fps1");
    g_mmu.toolheads[0].busy = MMU_BUSY_NONE;

    /* a toolhead id nothing names is local, not a crash */
    expect_local("unload fps9");
    expect_local("stop fps9");
}

/* UNIFIED_UI 4g: Stop stops the whole sequence. It is enabled while the lane
 * is busy OR its stage plan runs (heat, home, cut: the lane is not busy yet),
 * and with the host's own stop the line is the RPC, not G-code. */
static void expect_rpc_stop(const char *line, const char *lane)
{
    char method[64];
    cJSON *params = NULL;
    const cJSON *l;

    assert(actions_rpc(line, &g_mmu, method, sizeof method, &params) == 1);
    assert(strcmp(method, "server.openams.stop") == 0);
    l = cJSON_GetObjectItemCaseSensitive(params, "lane");
    assert(cJSON_IsString(l) && strcmp(l->valuestring, lane) == 0);
    cJSON_Delete(params);
}

static void test_stop_whole_sequence(void)
{
    mmu_toolhead_t *t = &g_mmu.toolheads[0];
    char method[64];
    cJSON *params = NULL;
    const char *why = "";

    /* a heat step: the lane is not busy, the plan runs */
    t->busy = MMU_BUSY_NONE;
    t->step_count = 3;
    t->step_current = 0;
    t->step_failed = -1;
    expect("stop fps1", "OAMSM_LOAD_FILAMENT_CANCEL FPS=fps1");
    /* a failed step, or a plan past its last step, is no longer running */
    t->step_failed = 1;
    expect_unaddressable("stop fps1", "Nothing is loading or unloading");
    t->step_failed = -1;
    t->step_current = 3;
    expect_unaddressable("stop fps1", "Nothing is loading or unloading");
    t->step_current = -1;
    expect_unaddressable("stop fps1", "Nothing is loading or unloading");

    /* the bare line follows the display's own lane's plan (the legacy mirror) */
    g_mmu.step_count = 3;
    g_mmu.step_current = 1;
    g_mmu.step_failed = -1;
    expect("stop", "OAMSM_LOAD_FILAMENT_CANCEL FPS=fps1");
    g_mmu.step_count = 0;
    g_mmu.step_current = -1;
    expect_unaddressable("stop", "Nothing is loading or unloading");

    /* with the host's own stop: the RPC, and no G-code for the line */
    g_mmu.spoolman_stop = true;
    t->step_count = 3;
    t->step_current = 0;
    assert(actions_check("stop fps1", &g_mmu, &why) == 1);
    expect_local("stop fps1");                   /* not G-code any more */
    expect_rpc_stop("stop fps1", "fps1");
    t->step_current = -1;
    assert(actions_check("stop fps1", &g_mmu, &why) == 0);
    assert(strcmp(why, "Nothing is loading or unloading") == 0);
    assert(actions_rpc_why("stop fps1", &g_mmu, method, sizeof method, &params, &why) == -1);
    t->busy = MMU_BUSY_LOAD;
    assert(actions_check("stop fps1", &g_mmu, &why) == 1);
    expect_rpc_stop("stop fps1", "fps1");
    g_mmu.busy = MMU_BUSY_LOAD;
    g_mmu.busy_unit = 0;
    expect_rpc_stop("stop", "fps1");
    /* `stop drying` is still the alert button's own line */
    expect_local("stop drying");
    assert(actions_rpc("stop drying", &g_mmu, method, sizeof method, &params) == 0);

    g_mmu.spoolman_stop = false;
    g_mmu.busy = MMU_BUSY_NONE;
    g_mmu.busy_unit = -1;
    t->busy = MMU_BUSY_NONE;
    t->step_count = 0;
}

/* U1c: a bay the host is actively positioning cannot be (re)loaded - the
 * view's "load" action on a positioning tile is disabled with this reason. */
static void test_load_while_positioning(void)
{
    g_mmu.units[0].slots[1].positioning = true;
    expect_unaddressable("load oams12", "The spool is still being positioned");
    g_mmu.units[0].slots[1].positioning = false;

    /* still loadable once positioning clears */
    expect("load oams12", "OPENAMS_LOAD GROUP=T1 SLOT=1");
}

static void test_reread_calibrate(void)
{
    expect("reread oams11", "OAMS_RFID_SCAN OAMS=1");
    expect("reread oams12", "OAMS_RFID_SCAN OAMS=1");
    expect_unaddressable("reread oams13", "No spool is inserted");
    expect("calibrate oams11", "OAMS_CALIBRATE_PTFE_LENGTH OAMS=1 SPOOL=0");
    expect("calibrate oams13", "OAMS_CALIBRATE_PTFE_LENGTH OAMS=1 SPOOL=2");
}

static void test_dry(void)
{
    expect("dry oams1 start 50C 4h", "OAMS_DRYER_START OAMS=1 TARGET=50 DURATION=14400");
    expect("dry oams1 start 60C 1h", "OAMS_DRYER_START OAMS=1 TARGET=60 DURATION=3600");
    expect_unaddressable("dry oams1 stop", "The dryer is not running");
    g_mmu.units[0].dry_state = MMU_DRY_HEATING;
    expect("dry oams1 stop", "OAMS_DRYER_STOP OAMS=1");
    g_mmu.units[0].dry_state = MMU_DRY_IDLE;

    /* only start and stop reach the host; the pending-value edits stay local */
    expect_local("dry oams1 target 50C");
    expect_local("dry oams1 hours 4h");
    expect_local("dry oams1 preset PLA");
    expect_local("dry oams9 stop");
}

static void test_setting(void)
{
    expect("setting read-on-insertion on",
           "OAMSM_SET_OPTION NAME=read_tag_on_insertion VALUE=1");
    expect("setting read-on-startup off",
           "OAMSM_SET_OPTION NAME=read_tags_at_startup VALUE=0");
    expect("setting read_tag_on_insertion on",
           "OAMSM_SET_OPTION NAME=read_tag_on_insertion VALUE=1");
    expect("setting auto_reload_on_runout off",
           "OAMSM_SET_OPTION NAME=auto_reload_on_runout VALUE=0");
    expect("setting auto_create_spoolman_spools on",
           "OAMSM_SET_OPTION NAME=auto_create_spoolman_spools VALUE=1");
    expect("setting apply_pa_on_load on",
           "OAMSM_SET_OPTION NAME=apply_pa_on_load VALUE=1");

    /* any other key is the display's own */
    expect_local("setting bogus-key on");
    expect_local("setting read-on-insertion maybe");
}

static void test_groups(void)
{
    expect("create group T2", "OAMSM_CREATE_GROUP GROUP=T2");
    /* the model's own spelling of the same tap (mmu_model.c) */
    expect("group create T3", "OAMSM_CREATE_GROUP GROUP=T3");

    expect("assign T1 oams12", "OAMSM_ASSIGN_BAY GROUP=T1 OAMS=oams1 BAY=1");
    expect("unassign oams11", "OAMSM_UNASSIGN_BAY GROUP=T0 OAMS=oams1 BAY=0");

    expect("assign spool oams12 12",
           "OAMSM_SET_BAY_SPOOL OAMS=oams1 BAY=1 SPOOL=12 SOURCE=manual");
    expect_unaddressable("unlink oams12", "No Spoolman spool is linked");
    g_mmu.units[0].slots[1].spool_id = 5;
    expect("unlink oams12", "OAMSM_CLEAR_BAY_SPOOL OAMS=oams1 BAY=1");
    g_mmu.units[0].slots[1].spool_id = -1;
}

/* The "Change group..." form's own line, and the delete the groups list
 * carries: every choice of the list maps to the command(s) it means. */
static void test_change_group(void)
{
    build("loaded");
    /* T0 = bay 0 and T1 = bay 1, T0 loaded: the next free name is T2. */

    /* a group's own index: the bay stays where it is, and the script says so */
    expect("change group 1 oams12", "OAMSM_ASSIGN_BAY GROUP=T1 OAMS=oams1 BAY=1");
    /* the loaded group is not a destination: one reason, one string */
    expect_unaddressable("change group 0 oams12", "This group is loaded in the toolhead");
    expect_unaddressable("change group 0 oams11", "This group is loaded in the toolhead");
    /* "No group" on a bay in one */
    expect("change group -1 oams12", "OAMSM_UNASSIGN_BAY GROUP=T1 OAMS=oams1 BAY=1");
    /* "No group" on a bay in none: the host has no group to name */
    expect_unaddressable("change group -1 oams13", "Assign this spool to a tool first");
    /* "New group": create it and fill it, in that order, as one script */
    expect("change group -2 oams12",
           "OAMSM_CREATE_GROUP GROUP=T2\nOAMSM_ASSIGN_BAY GROUP=T2 OAMS=oams1 BAY=1");

    /* a choice outside the table, and a bay the model does not hold, go to
     * nobody: nothing is sent rather than something wrong */
    expect_local("change group 7 oams12");
    expect_local("change group -3 oams12");
    expect_local("change group 1 oams99");
    expect_local("change group T1 oams12");

    /* the loaded group cannot be deleted either */
    expect_unaddressable("delete group T0", "This group is loaded in the toolhead");
    expect("delete group T1", "OAMSM_DELETE_GROUP GROUP=T1");
    expect("group delete T1", "OAMSM_DELETE_GROUP GROUP=T1");
    expect_local("delete group T9");

    g_mmu.busy = MMU_BUSY_LOAD;
    expect_unaddressable("change group 1 oams12", "The toolhead is busy");
    expect_unaddressable("delete group T1", "The toolhead is busy");
    g_mmu.busy = MMU_BUSY_NONE;

    g_mmu.runout_active = true;
    expect_unaddressable("change group 1 oams12", "The toolhead is handling a runout");
    expect_unaddressable("delete group T1", "The toolhead is handling a runout");
    g_mmu.runout_active = false;

    /* with the lane free again, both are sent as they stand */
    expect("change group 1 oams12", "OAMSM_ASSIGN_BAY GROUP=T1 OAMS=oams1 BAY=1");
    expect("delete group T1", "OAMSM_DELETE_GROUP GROUP=T1");
}

static void test_faults(void)
{
    expect("fault clear_errors", "OAMSM_CLEAR_ERRORS");
    expect("fault clear_fault oams1", "OAMS_CLEAR_FAULT OAMS=1");
    expect("fault retry_load T1", "OAMSM_LOAD_FILAMENT GROUP=T1");
    expect("fault resume", "RESUME");
    expect("resume", "RESUME");
}

static void test_local_vocabulary(void)
{
    static const char *const k_local[] = {
        "display home", "open notice", "open active error", "alerts", "screens",
        "back home", "print status", "load status", "bind display to oams1",
        "side front", "rotate cw", "edit T0", "change group T1",
        "confirm mapping: keep", "  T0 -> oams11", "clear resolved alerts",
        "rescan units", "retry", "finished continue", "ok", "stop drying",
        "filament extruded, continue", "not extruded yet, retry",
        "retry (problem solved)",
    };
    unsigned i;

    for(i = 0; i < sizeof k_local / sizeof k_local[0]; i++) expect_local(k_local[i]);
}

static void test_unknown_and_unresolvable(void)
{
    /* unknown lines are local, including a slot the model does not hold */
    expect_local("frobnicate");
    expect_local("");
    expect_local("load");
    expect_local("load oams99");
    expect_local("unload oams99");
    expect_local("assign T1 oams99");
    expect_local("fault clear_fault oams9");

    /* a line and a model are both required */
    {
        char out[64];
        assert(actions_map(NULL, &g_mmu, out, sizeof out) == 0);
        assert(actions_map("load oams11", NULL, out, sizeof out) == 0);
    }
}

static void test_small_out(void)
{
    forget_host_lists(&g_mmu);   /* buffer sizes, not the host's lists */
    struct { char buf[8]; unsigned char canary; } tiny;
    int rc;

    tiny.canary = 0x7f;
    rc = actions_map("load oams12", &g_mmu, tiny.buf, sizeof tiny.buf);
    assert(rc == -1);
    assert(tiny.canary == 0x7f);   /* never wrote past out_len */

    tiny.canary = 0x7f;
    rc = actions_map("load oams12", &g_mmu, tiny.buf, 0);
    assert(rc == -1);
    assert(tiny.canary == 0x7f);

    /* no room to write at all is still -1, not a crash */
    assert(actions_map("load oams12", &g_mmu, NULL, 0) == -1);

    /* exactly enough room is enough */
    {
        char exact[sizeof "OPENAMS_LOAD GROUP=T1 SLOT=1"];
        rc = actions_map("load oams12", &g_mmu, exact, sizeof exact);
        assert(rc == 1);
        assert(strcmp(exact, "OPENAMS_LOAD GROUP=T1 SLOT=1") == 0);
    }
}

/* --------------------------------------------------------- actions_rpc tests */

static void test_rpc_spool_list(void)
{
    char method[64];
    cJSON *params = NULL;
    const cJSON *rm, *path;
    int rc;

    rc = actions_rpc("spool list", &g_mmu, method, sizeof method, &params);
    assert(rc == 1);
    assert(strcmp(method, "server.spoolman.proxy") == 0);
    assert(params != NULL);
    rm   = cJSON_GetObjectItemCaseSensitive(params, "request_method");
    path = cJSON_GetObjectItemCaseSensitive(params, "path");
    assert(cJSON_IsString(rm)   && strcmp(rm->valuestring,   "GET")       == 0);
    assert(cJSON_IsString(path) && strcmp(path->valuestring, "/v1/spool") == 0);
    cJSON_Delete(params);
}

static void test_rpc_spool_confirm(void)
{
    char method[64];
    cJSON *params = NULL;
    const cJSON *bay, *rem;
    int rc;

    /* oams11 = unit oams1 bay 0 */
    rc = actions_rpc("spool confirm oams11 750", &g_mmu, method, sizeof method, &params);
    assert(rc == 1);
    assert(strcmp(method, "server.openams_spoolman.confirm") == 0);
    assert(params != NULL);
    bay = cJSON_GetObjectItemCaseSensitive(params, "bay");
    rem = cJSON_GetObjectItemCaseSensitive(params, "remaining_g");
    assert(cJSON_IsString(bay) && strcmp(bay->valuestring, "oams1-0") == 0);
    assert(cJSON_IsNumber(rem) && (int)rem->valuedouble == 750);
    cJSON_Delete(params);
}

static void test_rpc_unknown_slot(void)
{
    char method[64];
    cJSON *params = NULL;
    int rc;

    rc = actions_rpc("spool confirm oams99 500", &g_mmu, method, sizeof method, &params);
    assert(rc == 0);
    assert(params == NULL);
}

static void test_rpc_not_rpc(void)
{
    char method[64];
    cJSON *params = NULL;
    int rc;

    /* "load" is a G-code line, not an RPC */
    rc = actions_rpc("load oams11", &g_mmu, method, sizeof method, &params);
    assert(rc == 0);
    assert(params == NULL);
}

static void test_rpc_vendor_list(void)
{
    char method[64];
    cJSON *params = NULL;
    const cJSON *rm, *path;

    assert(actions_rpc("vendor list", &g_mmu, method, sizeof method, &params) == 1);
    assert(strcmp(method, "server.spoolman.proxy") == 0);
    rm   = cJSON_GetObjectItemCaseSensitive(params, "request_method");
    path = cJSON_GetObjectItemCaseSensitive(params, "path");
    assert(cJSON_IsString(rm)   && strcmp(rm->valuestring,   "GET")         == 0);
    assert(cJSON_IsString(path) && strcmp(path->valuestring, "/v1/vendor") == 0);
    cJSON_Delete(params);
}

/* `spool edit`: the model the tests poke is oams1, bay 0 linked (Spoolman spool
 * 12: PETG, Polymaker, blue, 430 g of 43 %), bay 1 a spool with no record, bay 2
 * empty. */
static const char *edit_why;

static int edit_rpc(const char *line, cJSON **params)
{
    char method[64];
    int rc;

    *params = NULL;
    edit_why = NULL;
    rc = actions_rpc_why(line, &g_mmu, method, sizeof method, params, &edit_why);
    if(rc == 1) assert(strcmp(method, "server.openams_spoolman.edit") == 0);
    return rc;
}

static void edit_ok(const char *line, const char *want_json)
{
    cJSON *params, *want;

    assert(edit_rpc(line, &params) == 1);
    want = cJSON_Parse(want_json);
    assert(want);
    if(!cJSON_Compare(params, want, true)) {
        char *got = cJSON_PrintUnformatted(params);

        fprintf(stderr, "test_actions_map: \"%s\" -> %s, want %s\n", line, got, want_json);
    }
    assert(cJSON_Compare(params, want, true));
    cJSON_Delete(params);
    cJSON_Delete(want);
}

static void edit_refused(const char *line, view_str_id_t why)
{
    cJSON *params;

    assert(edit_rpc(line, &params) == -1);
    assert(params == NULL);
    assert(edit_why == view_str(why));
}

static void test_rpc_spool_edit(void)
{
    mmu_slot_t *s0, *s1, *s2;
    cJSON *params;
    cJSON *arr;

    build("loaded");
    g_mmu.spoolman_online = true;
    g_mmu.spoolman_edit = true;
    arr = cJSON_Parse("[{\"name\":\"Polymaker\"},{\"name\":\"bambu lab\"},{\"name\":\"eSUN\"}]");
    assert(arr && status_map_apply_vendor_list(&g_mmu, arr));
    cJSON_Delete(arr);

    s0 = &g_mmu.units[0].slots[0];
    s1 = &g_mmu.units[0].slots[1];
    s2 = &g_mmu.units[0].slots[2];
    s0->state = MMU_SLOT_READY; s0->spool_id = 12; s0->color = 0x0A2989; s0->color_known = true;
    snprintf(s0->material, sizeof s0->material, "PETG");
    snprintf(s0->brand, sizeof s0->brand, "Polymaker");
    s0->remaining_g = 430; s0->remaining_pct = 43;
    s1->state = MMU_SLOT_READY; s1->spool_id = -1; s1->color_known = false;
    s1->material[0] = '\0'; s1->brand[0] = '\0'; s1->remaining_g = -1; s1->remaining_pct = -1;
    s2->state = MMU_SLOT_EMPTY;

    /* only a change is sent; Generic leads, then the host's vendors sorted, so
     * bambu lab is 1, eSUN 2 and Polymaker 3 (the curated brands follow) */
    edit_ok("spool edit oams11 color=255", "{\"unit\":\"oams1\",\"bay\":0,\"color_hex\":\"0000FF\"}");
    edit_ok("spool edit oams11 material=0", "{\"unit\":\"oams1\",\"bay\":0,\"material\":\"PLA\"}");
    edit_ok("spool edit oams11 vendor=1", "{\"unit\":\"oams1\",\"bay\":0,\"vendor\":\"bambu lab\"}");
    edit_ok("spool edit oams11 vendor=0", "{\"unit\":\"oams1\",\"bay\":0,\"vendor\":\"Generic\"}");
    edit_ok("spool edit oams11 remaining=300 initial=1200 pa=0.045",
            "{\"unit\":\"oams1\",\"bay\":0,\"remaining_weight\":300,\"initial_weight\":1200,\"flow_k\":0.045}");
    edit_ok("spool edit oams11 material=1 vendor=3 remaining=430 initial=1000 pa=0.02 color=255",
            "{\"unit\":\"oams1\",\"bay\":0,\"color_hex\":\"0000FF\"}");

    /* what the form starts from, or nothing at all, changes nothing */
    edit_refused("spool edit oams11 material=1 color=665993 vendor=3 remaining=430 initial=1000 pa=0.02",
                 VS_EDIT_NO_CHANGES);
    edit_refused("spool edit oams11 pa=0.020", VS_EDIT_NO_CHANGES);
    edit_refused("spool edit oams11", VS_EDIT_NO_CHANGES);

    /* a spool with no host record is added, with a material */
    edit_ok("spool edit oams12", "{\"unit\":\"oams1\",\"bay\":1,\"material\":\"PLA\"}");
    edit_ok("spool edit oams12 material=1 color=16711680 vendor=2 remaining=250",
            "{\"unit\":\"oams1\",\"bay\":1,\"material\":\"PETG\",\"color_hex\":\"FF0000\","
            "\"vendor\":\"eSUN\",\"remaining_weight\":250}");

    /* refusals, with the table's own words */
    edit_refused("spool edit oams11 remaining=-1", VS_REASON_EDIT_RANGE);
    edit_refused("spool edit oams11 initial=0", VS_REASON_EDIT_RANGE);
    edit_refused("spool edit oams11 pa=2.5", VS_REASON_EDIT_RANGE);
    edit_refused("spool edit oams11 pa=-0.1", VS_REASON_EDIT_RANGE);
    edit_refused("spool edit oams11 remaining=900 initial=800", VS_REASON_EDIT_EXCEEDS);
    edit_refused("spool edit oams11 material=99", VS_REASON_EDIT_STALE);
    edit_refused("spool edit oams11 vendor=32", VS_REASON_EDIT_STALE);  /* past the 30 names and the own */
    edit_refused("spool edit oams12 vendor=31", VS_REASON_EDIT_STALE);  /* no vendor of its own */
    edit_refused("spool edit oams13 color=1", VS_REASON_BAY_EMPTY);

    /* lines that are not a spool edit are not one */
    assert(edit_rpc("spool edit oams99 color=1", &params) == 0);
    assert(edit_rpc("spool edit oams11 bogus=1", &params) == 0);
    assert(edit_rpc("spool edit oams11 color=x", &params) == 0);
    assert(edit_rpc("spool edit oams11 pa=1e-2", &params) == 0);
    assert(edit_rpc("spool edit oams11 color=", &params) == 0);
    /* actions_rpc() sees a refusal as "not an RPC action" */
    {
        char method[64];

        assert(actions_rpc("spool edit oams11", &g_mmu, method, sizeof method, &params) == 0);
    }

    /* the predicate dims what the host cannot take, and never an empty submission */
    {
        const char *why = NULL;

        assert(actions_check("spool edit oams11 material=1 color=665993 vendor=3 remaining=430 initial=1000 pa=0.020",
                             &g_mmu, &why) == 1);
        g_mmu.spoolman_online = false;
        assert(actions_check("spool edit oams11", &g_mmu, &why) == 0);
        assert(why == view_str(VS_REASON_SPOOLMAN_OFFLINE));
        g_mmu.spoolman_edit = false;
        assert(actions_check("spool edit oams11", &g_mmu, &why) == 0);
        assert(why == view_str(VS_REASON_EDIT_NO_SPOOLMAN));
    }
}

/* ------------------------------------------------------------- actions_check */

static void test_actions_check(void)
{
    /* the reason comes back as a pointer into the string table (or the host's
     * own words), never as a copy, so a translation of any length survives */
    const char *why = NULL;

    forget_host_lists(&g_mmu);   /* the check's own plumbing, not the host's lists */
    assert(actions_check("load oams12", &g_mmu, &why) == 1);
    assert(why[0] == '\0');

    g_mmu.units[0].slots[1].positioning = true;
    assert(actions_check("load oams12", &g_mmu, &why) == 0);
    assert(strcmp(why, "The spool is still being positioned") == 0);
    /* it is the table's own string, not a copy of it */
    assert(why == view_str(VS_REASON_POSITIONING));
    g_mmu.units[0].slots[1].positioning = false;

    /* an RPC line the map does not touch at all */
    why = NULL;
    assert(actions_check("spool list", &g_mmu, &why) == 1);
    assert(why[0] == '\0');
}

/* The spool's own size and pressure advance, when the host publishes them, are
 * what the form starts from: "unchanged" means those, and the read-back weight
 * and Spoolman's 0.020 become changes like any other. */
static void test_rpc_spool_edit_own_weight_and_pa(void)
{
    mmu_slot_t *s0;
    cJSON *arr;

    build("loaded");
    g_mmu.spoolman_online = true;
    g_mmu.spoolman_edit = true;
    arr = cJSON_Parse("[{\"name\":\"Polymaker\"},{\"name\":\"bambu lab\"},{\"name\":\"eSUN\"}]");
    assert(arr && status_map_apply_vendor_list(&g_mmu, arr));
    cJSON_Delete(arr);

    s0 = &g_mmu.units[0].slots[0];
    s0->state = MMU_SLOT_READY; s0->spool_id = 12; s0->color = 0x0A2989; s0->color_known = true;
    snprintf(s0->material, sizeof s0->material, "PETG");
    snprintf(s0->brand, sizeof s0->brand, "Polymaker");
    s0->remaining_g = 640; s0->remaining_pct = 64; s0->initial_g = 2000; s0->flow_k_x1000 = 45;

    /* the form as it opens: the spool's own values, and nothing to save */
    edit_refused("spool edit oams11 material=1 color=665993 vendor=3 remaining=640 initial=2000 pa=0.045",
                 VS_EDIT_NO_CHANGES);
    /* what the estimate and the 0.020 default used to be is now a change */
    edit_ok("spool edit oams11 pa=0.02", "{\"unit\":\"oams1\",\"bay\":0,\"flow_k\":0.02}");
    edit_ok("spool edit oams11 initial=1000", "{\"unit\":\"oams1\",\"bay\":0,\"initial_weight\":1000}");
    edit_ok("spool edit oams11 remaining=1100 initial=2000",
            "{\"unit\":\"oams1\",\"bay\":0,\"remaining_weight\":1100}");

    /* an unknown PA is Spoolman's 0.020 again */
    s0->flow_k_x1000 = -1;
    edit_refused("spool edit oams11 pa=0.020", VS_EDIT_NO_CHANGES);
    edit_ok("spool edit oams11 pa=0.045", "{\"unit\":\"oams1\",\"bay\":0,\"flow_k\":0.045}");
}

/* -------------------------------------------------------------------- main */

/* `spool edit ... then load`: the edit RPC exactly as `spool edit`, plus the
 * line to send once the host has taken it; refused while the material is still
 * the form's unanswered -1. */
static void test_rpc_spool_edit_then_load(void)
{
    char method[64], then[160];
    const char *why = NULL;
    cJSON *params = NULL, *plain = NULL;
    const char *tail = "color=255 vendor=0 remaining=250 initial=1000 pa=0.020";
    char line[160];
    mmu_slot_t *s1;

    build("loaded");
    forget_host_lists(&g_mmu);   /* the follow-up load is the mapper's, the lists are tested apart */
    g_mmu.spoolman_online = true;
    g_mmu.spoolman_edit = true;
    s1 = &g_mmu.units[0].slots[1];
    s1->state = MMU_SLOT_READY; s1->spool_id = -1; s1->color_known = false;
    s1->material[0] = '\0'; s1->brand[0] = '\0'; s1->remaining_g = -1; s1->remaining_pct = -1;

    snprintf(line, sizeof line, "spool edit oams12 material=2 %s", tail);
    assert(actions_rpc_then(line, &g_mmu, method, sizeof method, &params,
                            then, sizeof then, &why) == 1);
    assert(strcmp(method, "server.openams_spoolman.edit") == 0);
    assert(strcmp(then, "") == 0);
    plain = params;

    snprintf(line, sizeof line, "spool edit oams12 material=2 %s then load", tail);
    params = NULL;
    assert(actions_rpc_then(line, &g_mmu, method, sizeof method, &params,
                            then, sizeof then, &why) == 1);
    assert(strcmp(method, "server.openams_spoolman.edit") == 0);
    assert(strcmp(then, "load oams12") == 0);
    assert(cJSON_Compare(params, plain, true));      /* the same call, nothing of "then" in it */
    cJSON_Delete(params);
    cJSON_Delete(plain);

    /* the wrapper without a place for it still maps the edit */
    edit_ok(line, "{\"unit\":\"oams1\",\"bay\":1,\"material\":\"ABS\",\"color_hex\":\"0000FF\","
                  "\"remaining_weight\":250}");

    /* the unanswered material: refused, in its own words, nothing to send */
    snprintf(line, sizeof line, "spool edit oams12 material=-1 %s then load", tail);
    then[0] = 'x';
    params = NULL;
    assert(actions_rpc_then(line, &g_mmu, method, sizeof method, &params,
                            then, sizeof then, &why) == -1);
    assert(params == NULL && why == view_str(VS_MATERIAL_CHOOSE));
    assert(then[0] == '\0');
    edit_refused(line, VS_MATERIAL_CHOOSE);
    /* and a bare edit never takes -1: that is the stale list it always was */
    snprintf(line, sizeof line, "spool edit oams12 material=-1 %s", tail);
    edit_refused(line, VS_REASON_EDIT_STALE);

    /* the edit's own refusals come first, as for any edit */
    g_mmu.spoolman_online = false;
    snprintf(line, sizeof line, "spool edit oams12 material=2 %s then load", tail);
    edit_refused(line, VS_REASON_SPOOLMAN_OFFLINE);
    g_mmu.spoolman_online = true;

    /* only exactly ` then load` is the suffix */
    snprintf(line, sizeof line, "spool edit oams12 material=2 %s then unload", tail);
    assert(edit_rpc(line, &params) == 0);
    snprintf(line, sizeof line, "spool edit oams12 material=2 %s then", tail);
    assert(edit_rpc(line, &params) == 0);
}

/* ------------------------------------------- what the host says a unit can do */

/* build() with the host's own lists kept (and, for the stepwise one, only the
 * first @p events events applied). */
static void build_host(const char *name, int events)
{
    s_keep_host_lists = true;
    s_event_limit = events;
    build(name);
    s_keep_host_lists = false;
}

/* Not exported to the action vectors (tools/export_actions_vectors.py reads
 * only the plain expect* calls, made against the "loaded" fixture): these run
 * on the capability fixtures, which web/core/test/capability.test.mjs replays. */
#define cap_ok expect
#define cap_refused expect_unaddressable

static void test_host_lists(void)
{
    const char *why = "";
    char line[160];

    /* full capabilities: everything the state allows goes out, as before */
    build_host("cap_full", -1);
    cap_ok("load full11", "OPENAMS_LOAD GROUP=T0 SLOT=0");
    cap_ok("calibrate full11", "OAMS_CALIBRATE_PTFE_LENGTH OAMS=1 SPOOL=0");
    cap_ok("reread full11", "OAMS_RFID_SCAN OAMS=1");
    cap_ok("dry full1 start 50C 4h", "OAMS_DRYER_START OAMS=1 TARGET=50 DURATION=14400");
    cap_ok("fault clear_fault full1", "OAMS_CLEAR_FAULT OAMS=1");
    cap_refused("dry full1 stop", "The dryer is not running");
    g_mmu.units[0].dry_state = MMU_DRY_HEATING;
    cap_ok("dry full1 stop", "OAMS_DRYER_STOP OAMS=1");
    assert(actions_dry_reason(&g_mmu.units[0])[0] == '\0');
    assert(!g_mmu.units[0].dry_unlisted && g_mmu.units[0].act_known && g_mmu.units[0].ready_known);

    /* a unit that only reports: ready is false and the lists are "ad" and [] */
    build_host("cap_monitor_only", -1);
    assert(g_mmu.units[0].ready_known && !g_mmu.units[0].ready);
    cap_refused("load mon11", "Not supported on this unit yet");
    cap_refused("load mon13", "Not supported on this unit yet");   /* an empty bay too */
    cap_refused("calibrate mon11", "Not supported on this unit yet");
    cap_refused("reread mon11", "Not supported on this unit yet");
    cap_refused("fault clear_fault mon1", "Not supported on this unit yet");
    cap_refused("dry mon1 start 50C 4h", "Not supported on this unit yet");
    g_mmu.units[0].slots[0].state = MMU_SLOT_LOADED;
    cap_refused("unload mon11", "Not supported on this unit yet");
    g_mmu.toolheads[0].loaded = true;
    g_mmu.toolheads[0].loaded_unit = 0;
    g_mmu.toolheads[0].loaded_slot = 0;
    cap_refused("unload fps1", "Not supported on this unit yet");
    assert(actions_check("load mon11", &g_mmu, &why) == 0);
    assert(strcmp(why, "Not supported on this unit yet") == 0);
    /* what the host does list stays reachable */
    cap_ok("unlink mon11", "OAMSM_CLEAR_BAY_SPOOL OAMS=mon1 BAY=0");

    /* the same unit, the host listing nothing for a bay but the unit ready: the
     * bay list alone withholds */
    build_host("cap_ams1", -1);
    assert(g_mmu.units[0].ready_known && g_mmu.units[0].ready);
    cap_ok("load am11", "OPENAMS_LOAD GROUP=T0 SLOT=0");
    cap_refused("calibrate am11", "Not available right now");
    cap_refused("calibrate am13", "No spool is inserted");        /* by state, when there is one */
    cap_refused("reread am11", "Not supported on this unit yet");  /* act has no rfid_scan */
    g_mmu.units[0].slots[0].state = MMU_SLOT_LOADED;
    cap_refused("calibrate am11", "This spool is already loaded");
    g_mmu.units[0].slots[0].state = MMU_SLOT_READY;
    g_mmu.toolheads[0].busy = MMU_BUSY_LOAD;
    g_mmu.toolheads[0].busy_unit = 0;
    g_mmu.toolheads[0].busy_slot = 0;
    cap_refused("calibrate am11", "The toolhead is busy with this spool");
    g_mmu.toolheads[0].busy_slot = 1;
    cap_refused("calibrate am11", "The toolhead is busy");
    g_mmu.toolheads[0].busy = MMU_BUSY_NONE;
    g_mmu.units[0].slots[1].positioning = true;
    cap_refused("calibrate am11", "The spool is still being positioned");

    /* a dryer the list does not offer: dryer_start missing, then the supply is the reason */
    build_host("cap_ams2_no_dryer_actions", -1);
    assert(g_mmu.units[0].can_dry && g_mmu.units[0].dry_unlisted);
    assert(strcmp(g_mmu.units[0].cannot_dry_reason, "Not supported on this unit yet") == 0);
    cap_refused("dry dry1 start 50C 4h", "Not supported on this unit yet");
    g_mmu.units[0].dry_state = MMU_DRY_HEATING;
    cap_refused("dry dry1 stop", "Not supported on this unit yet");
    cap_ok("reread dry11", "OAMS_RFID_SCAN OAMS=1");               /* rfid_scan is listed */
    cap_ok("fault clear_fault dry1", "OAMS_CLEAR_FAULT OAMS=1");    /* so is clear_fault */
    build_host("cap_ams2_dryer_no_adapter", -1);
    cap_refused("dry dry1 start 50C 4h", "The dryer's power adapter is not connected.");
    assert(strcmp(g_mmu.units[0].cannot_dry_reason, "The dryer's power adapter is not connected.") == 0);

    /* the dryer's stop alone is missing */
    build_host("cap_ams_ht_one_bay", -1);
    assert(!g_mmu.units[0].dry_unlisted);
    cap_ok("dry ht1 start 55C 4h", "OAMS_DRYER_START OAMS=1 TARGET=55 DURATION=14400");
    cap_refused("dry ht1 stop", "Not supported on this unit yet");

    /* offline, then online but not ready, then ready: each step on its own */
    build_host("cap_offline_then_online", 2);
    assert(!g_mmu.units[0].connected && g_mmu.units[0].ready_known && !g_mmu.units[0].ready);
    cap_refused("load off11", "Unit is not connected");           /* offline wins over not ready */
    build_host("cap_offline_then_online", 4);
    assert(g_mmu.units[0].connected && !g_mmu.units[0].ready);
    cap_refused("load off11", "Not supported on this unit yet");
    cap_refused("calibrate off11", "Not supported on this unit yet");
    build_host("cap_offline_then_online", -1);
    assert(g_mmu.units[0].connected && g_mmu.units[0].ready);
    cap_ok("load off11", "OPENAMS_LOAD GROUP=T0 SLOT=0");
    cap_ok("calibrate off11", "OAMS_CALIBRATE_PTFE_LENGTH OAMS=1 SPOOL=0");

    /* a stop fault on the lane is the reason, in the host's words, before not ready */
    build_host("cap_ams1", -1);
    g_mmu.toolheads[0].has_error = true;
    g_mmu.toolheads[0].error_severity = MMU_SEV_STOP;
    snprintf(g_mmu.toolheads[0].error_text, sizeof g_mmu.toolheads[0].error_text, "Hub jam");
    cap_refused("load am11", "Hub jam");
    g_mmu.units[0].ready_known = true;
    g_mmu.units[0].ready = false;
    cap_refused("calibrate am11", "Hub jam");
    g_mmu.toolheads[0].has_error = false;
    cap_refused("calibrate am11", "Not supported on this unit yet");

    /* a ready bay on an idle lane, uncalibrated, whose list offers calibrate but not load */
    build_host("cap_ams1", -1);
    g_mmu.units[0].slots[0].acts = MMU_BAY_ACT_CALIBRATE | MMU_BAY_ACT_ASSIGN;
    g_mmu.units[0].slots[0].cal_state = MMU_CAL_UNCALIBRATED;
    cap_refused("load am11", "Calibrate this bay first");
    g_mmu.units[0].slots[0].cal_state = MMU_CAL_CONFIGURED;
    cap_refused("load am11", "Not available right now");
    g_mmu.units[0].slots[0].cal_state = MMU_CAL_UNCALIBRATED;
    g_mmu.units[0].slots[0].acts = MMU_BAY_ACT_ASSIGN;
    cap_refused("load am11", "Not available right now");          /* calibrate not offered either */

    /* a host that sends neither list withholds nothing: every older fixture */
    s_keep_host_lists = false;
    build("loaded");
    s_keep_host_lists = true;
    assert(!g_mmu.units[0].ready_known && !g_mmu.units[0].act_known && !g_mmu.units[0].slots[0].acts_known);
    cap_ok("calibrate oams12", "OAMS_CALIBRATE_PTFE_LENGTH OAMS=1 SPOOL=1");

    /* a save that asks to load afterwards is refused when the load is withheld */
    {
        char method[64], then[64];
        cJSON *params = NULL;
        int rc;

        build_host("cap_monitor_only", -1);
        g_mmu.spoolman_edit = true;
        g_mmu.spoolman_online = true;
        snprintf(line, sizeof line, "spool edit mon11 remaining=500 then load");
        rc = actions_rpc_then(line, &g_mmu, method, sizeof method, &params, then, sizeof then, &why);
        assert(rc == -1);
        assert(strcmp(why, "Not supported on this unit yet") == 0);
        assert(params == NULL);

        build_host("cap_full", -1);
        g_mmu.spoolman_edit = true;
        g_mmu.spoolman_online = true;
        rc = actions_rpc_then("spool edit full11 remaining=500 then load", &g_mmu,
                              method, sizeof method, &params, then, sizeof then, &why);
        assert(rc == 1);
        assert(strcmp(then, "load full11") == 0);
        cJSON_Delete(params);
    }
}

/* ---------------------------------------------------- long unit names */

/* A unit's name is the host's own and can be any length: it is found by its
 * whole name (cut to the model's 31 bytes on both sides), so it stays one unit
 * however often it is updated, and its bay ids and lines still resolve. */
static void test_long_names(void)
{
    static const struct { const char *fixture; const char *id; const char *load; } k[] = {
        { "cap_name_8", "ams_left1", "OPENAMS_LOAD GROUP=T0 SLOT=0" },
        { "cap_name_16", "ams_left_unit_011", "OPENAMS_LOAD GROUP=T0 SLOT=0" },
        { "cap_name_31", "ams_left_unit_with_long_nam_1xx1", "OPENAMS_LOAD GROUP=T0 SLOT=0" },
    };
    unsigned i;
    char line[256], name[80];
    cJSON *obj;

    for(i = 0; i < sizeof k / sizeof k[0]; i++) {
        build_host(k[i].fixture, -1);
        assert(g_mmu.unit_count == 1);                        /* three updates, one unit */
        snprintf(line, sizeof line, "load %s", k[i].id);
        cap_ok(line, k[i].load);
        assert(mmu_set_this_unit_by_name(g_mmu.units[0].name));
        assert(g_mmu.toolhead_count == 1);                    /* and one lane */
    }

    /* two names that share their first 8 and 9 bytes stay two units */
    build_host("cap_name_shared_prefix", -1);
    assert(g_mmu.unit_count == 2);
    cap_ok("load ams_ht_a11", "OPENAMS_LOAD GROUP=T0 SLOT=0");
    cap_ok("load ams_ht_a21", "OPENAMS_LOAD GROUP=T1 SLOT=2");

    /* a name past the limit is cut the same way each time: still one unit,
     * found again by its full name, and a bind by that name finds it */
    memset(name, 'u', 40);
    name[40] = '\0';
    status_map_reset(&g_mmu);
    for(i = 0; i < 3; i++) {
        snprintf(line, sizeof line,
                 "{\"unit\":\"%s\",\"family\":\"ams2\",\"idx\":1,\"bays\":[{\"id\":0,\"s\":\"r\",\"a\":\"lc\"}],"
                 "\"lane\":{\"id\":\"fps\"},\"online\":true}", name);
        obj = cJSON_Parse(line);
        assert(obj && status_map_apply_openams_ui(&g_mmu, obj));
        cJSON_Delete(obj);
    }
    assert(g_mmu.unit_count == 1);
    assert(strlen(g_mmu.units[0].name) == 31);
    assert(mmu_set_this_unit_by_name(name));
}

int main(void)
{
    test_model_is_the_fixture();
    test_load_unload();
    test_stop();
    test_stop_whole_sequence();
    test_toolhead_targeted();
    test_load_while_positioning();
    test_reread_calibrate();
    test_dry();
    test_setting();
    test_groups();
    test_change_group();
    test_faults();
    test_local_vocabulary();
    test_unknown_and_unresolvable();
    test_small_out();
    test_rpc_spool_list();
    test_rpc_spool_confirm();
    test_rpc_unknown_slot();
    test_rpc_not_rpc();
    test_rpc_vendor_list();
    test_rpc_spool_edit();
    test_actions_check();
    test_rpc_spool_edit_own_weight_and_pa();
    test_rpc_spool_edit_then_load();
    test_host_lists();
    test_long_names();

    printf("test_actions_map: ok\n");
    return 0;
}
