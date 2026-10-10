/**
 * @file test_status_map.c
 * The fixture replay, end to end: every recorded event applies, and the fields
 * the screens read end up where C2 6.5 says they should.
 *
 * Plain C, plain assert, no framework. Each fixture is applied onto a model
 * reset with status_map_reset(), exactly as --replay does it.
 */

/* The default CMAKE_BUILD_TYPE here is Release, which puts -DNDEBUG on the
 * command line and would compile every assert() in this file away: the CTest
 * run would then pass without checking anything. Keep them live. */
#undef NDEBUG

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend/status_map.h"
#include "lib/cjson/cJSON.h"
#include "model/mmu_model.h"

#ifndef STATUS_MAP_FIXTURE_DIR
#error "compile this test with -DSTATUS_MAP_FIXTURE_DIR=<abs path to tests/fixtures>"
#endif

/* mmu_model.c logs group edits through actions.c; the test does not care. */
void actions_log(const char *fmt, ...)
{
    (void)fmt;
}

/* ---------------------------------------------------------------- loading */

static cJSON *load_fixture(const char *name)
{
    char path[512];
    FILE *f;
    long len;
    char *text;
    cJSON *root;

    snprintf(path, sizeof path, "%s/%s.json", STATUS_MAP_FIXTURE_DIR, name);
    f = fopen(path, "rb");
    if(!f) {
        fprintf(stderr, "test_status_map: cannot open %s\n", path);
        exit(1);
    }
    if(fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "test_status_map: cannot size %s\n", path);
        exit(1);
    }
    text = malloc((size_t)len + 1);
    assert(text);
    if(fread(text, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "test_status_map: short read on %s\n", path);
        exit(1);
    }
    text[len] = '\0';
    fclose(f);

    root = cJSON_Parse(text);
    free(text);
    if(!root) {
        fprintf(stderr, "test_status_map: %s is not valid JSON\n", path);
        exit(1);
    }
    return root;
}

/* Apply every event of a fixture onto a freshly reset model. Every kind has to
 * come back true: the kinds the mapper does not read (c2_status, action_log,
 * spoolman_call, gcode_script) are ignored, not failures.
 *
 * Also checks the one expectation that does not depend on a fixture's story:
 * every bay its Spoolman status flags for confirmation must show it on the
 * matching slot. Returns how many bays were flagged. */
static int replay(const char *name)
{
    cJSON *root = load_fixture(name);
    const cJSON *events = cJSON_GetObjectItemCaseSensitive(root, "events");
    const cJSON *ev;
    int flagged = 0;

    if(!cJSON_IsArray(events)) {
        fprintf(stderr, "test_status_map: %s has no events array\n", name);
        exit(1);
    }

    status_map_reset(&g_mmu);

    cJSON_ArrayForEach(ev, events) {
        if(!status_map_apply_fixture_event(&g_mmu, ev)) {
            fprintf(stderr, "test_status_map: %s: an event failed to apply\n", name);
            exit(1);
        }
    }

    cJSON_ArrayForEach(ev, events) {
        const cJSON *kind = cJSON_GetObjectItemCaseSensitive(ev, "kind");
        const cJSON *payload, *bays, *bay;

        if(!cJSON_IsString(kind)) continue;
        if(strcmp(kind->valuestring, "component_status") != 0) continue;

        payload = cJSON_GetObjectItemCaseSensitive(ev, "payload");
        bays = payload ? cJSON_GetObjectItemCaseSensitive(payload, "bays") : NULL;
        if(!cJSON_IsObject(bays)) continue;

        cJSON_ArrayForEach(bay, bays) {
            const cJSON *pc;
            char uname[16];
            int slot, ui, i;
            size_t n;

            if(!bay->string) continue;
            pc = cJSON_GetObjectItemCaseSensitive(bay, "pending_confirmation");
            if(!cJSON_IsTrue(pc)) continue;

            /* "<unit>-<bay>" */
            {
                const char *d = strrchr(bay->string, '-');
                assert(d && d > bay->string);
                n = (size_t)(d - bay->string);
                assert(n < sizeof uname);
                memcpy(uname, bay->string, n);
                uname[n] = '\0';
                slot = atoi(d + 1);
            }

            ui = -1;
            for(i = 0; i < (int)g_mmu.unit_count; i++) {
                if(strcmp(g_mmu.units[i].name, uname) == 0) {
                    ui = i;
                    break;
                }
            }
            assert(ui >= 0);
            assert(slot >= 0 && slot < (int)g_mmu.units[ui].slot_count);
            assert(g_mmu.units[ui].slots[slot].pending_confirmation);
            flagged++;
        }
    }

    /* The parsed tree must outlive the loop above; nothing else needs it. */
    cJSON_Delete(root);
    return flagged;
}

/* --------------------------------------------------------------- searching */

static int find_unit(const char *name)
{
    int i;

    for(i = 0; i < (int)g_mmu.unit_count; i++) {
        if(strcmp(g_mmu.units[i].name, name) == 0) return i;
    }
    return -1;
}

/* Same as find_unit(), for a model that is not g_mmu (test_two_lanes() uses a
 * local model, since it must not disturb the fixtures that follow it). */
static int find_unit_in(const mmu_model_t *m, const char *name)
{
    int i;

    for(i = 0; i < (int)m->unit_count; i++) {
        if(strcmp(m->units[i].name, name) == 0) return i;
    }
    return -1;
}

static bool any_positioning(void)
{
    int u, i;

    for(u = 0; u < (int)g_mmu.unit_count; u++) {
        for(i = 0; i < (int)g_mmu.units[u].slot_count; i++) {
            if(g_mmu.units[u].slots[i].positioning) return true;
        }
    }
    return false;
}

static bool any_cal_state(mmu_cal_state_t want)
{
    int u, i;

    for(u = 0; u < (int)g_mmu.unit_count; u++) {
        for(i = 0; i < (int)g_mmu.units[u].slot_count; i++) {
            if(g_mmu.units[u].slots[i].cal_state == want) return true;
        }
    }
    return false;
}

static bool any_drying(void)
{
    int u;

    for(u = 0; u < (int)g_mmu.unit_count; u++) {
        if(g_mmu.units[u].dry_state != MMU_DRY_IDLE) return true;
    }
    return false;
}

/* --------------------------------------------------------------- fixtures */

/* idle: one unit, the lane parked, and the Spoolman facts merged into the
 * bays the projection only linked. */
static void test_idle(void)
{
    int u;

    assert(replay("idle") == 0);

    assert(g_mmu.unit_count >= 1);
    assert(g_mmu.busy == MMU_BUSY_NONE);

    u = find_unit("oams1");
    assert(u == 0);
    assert(g_mmu.units[u].kind == MMU_UNIT_4SLOT);
    assert(g_mmu.units[u].variant == MMU_VARIANT_AMS);
    assert(g_mmu.units[u].connected);
    assert(g_mmu.units[u].slot_count == 4);
    assert(g_mmu.units[u].has_humidity_pct && g_mmu.units[u].humidity_pct == 31);
    assert(g_mmu.units[u].has_temp && g_mmu.units[u].temp_c == 24);
    assert(g_mmu.units[u].rfid);      /* act carries rfid_scan */
    assert(!g_mmu.units[u].can_dry);  /* dryer is null */

    /* bays: the link facts the projection owns */
    assert(g_mmu.units[u].slots[0].state == MMU_SLOT_READY);
    assert(g_mmu.units[u].slots[0].spool_id == 12);
    assert(g_mmu.units[u].slots[0].info_source == MMU_INFO_TAG);
    assert(g_mmu.units[u].slots[1].state == MMU_SLOT_READY);
    assert(g_mmu.units[u].slots[1].spool_id == 15);
    assert(g_mmu.units[u].slots[1].info_source == MMU_INFO_MANUAL);
    assert(g_mmu.units[u].slots[2].state == MMU_SLOT_READY);
    assert(g_mmu.units[u].slots[2].spool_id == -1);
    assert(g_mmu.units[u].slots[2].info_source == MMU_INFO_UNKNOWN);
    assert(g_mmu.units[u].slots[3].state == MMU_SLOT_EMPTY);
    assert(g_mmu.units[u].slots[3].cal_state == MMU_CAL_CONFIGURED);

    /* bays: the spool facts the component status owns */
    assert(strcmp(g_mmu.units[u].slots[0].material, "PLA") == 0);
    assert(strcmp(g_mmu.units[u].slots[0].brand, "Bambu Lab") == 0);
    assert(g_mmu.units[u].slots[0].color == 0x0A2989u && g_mmu.units[u].slots[0].color_known);
    assert(g_mmu.units[u].slots[0].remaining_g == 812);
    assert(g_mmu.units[u].slots[0].remaining_pct == 81);
    assert(strcmp(g_mmu.units[u].slots[1].material, "PETG") == 0);
    assert(strcmp(g_mmu.units[u].slots[1].brand, "Polymaker") == 0);
    assert(g_mmu.units[u].slots[1].color == 0xF5F5F5u);
    assert(g_mmu.units[u].slots[1].remaining_g == 430);
    assert(g_mmu.units[u].slots[1].remaining_pct == 43);
    assert(g_mmu.units[u].slots[2].material[0] == '\0');  /* unlinked */
    assert(g_mmu.units[u].slots[2].remaining_g == -1);
    assert(g_mmu.spoolman_online);

    /* groups: declared order preserved, nothing loaded */
    assert(g_mmu.group_count == 2);
    assert(strcmp(g_mmu.groups[0].name, "T0") == 0);
    assert(g_mmu.groups[0].count == 2);
    assert(g_mmu.groups[0].members[0].unit == 0 && g_mmu.groups[0].members[0].slot == 0);
    assert(g_mmu.groups[0].members[1].unit == 0 && g_mmu.groups[0].members[1].slot == 1);
    assert(strcmp(g_mmu.groups[1].name, "T1") == 0);
    assert(g_mmu.groups[1].count == 1);

    assert(g_mmu.current_group == -1);
    assert(!g_mmu.loaded);
    assert(!g_mmu.runout_active);
    assert(!g_mmu.has_error);
    assert(g_mmu.motion_settled);

    /* the replay ignores the recording's c2_status event, so the settings
     * stay at their defaults and unknown */
    assert(!g_mmu.settings.known);
}

/* loading: the lane is mid-transfer, so busy, and the model is not settled. */
static void test_loading(void)
{
    assert(replay("loading") == 0);

    assert(g_mmu.busy != MMU_BUSY_NONE);
    assert(g_mmu.busy == MMU_BUSY_LOAD);
    assert(g_mmu.busy_unit == find_unit("oams1"));
    assert(g_mmu.busy_slot == 0);
    assert(!g_mmu.motion_settled);
    assert(!g_mmu.loaded);
    assert(!g_mmu.toolhead_sensor);

    /* lane.stage [["cut", "retract", "feed"], 2, null]: three steps, the
     * third running, none failed, each with its display label (C2 5.3) */
    assert(g_mmu.step_count == 3);
    assert(g_mmu.step_current == 2);
    assert(g_mmu.step_failed == -1);
    assert(strcmp(g_mmu.steps[0], "cut") == 0);
    assert(strcmp(g_mmu.steps[1], "retract") == 0);
    assert(strcmp(g_mmu.steps[2], "feed") == 0);
}

/* loaded: the lane holds bay 0, which is a member of T0. */
static void test_loaded(void)
{
    int u;

    assert(replay("loaded") == 0);

    assert(g_mmu.busy == MMU_BUSY_NONE);
    assert(g_mmu.loaded);
    assert(g_mmu.loaded_unit == find_unit("oams1"));
    assert(g_mmu.loaded_slot == 0);
    assert(g_mmu.loaded_ext == -1);
    assert(g_mmu.toolhead_sensor);

    assert(g_mmu.current_group >= 0);
    assert(strcmp(g_mmu.groups[g_mmu.current_group].name, "T0") == 0);

    u = find_unit("oams1");
    assert(g_mmu.units[u].slots[0].state == MMU_SLOT_LOADED);
    assert(g_mmu.units[u].slots[1].state == MMU_SLOT_READY);
    assert(g_mmu.motion_settled);
}

/* positioning: bay 0 is inserting a spool right now. */
static void test_positioning(void)
{
    int u;

    assert(replay("positioning") == 0);

    assert(any_positioning());
    u = find_unit("oams1");
    assert(g_mmu.units[u].slots[0].positioning);
    assert(g_mmu.units[u].slots[0].state == MMU_SLOT_READY);
    assert(g_mmu.units[u].slots[0].spool_id == 12);
    assert(!g_mmu.units[u].slots[1].positioning);
    assert(g_mmu.busy == MMU_BUSY_NONE);
}

/* uncalibrated: bays 1..3 have no effective PTFE length, and the component
 * status wants confirmation on bay 2. */
static void test_uncalibrated(void)
{
    int u;

    assert(replay("uncalibrated") == 1);

    assert(any_cal_state(MMU_CAL_UNCALIBRATED));
    u = find_unit("oams1");
    assert(g_mmu.units[u].slots[0].cal_state == MMU_CAL_CONFIGURED);
    assert(g_mmu.units[u].slots[1].cal_state == MMU_CAL_UNCALIBRATED);
    assert(g_mmu.units[u].slots[2].cal_state == MMU_CAL_UNCALIBRATED);
    assert(g_mmu.units[u].slots[3].cal_state == MMU_CAL_UNCALIBRATED);

    /* bay 2's projection link is null, so the C4 facts land on a slot that
     * has no spool id and no source: the merge gap the report calls out. */
    assert(g_mmu.units[u].slots[2].spool_id == -1);
    assert(g_mmu.units[u].slots[2].info_source == MMU_INFO_UNKNOWN);
    assert(g_mmu.units[u].slots[2].pending_confirmation);
    assert(g_mmu.units[u].slots[2].remaining_g == 1000);
    assert(g_mmu.units[u].slots[2].remaining_pct == 100);
    assert(strcmp(g_mmu.units[u].slots[2].material, "PETG") == 0);
    assert(g_mmu.units[u].slots[2].color == 0x000000u && g_mmu.units[u].slots[2].color_known); /* "000000": black filament */
    assert(!g_mmu.units[u].slots[1].pending_confirmation);
}

/* error: the drier faulted and the lane is on its failed step. */
static void test_error(void)
{
    int u;

    assert(replay("error") == 0);

    assert(g_mmu.has_error);
    assert(g_mmu.error_severity == MMU_SEV_STOP);
    assert(strcmp(g_mmu.error_code, "heater_overtemperature") == 0);
    assert(strcmp(g_mmu.error_text, "Dryer heater over temperature") == 0);
    assert(g_mmu.error_unit == find_unit("amsht"));
    assert(g_mmu.error_slot == -1);
    assert(!g_mmu.motion_settled);  /* has_error */

    /* lane.stage [["cut", "retract", "feed"], 2, "feed"]: three steps, the
     * third running, and that same third step is the one that failed */
    assert(g_mmu.step_count == 3);
    assert(g_mmu.step_current == 2);
    assert(g_mmu.step_failed == 2);
    assert(strcmp(g_mmu.steps[2], "feed") == 0);

    u = find_unit("amsht");
    assert(u == 0);
    assert(g_mmu.units[u].kind == MMU_UNIT_HT);
    assert(g_mmu.units[u].variant == MMU_VARIANT_AMS_HT);
    assert(g_mmu.units[u].slot_count == 1);
    assert(g_mmu.units[u].slots[0].state == MMU_SLOT_ERROR);
    assert(g_mmu.units[u].slots[0].spool_id == 44);
    assert(g_mmu.units[u].dry_state == MMU_DRY_ERROR);
    assert(g_mmu.units[u].dry_target_c == 80);
    assert(g_mmu.units[u].can_dry);

    /* the component status still brings in the unit it names */
    assert(g_mmu.unit_count >= 2);
    assert(find_unit("oams1") >= 0);
}

/* drying: a timed dry with minutes left. */
static void test_drying(void)
{
    int u;

    assert(replay("drying") == 0);

    assert(any_drying());
    u = find_unit("ams2");
    assert(u == 0);
    assert(g_mmu.units[u].variant == MMU_VARIANT_AMS2_PRO);
    assert(g_mmu.units[u].dry_state == MMU_DRY_DEHUMIDIFYING);  /* timed_dry */
    assert(g_mmu.units[u].dry_target_c == 55);
    assert(g_mmu.units[u].dry_remaining_min == 179);
    assert(g_mmu.units[u].can_dry);
    assert(g_mmu.units[u].power_adapter);
    assert(g_mmu.units[u].humidity_pct == 9);
    assert(g_mmu.units[u].temp_c == 55);
    assert(g_mmu.units[u].slot_count == 4);
}

/* multi_unit: the projection describes one unit, the Spoolman status another,
 * so both have to exist; T0 also names a member on a third unit that never
 * reports. */
static void test_multi_unit(void)
{
    assert(replay("multi_unit") == 0);

    assert(g_mmu.unit_count >= 2);
    assert(find_unit("ams2") == 0);
    assert(find_unit("oams1") == 1);   /* created by C4 alone */

    /* T0 [ams2-1, ams2-0, amsht-0]: the absent unit's member is skipped and
     * the rest keep the declared order. */
    assert(g_mmu.group_count == 4);
    assert(strcmp(g_mmu.groups[0].name, "T0") == 0);
    assert(g_mmu.groups[0].count == 2);
    assert(g_mmu.groups[0].members[0].unit == 0 && g_mmu.groups[0].members[0].slot == 1);
    assert(g_mmu.groups[0].members[1].unit == 0 && g_mmu.groups[0].members[1].slot == 0);

    /* T1 and T3 carry a name and nothing else; T2 names one real bay. */
    assert(g_mmu.groups[1].count == 0);
    assert(strcmp(g_mmu.groups[2].name, "T2") == 0);
    assert(g_mmu.groups[2].count == 1);
    assert(g_mmu.groups[3].count == 0);

    assert(g_mmu.loaded && g_mmu.loaded_unit == 0 && g_mmu.loaded_slot == 1);
    assert(g_mmu.current_group == 0);
    assert(g_mmu.units[0].slots[1].state == MMU_SLOT_LOADED);
    assert(g_mmu.units[0].dry_state == MMU_DRY_IDLE);  /* dryer "off" */
    assert(g_mmu.units[0].can_dry);
}

/* remote_unit: the lane's bay belongs to a unit that reports nothing at all. */
static void test_remote_unit(void)
{
    assert(replay("remote_unit") == 0);

    assert(g_mmu.unit_count == 1);   /* oams2 has no status event anywhere */
    assert(g_mmu.busy == MMU_BUSY_LOAD);
    assert(g_mmu.busy_unit == -1);   /* the ref resolves to no unit in the model */
    assert(g_mmu.busy_slot == -1);
    assert(!g_mmu.motion_settled);

    /* T1 [oams2-2, oams1-2]: the unreachable member is skipped. */
    assert(strcmp(g_mmu.groups[1].name, "T1") == 0);
    assert(g_mmu.groups[1].count == 1);
    assert(g_mmu.groups[1].members[0].unit == 0 && g_mmu.groups[1].members[0].slot == 2);
    assert(g_mmu.groups[0].count == 2);
}

/* runout_reload: an auto-refill in flight, bay 0 already spent. */
static void test_runout_reload(void)
{
    int u;

    assert(replay("runout_reload") == 0);

    assert(g_mmu.runout_active);     /* runout "loading", not idle */
    assert(g_mmu.runout_from_unit == 0 && g_mmu.runout_from_slot == 0);
    assert(g_mmu.runout_to_unit == 0 && g_mmu.runout_to_slot == 1);

    u = find_unit("oams1");
    assert(g_mmu.units[u].slots[0].state == MMU_SLOT_EMPTY);  /* ran out */
    assert(g_mmu.units[u].slots[1].state == MMU_SLOT_READY);
    assert(g_mmu.units[u].slots[0].spool_id == 12);           /* still tagged */

    assert(g_mmu.loaded && g_mmu.loaded_unit == 0 && g_mmu.loaded_slot == 0);
    /* lane.stage [["feed"], 0, null] */
    assert(g_mmu.step_count == 1 && g_mmu.step_current == 0);
    assert(strcmp(g_mmu.steps[0], "feed") == 0);
    assert(g_mmu.step_failed == -1);
}

/* ----------------------------------------------------------- toolheads (U1) */

/* Two openams_ui projections on two different FPS lanes must not overwrite
 * each other's globals (the live bug U1a fixes): unit A (lane fps1) is
 * loaded at bay 0, unit B (lane fps2) is loading bay 2 with a stage. Applying
 * B after A leaves A's toolhead loaded and B's toolhead loading, and the
 * globals mirror whichever unit is this_unit. */
static void test_two_lanes(void)
{
    mmu_model_t m;
    cJSON *a = cJSON_Parse(
        "{\"unit\":\"unitA\",\"family\":\"ams1\",\"bays\":[{\"s\":\"l\"},{\"s\":\"r\"}],"
        "\"lane\":{\"id\":\"fps1\",\"op\":\"LOADED\",\"bay\":\"unitA-0\"}}");
    cJSON *b = cJSON_Parse(
        "{\"unit\":\"unitB\",\"family\":\"ams1\","
        "\"bays\":[{\"s\":\"r\"},{\"s\":\"r\"},{\"s\":\"r\"}],"
        "\"lane\":{\"id\":\"fps2\",\"op\":\"LOADING\",\"bay\":\"unitB-2\","
        "\"stage\":[[\"cut\",\"feed\"],0,null]}}");
    int ua, ub;
    const mmu_toolhead_t *tha, *thb;

    assert(a && b);
    status_map_reset(&m);

    assert(status_map_apply_openams_ui(&m, a));
    assert(status_map_apply_openams_ui(&m, b));

    ua = find_unit_in(&m, "unitA");
    ub = find_unit_in(&m, "unitB");
    assert(ua >= 0 && ub >= 0);
    assert(m.units[ua].toolhead >= 0 && m.units[ub].toolhead >= 0);
    assert(m.units[ua].toolhead != m.units[ub].toolhead);   /* different lanes */

    /* Each lane kept its own toolhead's state, regardless of application
     * order: applying B did not touch A's toolhead. */
    tha = mmu_model_toolhead(&m, m.units[ua].toolhead);
    thb = mmu_model_toolhead(&m, m.units[ub].toolhead);
    assert(tha && thb);
    assert(tha->loaded && tha->loaded_unit == ua && tha->loaded_slot == 0);
    assert(thb->busy == MMU_BUSY_LOAD && thb->busy_unit == ub && thb->busy_slot == 2);
    assert(thb->step_count == 2 && thb->step_current == 0);

    /* this_unit = A: the globals mirror A. */
    m.this_unit = (int8_t)ua;
    mmu_model_refresh_this(&m);
    assert(m.loaded && m.loaded_unit == ua && m.loaded_slot == 0);
    assert(m.busy == MMU_BUSY_NONE);

    /* Switch this_unit to B and refresh: the globals now mirror B. */
    m.this_unit = (int8_t)ub;
    mmu_model_refresh_this(&m);
    assert(!m.loaded);
    assert(m.busy == MMU_BUSY_LOAD && m.busy_unit == ub && m.busy_slot == 2);
    assert(m.step_count == 2 && m.step_current == 0);

    cJSON_Delete(a);
    cJSON_Delete(b);
}

/* current_group follows lane.group, because the host is authoritative about
 * which of the lane's groups is loaded, and falls back to the loaded bay's own
 * membership when the projection does not name one it knows. */
static void test_current_group(void)
{
    cJSON *obj = cJSON_Parse(
        "{\"unit\":\"oams1\",\"family\":\"ams1\",\"bays\":[{\"s\":\"l\"},{\"s\":\"r\"}],"
        "\"groups\":[{\"n\":\"T0\",\"b\":[\"oams1-0\"]},{\"n\":\"T1\",\"b\":[\"oams1-1\"]}],"
        "\"lane\":{\"op\":\"LOADED\",\"bay\":\"oams1-0\",\"group\":\"T1\"}}");
    cJSON *lane;

    assert(obj);
    status_map_reset(&g_mmu);
    assert(status_map_apply_openams_ui(&g_mmu, obj));

    assert(g_mmu.loaded && g_mmu.loaded_unit == 0 && g_mmu.loaded_slot == 0);
    assert(strcmp(g_mmu.groups[0].name, "T0") == 0);
    assert(strcmp(g_mmu.groups[1].name, "T1") == 0);
    /* bay 0 is a member of T0, but the lane says T1 is the loaded one */
    assert(g_mmu.current_group == 1);

    /* no lane.group: the bay's own membership decides */
    lane = cJSON_GetObjectItemCaseSensitive(obj, "lane");
    assert(cJSON_IsObject(lane));
    cJSON_DeleteItemFromObjectCaseSensitive(lane, "group");
    status_map_reset(&g_mmu);
    assert(status_map_apply_openams_ui(&g_mmu, obj));
    assert(g_mmu.loaded && g_mmu.current_group == 0);

    /* a lane.group the table does not hold falls back the same way */
    assert(cJSON_AddStringToObject(lane, "group", "T9"));
    status_map_reset(&g_mmu);
    assert(status_map_apply_openams_ui(&g_mmu, obj));
    assert(g_mmu.current_group == 0);

    cJSON_Delete(obj);
}

/* A full `openams_ui oams1` projection, the shape the recorded fixtures use:
 * four bays, the FPS lane LOADED from bay 3, and @p groups as the unit's whole
 * group list - the host's complete answer for the unit it came from. */
static void apply_oams1(const char *groups, const char *lane_group)
{
    char text[1024];
    cJSON *obj;

    snprintf(text, sizeof text,
             "{\"v\":1,\"unit\":\"oams1\",\"idx\":1,\"family\":\"ams1\","
             "\"bays\":[{\"id\":0,\"s\":\"e\"},{\"id\":1,\"s\":\"r\"},"
             "{\"id\":2,\"s\":\"r\"},{\"id\":3,\"s\":\"l\"}],"
             "\"groups\":%.600s,"
             "\"lane\":{\"id\":\"fps1\",\"ext\":\"extruder\",\"op\":\"LOADED\","
             "\"bay\":\"oams1-3\",\"group\":\"%.16s\"}}",
             groups, lane_group);
    obj = cJSON_Parse(text);
    assert(obj);
    assert(status_map_apply_openams_ui(&g_mmu, obj));
    cJSON_Delete(obj);
}

/* The `groups` of the recorded move: bay oams1-2 out of T2 and into T1. */
static const char k_groups_before[] =
    "[{\"n\":\"T0\",\"b\":[\"oams1-0\"],\"s\":\"e\"},"
    "{\"n\":\"T1\",\"b\":[\"oams1-1\"],\"s\":\"r\"},"
    "{\"n\":\"T2\",\"b\":[\"oams1-2\"],\"s\":\"r\"},"
    "{\"n\":\"T3\",\"b\":[\"oams1-3\"],\"s\":\"r\"}]";
static const char k_groups_after[] =
    "[{\"n\":\"T0\",\"b\":[\"oams1-0\"],\"s\":\"e\"},"
    "{\"n\":\"T1\",\"b\":[\"oams1-1\",\"oams1-2\"],\"s\":\"rr\"},"
    "{\"n\":\"T3\",\"b\":[\"oams1-3\"],\"s\":\"r\"}]";
static const char k_groups_back[] =
    "[{\"n\":\"T0\",\"b\":[\"oams1-0\"],\"s\":\"e\"},"
    "{\"n\":\"T1\",\"b\":[\"oams1-1\"],\"s\":\"r\"},"
    "{\"n\":\"T2\",\"b\":[\"oams1-2\"],\"s\":\"r\"},"
    "{\"n\":\"T3\",\"b\":[\"oams1-3\"],\"s\":\"r\"}]";

/* What a group's membership is right now, as " unit-slot" per member, and
 * "" when the table holds no group of that name. */
static void group_members_of(const char *name, char *out, size_t n)
{
    int g, k;
    size_t used = 0;

    out[0] = '\0';
    for(g = 0; g < (int)g_mmu.group_count; g++) {
        if(strcmp(g_mmu.groups[g].name, name) != 0) continue;
        for(k = 0; k < (int)g_mmu.groups[g].count; k++)
            used += (size_t)snprintf(out + used, n - used, " %d-%d",
                                     (int)g_mmu.groups[g].members[k].unit,
                                     (int)g_mmu.groups[g].members[k].slot);
        return;
    }
}

/* The display must follow the group changes the host makes after startup: a
 * `groups` list is the host's whole answer for that unit, so a bay that moved
 * out of a group, and a group the host stopped listing, both have to be taken
 * out of the model - and every stored group index has to follow the table. */
static void test_groups_follow_the_host(void)
{
    char members[64];
    const mmu_toolhead_t *th;
    int u, t5;

    status_map_reset(&g_mmu);

    /* before: T0..T3, one bay each, the lane loaded on T3 */
    apply_oams1(k_groups_before, "T3");
    assert(g_mmu.group_count == 4);
    assert(strcmp(g_mmu.groups[0].name, "T0") == 0);
    assert(strcmp(g_mmu.groups[3].name, "T3") == 0);
    assert(g_mmu.current_group == 3);

    /* A second unit on its own lane, so that its toolhead's group index is
     * stored where oams1's next update can only repair it. */
    {
        cJSON *obj = cJSON_Parse(
            "{\"v\":1,\"unit\":\"oams2\",\"idx\":2,\"family\":\"ams1\","
            "\"bays\":[{\"id\":0,\"s\":\"r\"}],"
            "\"groups\":[{\"n\":\"T5\",\"b\":[\"oams2-0\"],\"s\":\"r\"}],"
            "\"lane\":{\"id\":\"fps2\",\"ext\":\"extruder\",\"op\":\"LOADED\","
            "\"bay\":\"oams2-0\",\"group\":\"T5\"}}");

        assert(obj);
        assert(status_map_apply_openams_ui(&g_mmu, obj));
        cJSON_Delete(obj);
    }
    u = find_unit("oams2");
    assert(u >= 0 && g_mmu.units[u].toolhead >= 0);
    assert(g_mmu.group_count == 5);
    assert(strcmp(g_mmu.groups[4].name, "T5") == 0);
    th = mmu_model_toolhead(&g_mmu, g_mmu.units[u].toolhead);
    assert(th && th->current_group == 4);   /* T5 */

    /* after: bay 2 is a member of T1, and T2 is not listed at all */
    apply_oams1(k_groups_after, "T3");
    group_members_of("T1", members, sizeof members);
    assert(strcmp(members, " 0-1 0-2") == 0);
    assert(g_mmu.group_count == 4);          /* T2 is gone */
    assert(strcmp(g_mmu.groups[0].name, "T0") == 0);
    assert(strcmp(g_mmu.groups[1].name, "T1") == 0);
    assert(strcmp(g_mmu.groups[2].name, "T3") == 0);
    assert(strcmp(g_mmu.groups[3].name, "T5") == 0);
    group_members_of("T2", members, sizeof members);
    assert(strcmp(members, "") == 0);

    /* The lane is still loaded out of T3, now at its new index, and the
     * second unit's toolhead kept T5 - its index only moved. */
    u = find_unit("oams1");
    assert(g_mmu.units[u].toolhead >= 0);
    th = mmu_model_toolhead(&g_mmu, g_mmu.units[u].toolhead);
    assert(th && th->current_group == 2);
    assert(g_mmu.current_group == 2);        /* the model mirror */
    assert(strcmp(g_mmu.groups[2].name, "T3") == 0);
    t5 = g_mmu.units[find_unit("oams2")].toolhead;
    assert(t5 >= 0);
    th = mmu_model_toolhead(&g_mmu, t5);
    assert(th && th->current_group == 3);
    assert(strcmp(g_mmu.groups[th->current_group].name, "T5") == 0);

    /* back: T2 is listed again and holds bay 2; T1 is back to bay 1 alone */
    apply_oams1(k_groups_back, "T3");
    assert(g_mmu.group_count == 5);
    group_members_of("T2", members, sizeof members);
    assert(strcmp(members, " 0-2") == 0);
    group_members_of("T1", members, sizeof members);
    assert(strcmp(members, " 0-1") == 0);
    assert(mmu_slot_group(0, 2) >= 0);
    assert(strcmp(g_mmu.groups[mmu_slot_group(0, 2)].name, "T2") == 0);
    /* T2 takes its place by name, and the loaded groups' indices follow */
    assert(strcmp(g_mmu.groups[0].name, "T0") == 0 && strcmp(g_mmu.groups[1].name, "T1") == 0 &&
           strcmp(g_mmu.groups[2].name, "T2") == 0 && strcmp(g_mmu.groups[3].name, "T3") == 0 &&
           strcmp(g_mmu.groups[4].name, "T5") == 0);
    assert(g_mmu.current_group == 3);
    assert(strcmp(g_mmu.groups[3].name, "T3") == 0);
    th = mmu_model_toolhead(&g_mmu, t5);
    assert(th && th->current_group == 4);
    assert(strcmp(g_mmu.groups[th->current_group].name, "T5") == 0);

    /* an update without `groups` is a delta: nothing about them moves */
    {
        cJSON *obj = cJSON_Parse(
            "{\"v\":1,\"unit\":\"oams1\",\"idx\":1,\"family\":\"ams1\","
            "\"bays\":[{\"id\":0,\"s\":\"e\"},{\"id\":1,\"s\":\"r\"},"
            "{\"id\":2,\"s\":\"r\"},{\"id\":3,\"s\":\"l\"}],"
            "\"lane\":{\"id\":\"fps1\",\"ext\":\"extruder\",\"op\":\"LOADED\","
            "\"bay\":\"oams1-3\",\"group\":\"T3\"}}");

        assert(obj);
        assert(status_map_apply_openams_ui(&g_mmu, obj));
        cJSON_Delete(obj);
    }
    assert(g_mmu.group_count == 5);
    group_members_of("T2", members, sizeof members);
    assert(strcmp(members, " 0-2") == 0);
}

/* Groups are listed by name, a run of digits read as a number: T2 before
 * T10, whatever order the units' updates created them in. */
static void test_groups_in_name_order(void)
{
    cJSON *obj = cJSON_Parse(
        "{\"v\":1,\"unit\":\"oams1\",\"idx\":1,\"family\":\"ams1\","
        "\"bays\":[{\"id\":0,\"s\":\"r\"},{\"id\":1,\"s\":\"r\"},{\"id\":2,\"s\":\"r\"},{\"id\":3,\"s\":\"r\"}],"
        "\"groups\":[{\"n\":\"T10\",\"b\":[\"oams1-0\"],\"s\":\"r\"},{\"n\":\"T2\",\"b\":[\"oams1-1\"],\"s\":\"r\"},"
        "{\"n\":\"T1\",\"b\":[\"oams1-2\"],\"s\":\"r\"},{\"n\":\"A\",\"b\":[\"oams1-3\"],\"s\":\"r\"}],"
        "\"lane\":{\"id\":\"fps1\",\"ext\":\"extruder\",\"op\":\"UNLOADED\"}}");

    status_map_reset(&g_mmu);
    assert(obj);
    assert(status_map_apply_openams_ui(&g_mmu, obj));
    cJSON_Delete(obj);
    assert(g_mmu.group_count == 4);
    assert(strcmp(g_mmu.groups[0].name, "A") == 0);
    assert(strcmp(g_mmu.groups[1].name, "T1") == 0);
    assert(strcmp(g_mmu.groups[2].name, "T2") == 0);
    assert(strcmp(g_mmu.groups[3].name, "T10") == 0);
    assert(g_mmu.groups[3].members[0].slot == 0);
}

/* `lane.ms` is the host's own motion-safety predicate: the model takes it as
 * published, and only derives it from busy/error when an older host omits it. */
static void test_motion_settled(void)
{
    cJSON *obj = cJSON_Parse(
        "{\"unit\":\"oams1\",\"family\":\"ams1\",\"bays\":[{\"s\":\"p\"}],"
        "\"lane\":{\"op\":\"UNLOADED\",\"ms\":false}}");
    cJSON *lane;

    assert(obj);
    status_map_reset(&g_mmu);
    assert(status_map_apply_openams_ui(&g_mmu, obj));
    /* nothing is busy and no fault is latched, so the old guess would say
     * settled; the host says a bay is still positioning, so it is not */
    assert(g_mmu.busy == MMU_BUSY_NONE && !g_mmu.has_error);
    assert(!g_mmu.motion_settled);

    /* `ms: true` on the same idle lane */
    lane = cJSON_GetObjectItemCaseSensitive(obj, "lane");
    assert(cJSON_IsObject(lane));
    assert(cJSON_ReplaceItemInObjectCaseSensitive(lane, "ms", cJSON_CreateBool(1)));
    status_map_reset(&g_mmu);
    assert(status_map_apply_openams_ui(&g_mmu, obj));
    assert(g_mmu.motion_settled);

    /* no `ms`: an older host, so the derivation stands... */
    cJSON_DeleteItemFromObjectCaseSensitive(lane, "ms");
    status_map_reset(&g_mmu);
    assert(status_map_apply_openams_ui(&g_mmu, obj));
    assert(g_mmu.motion_settled);   /* idle lane */

    /* ...and still reports unsettled while a transfer is running */
    assert(cJSON_ReplaceItemInObjectCaseSensitive(
        lane, "op", cJSON_CreateString("LOADING")));
    status_map_reset(&g_mmu);
    assert(status_map_apply_openams_ui(&g_mmu, obj));
    assert(!g_mmu.motion_settled);

    cJSON_Delete(obj);
}

/* The five settings (C2 4.9) are mirrored by hand: the replay ignores the
 * recording's c2_status event, which is where a fixture carries them. A
 * partial object changes only the keys it carries and marks the settings
 * known; an empty object marks them known without changing a value. */
static void test_settings(void)
{
    cJSON *pa_on = cJSON_Parse("{\"apply_pa_on_load\":true}");
    cJSON *full = cJSON_Parse(
        "{\"read_tag_on_insertion\":true,\"read_tags_at_startup\":false,"
        "\"auto_reload_on_runout\":false,\"auto_create_spoolman_spools\":true,"
        "\"apply_pa_on_load\":false}");
    cJSON *rti_off = cJSON_Parse("{\"read_tag_on_insertion\":false}");
    cJSON *empty = cJSON_Parse("{}");
    cJSON *not_object = cJSON_Parse("[]");

    assert(pa_on && full && rti_off && empty && not_object);

    /* status_map_reset leaves the C2 defaults and known = false */
    status_map_reset(&g_mmu);
    assert(!g_mmu.settings.known);
    assert(g_mmu.settings.read_tag_on_insertion);
    assert(g_mmu.settings.read_tags_at_startup);
    assert(g_mmu.settings.auto_reload_on_runout);
    assert(g_mmu.settings.auto_create_spoolman_spools);
    assert(!g_mmu.settings.apply_pa_on_load);

    /* one key alone: it flips, the rest keep their defaults, and the object
     * counts as published */
    assert(status_map_apply_settings(&g_mmu, pa_on));
    assert(g_mmu.settings.known);
    assert(g_mmu.settings.apply_pa_on_load);
    assert(g_mmu.settings.read_tag_on_insertion);
    assert(g_mmu.settings.read_tags_at_startup);
    assert(g_mmu.settings.auto_reload_on_runout);
    assert(g_mmu.settings.auto_create_spoolman_spools);

    assert(status_map_apply_settings(&g_mmu, full));
    assert(g_mmu.settings.known);
    assert(g_mmu.settings.read_tag_on_insertion);
    assert(!g_mmu.settings.read_tags_at_startup);
    assert(!g_mmu.settings.auto_reload_on_runout);
    assert(g_mmu.settings.auto_create_spoolman_spools);
    assert(!g_mmu.settings.apply_pa_on_load);

    /* a diff carrying one key leaves the other four as the full object did */
    assert(status_map_apply_settings(&g_mmu, rti_off));
    assert(!g_mmu.settings.read_tag_on_insertion);
    assert(!g_mmu.settings.read_tags_at_startup);
    assert(!g_mmu.settings.auto_reload_on_runout);
    assert(g_mmu.settings.auto_create_spoolman_spools);
    assert(!g_mmu.settings.apply_pa_on_load);

    /* an empty object is well formed: known flips, nothing changes */
    status_map_reset(&g_mmu);
    assert(!g_mmu.settings.known);
    assert(status_map_apply_settings(&g_mmu, empty));
    assert(g_mmu.settings.known);
    assert(g_mmu.settings.read_tag_on_insertion);
    assert(g_mmu.settings.read_tags_at_startup);
    assert(g_mmu.settings.auto_reload_on_runout);
    assert(g_mmu.settings.auto_create_spoolman_spools);
    assert(!g_mmu.settings.apply_pa_on_load);

    /* a non-object is refused and changes nothing */
    assert(!status_map_apply_settings(&g_mmu, NULL));
    assert(!status_map_apply_settings(&g_mmu, not_object));
    assert(g_mmu.settings.known);
    assert(g_mmu.settings.read_tags_at_startup);

    cJSON_Delete(pa_on);
    cJSON_Delete(full);
    cJSON_Delete(rti_off);
    cJSON_Delete(empty);
    cJSON_Delete(not_object);
}

/* A malformed object is refused, and refusing changes nothing: the guards run
 * before any unit is created. */
static void test_malformed(void)
{
    cJSON *not_object = cJSON_Parse("[]");
    cJSON *no_bays = cJSON_Parse("{\"unit\":\"oams1\"}");
    cJSON *no_unit = cJSON_Parse("{\"bays\":[{\"s\":\"r\"}]}");
    cJSON *no_kind = cJSON_Parse("{\"t\":0,\"payload\":null}");
    cJSON *ignored_kind = cJSON_Parse("{\"t\":0,\"kind\":\"c2_status\",\"payload\":null}");
    cJSON *bad_payload = cJSON_Parse("{\"t\":0,\"kind\":\"openams_ui\",\"payload\":7}");
    cJSON *empty_comp = cJSON_Parse("{\"spoolman_online\":true,\"bays\":{}}");

    assert(not_object && no_bays && no_unit && no_kind && ignored_kind &&
           bad_payload && empty_comp);

    status_map_reset(&g_mmu);

    assert(!status_map_apply_openams_ui(&g_mmu, NULL));
    assert(!status_map_apply_openams_ui(&g_mmu, not_object));
    assert(!status_map_apply_openams_ui(&g_mmu, no_bays));
    assert(!status_map_apply_openams_ui(&g_mmu, no_unit));
    assert(!status_map_apply_component_status(&g_mmu, NULL));
    assert(!status_map_apply_component_status(&g_mmu, not_object));
    assert(!status_map_apply_component_status(&g_mmu, no_unit));  /* bays not an object */
    assert(!status_map_apply_fixture_event(&g_mmu, NULL));
    assert(!status_map_apply_fixture_event(&g_mmu, not_object));
    assert(!status_map_apply_fixture_event(&g_mmu, no_kind));

    /* nothing was created along the way */
    assert(g_mmu.unit_count == 0);

    /* a recorded kind the display does not read is ignored, not a failure,
     * while a kind that is read still fails on a payload of the wrong shape */
    assert(status_map_apply_fixture_event(&g_mmu, ignored_kind));
    assert(!status_map_apply_fixture_event(&g_mmu, bad_payload));

    /* an empty component status is well formed: online flips, no bay appears */
    assert(status_map_apply_component_status(&g_mmu, empty_comp));
    assert(g_mmu.spoolman_online);
    assert(g_mmu.unit_count == 0);

    cJSON_Delete(not_object);
    cJSON_Delete(no_bays);
    cJSON_Delete(no_unit);
    cJSON_Delete(no_kind);
    cJSON_Delete(ignored_kind);
    cJSON_Delete(bad_payload);
    cJSON_Delete(empty_comp);
}

/* ---------------------------------------------------------- fault / alert tests (D5c) */

/* Minimal openams_ui object with one bay, a lane, and a fault object.
 * fault and nf are top-level siblings (C2 6.2), not inside lane. */
static cJSON *make_fault_obj(const char *code, const char *sev,
                             const char *act1, const char *act2, int nf)
{
    char buf[512];
    int n;

    n = snprintf(buf, sizeof(buf),
        "{\"unit\":\"oams1\",\"family\":\"ams1\",\"bays\":[{\"s\":\"p\"}],"
        "\"lane\":{\"op\":\"UNLOADED\"},"
        "\"fault\":{\"code\":\"%s\",\"text\":\"Fault text\","
        "\"sev\":\"%s\",\"act\":[", code, sev);
    if(act1) n += snprintf(buf + n, sizeof(buf) - (size_t)n, "\"%s\"", act1);
    if(act1 && act2) n += snprintf(buf + n, sizeof(buf) - (size_t)n, ",\"%s\"", act2);
    snprintf(buf + n, sizeof(buf) - (size_t)n, "]},\"nf\":%d}", nf);
    return cJSON_Parse(buf);
}

/* A fault with act:["clear_fault","clear_errors"] fills both tokens, nf, and
 * adds one entry to the pending queue (not directly to the history). */
static void test_fault_actions(void)
{
    mmu_model_t m;
    cJSON *obj = make_fault_obj("ERR_001", "pause", "clear_fault", "clear_errors", 3);

    assert(obj);
    status_map_reset(&m);
    assert(status_map_apply_openams_ui(&m, obj));

    assert(m.has_error);
    assert(m.error_act_count == 2);
    assert(strcmp(m.error_act[0], "clear_fault")  == 0);
    assert(strcmp(m.error_act[1], "clear_errors") == 0);
    assert(m.error_count == 3);
    /* The alert lands in the pending queue, not the history yet. */
    assert(m.alert_pending_count == 1);
    assert(m.alert_count == 0);

    cJSON_Delete(obj);
}

/* The same fault applied twice produces one pending entry (not two). */
static void test_fault_dedup(void)
{
    mmu_model_t m;
    cJSON *obj = make_fault_obj("ERR_002", "pause", "clear_errors", NULL, 1);

    assert(obj);
    status_map_reset(&m);
    assert(status_map_apply_openams_ui(&m, obj));
    assert(status_map_apply_openams_ui(&m, obj));  /* same code, second apply */

    assert(m.alert_pending_count == 1);  /* only one pending entry */

    cJSON_Delete(obj);
}

/* A new fault code adds a second pending entry. */
static void test_fault_history(void)
{
    mmu_model_t m;
    cJSON *obj1 = make_fault_obj("ERR_A", "pause", "clear_errors", NULL, 1);
    cJSON *obj2 = make_fault_obj("ERR_B", "stop",  "clear_fault",  NULL, 1);

    assert(obj1 && obj2);
    status_map_reset(&m);
    assert(status_map_apply_openams_ui(&m, obj1));
    assert(status_map_apply_openams_ui(&m, obj2));

    assert(m.alert_pending_count == 2);
    assert(strcmp(m.error_code, "ERR_B") == 0);

    cJSON_Delete(obj1);
    cJSON_Delete(obj2);
}

/* An explicit null fault clears the tokens and count but keeps the pending
 * queue intact (the merge caller decides what to do with history). */
static void test_fault_null(void)
{
    mmu_model_t m;
    cJSON *obj_fault = make_fault_obj("ERR_C", "pause", "resume", NULL, 2);
    cJSON *obj_null  = cJSON_Parse(
        "{\"unit\":\"oams1\",\"family\":\"ams1\",\"bays\":[{\"s\":\"p\"}],"
        "\"lane\":{\"op\":\"UNLOADED\"},\"fault\":null,\"nf\":0}");

    assert(obj_fault && obj_null);
    status_map_reset(&m);
    assert(status_map_apply_openams_ui(&m, obj_fault));
    assert(m.alert_pending_count == 1);  /* fault arrived in pending */

    assert(status_map_apply_openams_ui(&m, obj_null));
    assert(!m.has_error);
    assert(m.error_act_count == 0);
    assert(m.error_count == 0);
    assert(m.alert_pending_count == 1);  /* pending kept (merge not called yet) */

    cJSON_Delete(obj_fault);
    cJSON_Delete(obj_null);
}

/* ------------------------------------------------- spool list tests */

static void test_spool_list(void)
{
    mmu_model_t m;
    /* 20 spools: one archived, so 19 are unarchived; we keep only 16. */
    char buf[8192];
    cJSON *arr;
    int i;
    int n;

    status_map_reset(&m);

    /* build array of 20 spools: spool id 100 is archived */
    n = snprintf(buf, sizeof buf, "[");
    for(i = 0; i < 20; i++) {
        int id = 200 + i;
        int arch = (id == 205) ? 1 : 0;  /* one archived */
        char entry[256];
        snprintf(entry, sizeof entry,
                 "%s{\"id\":%d,\"archived\":%s,\"remaining_weight\":%.1f,"
                 "\"filament\":{\"material\":\"PLA\",\"color_hex\":\"FF0000\","
                 "\"vendor\":{\"name\":\"Bambu\"}}}",
                 (i > 0) ? "," : "", id, arch ? "true" : "false",
                 (double)(1000 - i * 10));
        n += snprintf(buf + n, sizeof buf - (size_t)n, "%s", entry);
    }
    n += snprintf(buf + n, sizeof buf - (size_t)n, "]");
    (void)n;

    arr = cJSON_Parse(buf);
    assert(arr);
    assert(status_map_apply_spool_list(&m, arr));

    /* 20 entries - 1 archived = 19 unarchived; capped at 16 */
    assert(m.spool_count == 16);

    /* first entry has id 200, fields parsed */
    assert(m.spools[0].id == 200);
    assert(strcmp(m.spools[0].material, "PLA") == 0);
    assert(strcmp(m.spools[0].vendor,   "Bambu") == 0);
    assert(m.spools[0].color == 0xFF0000u);
    assert(m.spools[0].remaining_g == 1000);

    cJSON_Delete(arr);
}

static void test_spool_list_archived_skipped(void)
{
    mmu_model_t m;
    cJSON *arr = cJSON_Parse(
        "[{\"id\":1,\"archived\":false,\"remaining_weight\":800.0,"
        " \"filament\":{\"material\":\"PETG\",\"color_hex\":\"00FF00\","
        "              \"vendor\":{\"name\":\"Poly\"}}},"
        " {\"id\":2,\"archived\":true,\"remaining_weight\":100.0,"
        " \"filament\":{\"material\":\"ABS\",\"color_hex\":\"0000FF\","
        "              \"vendor\":{\"name\":\"eSUN\"}}}]");
    assert(arr);
    status_map_reset(&m);
    assert(status_map_apply_spool_list(&m, arr));
    assert(m.spool_count == 1);
    assert(m.spools[0].id == 1);
    assert(strcmp(m.spools[0].material, "PETG") == 0);
    cJSON_Delete(arr);
}

/* ------------------------------------------------- metadata / job tests */

static void test_metadata_weights(void)
{
    mmu_model_t m;
    cJSON *obj;

    status_map_reset(&m);

    /* A proxy result with filament_weights; 12.5 rounds up to 13. */
    obj = cJSON_Parse("{\"filament_weights\":[12.5,0,300]}");
    assert(obj);
    assert(status_map_apply_metadata(&m, obj));
    assert(m.job_need_g[0] == 13);
    assert(m.job_need_g[1] == 0);
    assert(m.job_need_g[2] == 300);
    /* Tools beyond the array stay zero. */
    assert(m.job_need_g[3] == 0);
    cJSON_Delete(obj);

    /* A missing filament_weights key leaves needs at zero. */
    {
        mmu_model_t m2;
        cJSON *obj2 = cJSON_Parse("{\"filename\":\"cube.gcode\"}");
        assert(obj2);
        status_map_reset(&m2);
        assert(!status_map_apply_metadata(&m2, obj2));
        assert(m2.job_need_g[0] == 0);
        cJSON_Delete(obj2);
    }
}

static void test_metadata_shortfall(void)
{
    mmu_model_t m;
    cJSON *cs, *meta;
    int gi;

    status_map_reset(&m);

    /* Build a component_status that gives oams1-0 50 g and oams1-1 50 g,
     * forming group T0 with 100 g total.  T1 has unknown remaining (spool
     * absent).  Needs are T0=120 g (shortfall), T1=50 g (all unknown, no
     * alert). */
    cs = cJSON_Parse(
        "{\"spoolman_online\":false,\"bays\":{"
        "\"oams1-0\":{\"state\":\"linked\",\"spool_id\":1,\"source\":\"tag\","
        "\"pending_confirmation\":false,\"error\":null,"
        "\"spool\":{\"material\":\"PLA\",\"color_hex\":\"FF0000\","
        "\"vendor\":\"Bambu\",\"remaining_g\":50,\"remaining_pct\":5}},"
        "\"oams1-1\":{\"state\":\"linked\",\"spool_id\":2,\"source\":\"tag\","
        "\"pending_confirmation\":false,\"error\":null,"
        "\"spool\":{\"material\":\"PLA\",\"color_hex\":\"FF0000\","
        "\"vendor\":\"Bambu\",\"remaining_g\":50,\"remaining_pct\":5}},"
        "\"oams1-2\":{\"state\":\"unlinked\",\"spool_id\":null,\"source\":\"none\","
        "\"pending_confirmation\":false,\"error\":null,\"spool\":null}}}");
    assert(cs);
    assert(status_map_apply_component_status(&m, cs));
    cJSON_Delete(cs);

    /* The low-pct alert should have fired for both linked slots (5 < 15). */
    assert(m.alert_pending_count == 2);
    m.alert_pending_count = 0; /* drain for shortfall test */

    /* Create groups T0 (slots 0,1) and T1 (slot 2). */
    gi = 0; /* reuse model's group system via status_map openams_ui fixture... */
    (void)gi;
    {
        /* Manually wire groups since there's no helper. */
        m.group_count = 2;
        snprintf(m.groups[0].name, sizeof m.groups[0].name, "T0");
        m.groups[0].count = 2;
        m.groups[0].members[0].unit = 0; m.groups[0].members[0].slot = 0;
        m.groups[0].members[1].unit = 0; m.groups[0].members[1].slot = 1;
        snprintf(m.groups[1].name, sizeof m.groups[1].name, "T1");
        m.groups[1].count = 1;
        m.groups[1].members[0].unit = 0; m.groups[1].members[0].slot = 2;
    }

    /* Apply metadata: T0 needs 120 g (> 100 available), T1 needs 50 g (unknown). */
    meta = cJSON_Parse("{\"filament_weights\":[120,50]}");
    assert(meta);
    assert(status_map_apply_metadata(&m, meta));
    cJSON_Delete(meta);

    /* T0 should be marked short; T1 is all-unknown so no shortfall. */
    assert(m.job_short == 0x01u); /* bit 0 = T0 */
    assert(m.alert_pending_count == 1);
    assert(m.alert_pending[0].kind == MMU_ALERT_SHORTFALL);   /* the sentence is made by the view */
    assert(m.alert_pending[0].arg[0] == 0 && m.alert_pending[0].arg[1] == 120);
    assert(m.alert_pending[0].severity == MMU_SEV_INFO);
}

/* Every fixture in the directory must at least apply cleanly and agree with
 * the expected number of pending confirmations. A new fixture adding one has
 * to be added here too, which is the point: the replay is the contract. */
static const char *const k_fixtures[] = {
    "idle", "loading", "loaded", "positioning", "uncalibrated",
    "error", "drying", "multi_unit", "remote_unit", "runout_reload",
};
static const int k_flagged[] = {0, 0, 0, 0, 1, 0, 0, 0, 0, 0};

/* The color of a bay's spool is known, unknown, or known to be black: a missing
 * or unreadable color_hex is "unknown" (a neutral tile); "000000" is black. */
static void test_color_known(void)
{
    mmu_model_t m;
    cJSON *ui = cJSON_Parse(
        "{\"unit\":\"oams1\",\"family\":\"ams1\",\"bays\":[{\"s\":\"r\"},{\"s\":\"r\"},{\"s\":\"r\"}],"
        "\"lane\":{\"id\":\"fps1\",\"op\":\"IDLE\"}}");
    cJSON *st = cJSON_Parse(
        "{\"bays\":{\"oams1-0\":{\"spool\":{\"material\":\"PLA\",\"color_hex\":\"000000\"}},"
        "\"oams1-1\":{\"spool\":{\"material\":\"PLA\",\"color_hex\":null}},"
        "\"oams1-2\":{\"spool\":{\"material\":\"PLA\",\"color_hex\":\"zzz\"}}}}");
    mmu_unit_t *u;

    assert(ui && st);
    status_map_reset(&m);
    assert(status_map_apply_openams_ui(&m, ui));
    assert(status_map_apply_component_status(&m, st));
    u = &m.units[0];
    assert(u->slots[0].color_known && u->slots[0].color == 0);
    assert(!u->slots[1].color_known);
    assert(!u->slots[2].color_known);
    cJSON_Delete(ui);
    cJSON_Delete(st);
}

/* The host says it can edit a bay's spool with `edit: true`; a status that says
 * nothing (an older component) says it can't. The vendor list is whole names,
 * sorted, without repeats. */
static void test_edit_capability_and_vendors(void)
{
    mmu_model_t m;
    cJSON *yes = cJSON_Parse("{\"spoolman_online\":true,\"edit\":true,\"bays\":{}}");
    cJSON *no = cJSON_Parse("{\"spoolman_online\":true,\"bays\":{}}");
    cJSON *vendors = cJSON_Parse(
        "[{\"id\":1,\"name\":\"zeta\"},{\"id\":2,\"name\":\"Alpha\"},{\"id\":3,\"name\":\"alpha\"},"
        "{\"id\":4,\"name\":\"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\"},{\"id\":5},{\"id\":6,\"name\":\"\"},"
        "{\"id\":7,\"name\":\"yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy\"}]");

    assert(yes && no && vendors);
    status_map_reset(&m);
    assert(!m.spoolman_edit);
    assert(status_map_apply_component_status(&m, yes));
    assert(m.spoolman_edit && m.spoolman_online);
    assert(status_map_apply_component_status(&m, no));
    assert(!m.spoolman_edit);

    assert(status_map_apply_vendor_list(&m, vendors));
    /* Generic first, then the host's three (Alpha, yyy...y - 31 bytes, it
     * fits - and zeta), then the curated brands it does not already name. */
    assert(m.vendor_count == 3 + 30);
    assert(strcmp(m.vendors[0], "Generic") == 0);
    assert(strcmp(m.vendors[1], "Alpha") == 0);
    assert(strlen(m.vendors[2]) == 31);
    assert(strcmp(m.vendors[3], "zeta") == 0);
    assert(strcmp(m.vendors[4], "Bambu Lab") == 0);
    assert(mmu_edit_vendor_count(&m) == m.vendor_count);   /* Generic is on the list now */
    assert(strcmp(m.vendors[32], "Das Filament") == 0);
    assert(!status_map_apply_vendor_list(&m, yes));   /* not an array */

    cJSON_Delete(yes);
    cJSON_Delete(no);
    cJSON_Delete(vendors);
}

/* A bay whose component_status reports "spool: null" has had its spool taken
 * away: the numbers of the removed spool go with it, so the slot reads like one
 * that never held a spool (which is what slot_clear() leaves behind). */
static void test_spool_removed(void)
{
    mmu_model_t m;
    cJSON *ui = cJSON_Parse(
        "{\"unit\":\"oams1\",\"family\":\"ams1\",\"bays\":[{\"s\":\"l\",\"sp\":{\"id\":7,\"src\":\"t\"}}],"
        "\"lane\":{\"id\":\"fps1\",\"op\":\"LOADED\",\"bay\":\"oams1-0\"}}");
    cJSON *with = cJSON_Parse(
        "{\"bays\":{\"oams1-0\":{\"spool\":{\"material\":\"PETG\",\"vendor\":\"Polymaker\","
        "\"color_hex\":\"d32f2f\",\"remaining_g\":640,\"remaining_pct\":64}}}}");
    cJSON *without = cJSON_Parse("{\"bays\":{\"oams1-0\":{\"spool\":null}}}");
    int u;
    mmu_slot_t *s;

    assert(ui && with && without);
    status_map_reset(&m);
    assert(status_map_apply_openams_ui(&m, ui));
    assert(status_map_apply_component_status(&m, with));

    u = find_unit_in(&m, "oams1");
    assert(u == 0);
    s = &m.units[u].slots[0];
    assert(strcmp(s->material, "PETG") == 0);
    assert(strcmp(s->brand, "Polymaker") == 0);
    assert(s->color == 0xd32f2fu && s->color_known);
    assert(s->remaining_g == 640);
    assert(s->remaining_pct == 64);

    /* The spool goes: everything it alone told us is gone too. */
    assert(status_map_apply_component_status(&m, without));
    assert(s->material[0] == '\0');
    assert(s->brand[0] == '\0');
    assert(s->color == 0 && !s->color_known);
    assert(s->remaining_g == -1);
    assert(s->remaining_pct == -1);

    /* A bay of its own right after reset() holds the same, so the two are
     * indistinguishable in the view. */
    {
        mmu_model_t fresh;
        cJSON *ui2 = cJSON_Parse(
            "{\"unit\":\"oams2\",\"family\":\"ams1\",\"bays\":[{\"s\":\"e\"}],"
            "\"lane\":{\"id\":\"fps1\",\"op\":\"UNLOADED\"}}");
        cJSON *null_spool = cJSON_Parse("{\"bays\":{\"oams2-0\":{\"spool\":null}}}");
        int f;

        assert(ui2 && null_spool);
        status_map_reset(&fresh);
        assert(status_map_apply_openams_ui(&fresh, ui2));
        assert(status_map_apply_component_status(&fresh, null_spool));
        f = find_unit_in(&fresh, "oams2");
        assert(f == 0);
        assert(strcmp(m.units[u].slots[0].material, fresh.units[f].slots[0].material) == 0);
        assert(m.units[u].slots[0].brand[0] == fresh.units[f].slots[0].brand[0]);
        assert(m.units[u].slots[0].color == fresh.units[f].slots[0].color);
        assert(m.units[u].slots[0].remaining_g == fresh.units[f].slots[0].remaining_g);
        assert(m.units[u].slots[0].remaining_pct == fresh.units[f].slots[0].remaining_pct);
        cJSON_Delete(ui2);
        cJSON_Delete(null_spool);
    }

    cJSON_Delete(ui);
    cJSON_Delete(with);
    cJSON_Delete(without);
}

/* `spool.initial_g` and `spool.flow_k` are the spool's own size and pressure
 * advance. Both are optional: absent, an explicit null or not a number is
 * unknown, which is -1, and the edit form then reads the size back from the
 * grams and the percent as it did before and starts the PA at Spoolman's
 * 0.020. A bay whose spool goes away loses them with the rest of the spool. */
static void test_spool_own_weight_and_pa(void)
{
    mmu_model_t m;
    cJSON *ui = cJSON_Parse(
        "{\"unit\":\"oams1\",\"family\":\"ams1\","
        "\"bays\":[{\"s\":\"r\"},{\"s\":\"r\"},{\"s\":\"r\"},{\"s\":\"r\"},{\"s\":\"r\"}],"
        "\"lane\":{\"id\":\"fps1\",\"op\":\"IDLE\"}}");
    cJSON *st = cJSON_Parse(
        "{\"bays\":{"
        /* both published, as the component's summary now carries them */
        "\"oams1-0\":{\"spool\":{\"material\":\"PLA\",\"vendor\":\"Generic\",\"color_hex\":\"1A73E8\","
        "\"remaining_g\":640,\"remaining_pct\":64,\"initial_g\":2000,\"flow_k\":0.045}},"
        /* an explicit null: the host has no value to give */
        "\"oams1-1\":{\"spool\":{\"material\":\"PLA\",\"remaining_g\":500,\"remaining_pct\":50,"
        "\"initial_g\":null,\"flow_k\":null}},"
        /* not a number: never something the form should prefill from */
        "\"oams1-2\":{\"spool\":{\"material\":\"PLA\",\"remaining_g\":250,\"remaining_pct\":25,"
        "\"initial_g\":\"2000\",\"flow_k\":\"0.045\"}},"
        /* an older component that sends neither */
        "\"oams1-3\":{\"spool\":{\"material\":\"PLA\",\"remaining_g\":200,\"remaining_pct\":20}},"
        /* a PA only rounding gets right: 1.001 * 1000 is 1000.9999999999999 */
        "\"oams1-4\":{\"spool\":{\"material\":\"PLA\",\"remaining_g\":100,\"remaining_pct\":20,"
        "\"flow_k\":1.001}}}}");
    cJSON *gone;
    mmu_unit_t *u;
    mmu_edit_defaults_t d;
    int unit;

    assert(ui && st);
    status_map_reset(&m);
    assert(status_map_apply_openams_ui(&m, ui));
    assert(status_map_apply_component_status(&m, st));
    unit = find_unit_in(&m, "oams1");
    assert(unit == 0);
    u = &m.units[unit];

    assert(u->slots[0].initial_g == 2000 && u->slots[0].flow_k_x1000 == 45);
    assert(u->slots[1].initial_g == -1 && u->slots[1].flow_k_x1000 == -1);
    assert(u->slots[2].initial_g == -1 && u->slots[2].flow_k_x1000 == -1);
    assert(u->slots[3].initial_g == -1 && u->slots[3].flow_k_x1000 == -1);
    assert(u->slots[4].initial_g == -1 && u->slots[4].flow_k_x1000 == 1001);

    /* what the form starts from: the spool's own size and PA when the host
     * sent them, the read-back and Spoolman's 0.020 when it did not */
    mmu_edit_defaults(&m, unit, 0, &d);
    assert(d.remaining_g == 640 && d.initial_g == 2000 && d.pa_x1000 == 45);
    mmu_edit_defaults(&m, unit, 1, &d);
    assert(d.remaining_g == 500 && d.initial_g == 1000 && d.pa_x1000 == 20);
    mmu_edit_defaults(&m, unit, 3, &d);
    assert(d.remaining_g == 200 && d.initial_g == 1000 && d.pa_x1000 == 20);
    mmu_edit_defaults(&m, unit, 4, &d);
    assert(d.initial_g == 500 && d.pa_x1000 == 1001);

    /* the spool goes: what it alone told us goes with it */
    gone = cJSON_Parse("{\"bays\":{\"oams1-0\":{\"spool\":null}}}");
    assert(gone);
    assert(status_map_apply_component_status(&m, gone));
    assert(u->slots[0].initial_g == -1 && u->slots[0].flow_k_x1000 == -1);
    mmu_edit_defaults(&m, unit, 0, &d);
    assert(d.remaining_g == 1000 && d.initial_g == 1000 && d.pa_x1000 == 20);

    /* outside the contract's range (a weight that does not fit the model, a
     * PA past 0-2): unknown, never wrapped into a value the form would show */
    {
        cJSON *bad = cJSON_Parse(
            "{\"bays\":{"
            "\"oams1-0\":{\"spool\":{\"material\":\"PLA\",\"remaining_g\":250,\"remaining_pct\":25,"
            "\"initial_g\":70000,\"flow_k\":5}},"
            "\"oams1-1\":{\"spool\":{\"material\":\"PLA\",\"remaining_g\":200,\"remaining_pct\":20,"
            "\"initial_g\":0,\"flow_k\":-0.1}}}}");

        assert(bad && status_map_apply_component_status(&m, bad));
        assert(u->slots[0].initial_g == -1 && u->slots[0].flow_k_x1000 == -1);
        assert(u->slots[1].initial_g == -1 && u->slots[1].flow_k_x1000 == -1);
        cJSON_Delete(bad);
    }

    cJSON_Delete(ui);
    cJSON_Delete(st);
    cJSON_Delete(gone);
}

/* An unloading lane publishes a stage exactly as a loading one does, and the
 * mapper keeps it: the plan's labels, the running step and the failed one. */
static void test_unload_stage(void)
{
    mmu_model_t m;
    cJSON *ui = cJSON_Parse(
        "{\"unit\":\"oams1\",\"family\":\"ams1\",\"bays\":[{\"s\":\"l\"},{\"s\":\"r\"}],"
        "\"lane\":{\"id\":\"fps1\",\"op\":\"UNLOADING\",\"bay\":\"oams1-0\","
        "\"stage\":[[\"Retract from extruder\",\"Pull back to the bay\",\"Park\"],1,\"Park\"]}}");
    cJSON *ui_load = cJSON_Parse(
        "{\"unit\":\"oams1\",\"family\":\"ams1\",\"bays\":[{\"s\":\"p\"},{\"s\":\"r\"}],"
        "\"lane\":{\"id\":\"fps1\",\"op\":\"LOADING\",\"bay\":\"oams1-0\","
        "\"stage\":[[\"cut\",\"feed\",\"park\"],0,\"feed\"]}}");
    int u;
    const mmu_toolhead_t *th;

    assert(ui && ui_load);
    status_map_reset(&m);
    assert(status_map_apply_openams_ui(&m, ui));

    u = find_unit_in(&m, "oams1");
    assert(u == 0);
    th = mmu_model_toolhead(&m, m.units[u].toolhead);
    assert(th);
    assert(th->busy == MMU_BUSY_UNLOAD);
    assert(th->step_count == 3);
    assert(strcmp(th->steps[0], "Retract from extruder") == 0);
    assert(strcmp(th->steps[1], "Pull back to the bay") == 0);
    assert(strcmp(th->steps[2], "Park") == 0);
    assert(th->step_current == 1);
    assert(th->step_failed == 2);   /* "Park" is plan entry 2 */

    /* The same shape while loading: the plan's step names are labeled the same
     * way, and `failed` names a plan entry, not a label. */
    assert(status_map_apply_openams_ui(&m, ui_load));
    th = mmu_model_toolhead(&m, m.units[u].toolhead);
    assert(th && th->busy == MMU_BUSY_LOAD);
    assert(th->step_count == 3);
    assert(strcmp(th->steps[0], "cut") == 0);
    assert(strcmp(th->steps[1], "feed") == 0);
    assert(strcmp(th->steps[2], "park") == 0);   /* a name the table does not know */
    assert(th->step_current == 0);
    assert(th->step_failed == 1);   /* "feed" is plan entry 1 */

    cJSON_Delete(ui);
    cJSON_Delete(ui_load);
}

/* The five-element `lane.stage` (H3): a label of the user's for a custom step
 * and for a standard one, and a detail for the running step. A three-element
 * array is an older printer and is read exactly as it always was. */
static void test_stage_labels_and_detail(void)
{
    mmu_model_t m;
    cJSON *ui = cJSON_Parse(
        "{\"unit\":\"oams1\",\"family\":\"ams1\",\"bays\":[{\"s\":\"l\"},{\"s\":\"r\"}],"
        "\"lane\":{\"id\":\"fps1\",\"op\":\"LOADING\",\"bay\":\"oams1-0\","
        "\"stage\":[[\"brushing\",\"heat\",\"home\",\"park\"],1,null,\"250 \\u00b0C\","
        "{\"brushing\":\"Brushing the nozzle\",\"heat\":\"Melting the tip\"}]}}");
    cJSON *old = cJSON_Parse(
        "{\"unit\":\"oams1\",\"family\":\"ams1\",\"bays\":[{\"s\":\"l\"},{\"s\":\"r\"}],"
        "\"lane\":{\"id\":\"fps1\",\"op\":\"LOADING\",\"bay\":\"oams1-0\","
        "\"stage\":[[\"cut\",\"feed\"],1,\"feed\"]}}");
    int u;
    const mmu_toolhead_t *th;

    assert(ui && old);
    status_map_reset(&m);
    assert(status_map_apply_openams_ui(&m, ui));

    u = find_unit_in(&m, "oams1");
    assert(u == 0);
    th = mmu_model_toolhead(&m, m.units[u].toolhead);
    assert(th);
    assert(th->step_count == 4);
    /* A label where there is one, a name where there is not. */
    assert(strcmp(th->steps[0], "Brushing the nozzle") == 0);
    assert(strcmp(th->steps[1], "Melting the tip") == 0);
    assert(strcmp(th->steps[2], "home") == 0);
    assert(strcmp(th->steps[3], "park") == 0);
    assert(th->step_current == 1);
    assert(th->step_failed == -1);
    assert(strcmp(th->step_detail, "250 \xc2\xb0" "C") == 0);

    /* An older printer's three elements: no labels, no detail, and the plan
     * read exactly as before. */
    assert(status_map_apply_openams_ui(&m, old));
    th = mmu_model_toolhead(&m, m.units[u].toolhead);
    assert(th);
    assert(th->step_count == 2);
    assert(strcmp(th->steps[0], "cut") == 0);
    assert(strcmp(th->steps[1], "feed") == 0);
    assert(th->step_current == 1);
    assert(th->step_failed == 1);
    assert(th->step_detail[0] == '\0');

    cJSON_Delete(ui);
    cJSON_Delete(old);
}

/* ------------------------------------------------- UTF-8-safe host text */

/* Every field the host's own words land in is a fixed buffer, and a real host
 * sends real names and real sentences: three bytes a character where English
 * spends one. The cut has to land on a character, or the model holds half a
 * glyph and every renderer draws it. web/logic/cstr.ts's cut() is the same
 * rule for the TypeScript port, and the differential suite feeds these same
 * strings through both.
 *
 * 32 bytes of unit name, 40 of bay id, 96 of fault text, 24 of material, 16 of
 * brand: each string below is chosen so the last character would not fit. */
static void test_host_text_cuts_on_a_character(void)
{
    mmu_model_t m;
    cJSON *obj;
    char bay[MMU_SLOT_ID_LEN];

    /* "oams" and ten glyphs is 34 bytes and the name field holds 31: the tenth
     * glyph's first byte would be the 32nd, and a half glyph is never left
     * behind, so the name ends after nine glyphs - 31 whole bytes. */
    obj = cJSON_Parse("{\"unit\":\"oams\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\",\"family\":\"ams1\","
                      "\"bays\":[{\"s\":\"p\"}],"
                      "\"lane\":{\"op\":\"UNLOADED\"}}");
    assert(obj);
    status_map_reset(&m);
    assert(status_map_apply_openams_ui(&m, obj));
    cJSON_Delete(obj);
    assert(m.unit_count == 1);
    assert(strcmp(m.units[0].name, "oams\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5") == 0);
    mmu_slot_id_in(&m, 0, 0, bay, sizeof bay);
    assert(strcmp(bay, "oams\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5" "1") == 0);

    /* A fault text of 32 CJK glyphs is 96 bytes and error_text[] holds 95 of
     * them. 93 fit and 94 does not fit whole either, so the sentence ends
     * after 31 glyphs (93 bytes): the field holds one fewer glyph than its
     * size allows, and never half of one. */
    {
        char json[300];
        char glyph[97];
        int i, n;

        for(i = 0; i < 32; i++) memcpy(glyph + i * 3, "\xe6\x97\xa5", 3);
        glyph[96] = '\0';
        n = snprintf(json, sizeof(json),
                     "{\"unit\":\"oams1\",\"family\":\"ams1\",\"bays\":[{\"s\":\"p\"}],"
                     "\"lane\":{\"op\":\"UNLOADED\"},\"fault\":{\"code\":\"E1\","
                     "\"text\":\"%s\",\"sev\":\"pause\"},\"nf\":1}", glyph);
        assert(n > 0 && n < (int)sizeof(json));
        obj = cJSON_Parse(json);
        assert(obj);
        status_map_reset(&m);
        assert(status_map_apply_openams_ui(&m, obj));
        cJSON_Delete(obj);
        assert(strlen(m.toolheads[0].error_text) == 93);
    }

    /* A material of 24 bytes fills its 24-byte field exactly; 25 would be one
     * byte too many, and the whole last glyph goes with it, so the field
     * holds 7 glyphs and not 8. A brand of 27 bytes into the same field ends
     * at 7 whole glyphs. */
    {
        char mat[25], brand[28];
        char json[256];
        int i, n;

        for(i = 0; i < 8; i++) memcpy(mat + i * 3, "\xe6\x97\xa5", 3);   /* 24 bytes */
        mat[24] = '\0';
        for(i = 0; i < 9; i++) memcpy(brand + i * 3, "\xe6\x9c\xac", 3); /* 27 bytes */
        brand[27] = '\0';
        n = snprintf(json, sizeof(json),
                     "{\"spoolman_online\":true,\"bays\":{\"oams1-0\":"
                     "{\"spool\":{\"material\":\"%s\",\"vendor\":\"%s\",\"remaining_g\":500}}}}",
                     mat, brand);
        assert(n > 0 && n < (int)sizeof(json));
        obj = cJSON_Parse(json);
        assert(obj);
        status_map_reset(&m);
        assert(status_map_apply_component_status(&m, obj));
        cJSON_Delete(obj);
        assert(strlen(m.units[0].slots[0].material) == 21);   /* 7 whole glyphs */
        assert(strlen(m.units[0].slots[0].brand) == 21);     /* 7 whole glyphs */
    }
}

int main(void)
{
    unsigned i;

    test_idle();
    test_loading();
    test_loaded();
    test_positioning();
    test_uncalibrated();
    test_error();
    test_drying();
    test_multi_unit();
    test_remote_unit();
    test_runout_reload();
    test_two_lanes();
    test_current_group();
    test_groups_follow_the_host();
    test_groups_in_name_order();
    test_motion_settled();
    test_settings();
    test_malformed();
    test_fault_actions();
    test_fault_dedup();
    test_fault_history();
    test_fault_null();
    test_spool_list();
    test_spool_list_archived_skipped();
    test_metadata_weights();
    test_metadata_shortfall();
    test_spool_removed();
    test_spool_own_weight_and_pa();
    test_color_known();
    test_edit_capability_and_vendors();
    test_unload_stage();
    test_stage_labels_and_detail();
    test_host_text_cuts_on_a_character();

    for(i = 0; i < sizeof k_fixtures / sizeof k_fixtures[0]; i++) {
        assert(replay(k_fixtures[i]) == k_flagged[i]);
    }

    printf("test_status_map: ok (%u fixtures)\n",
           (unsigned)(sizeof k_fixtures / sizeof k_fixtures[0]));
    return 0;
}
