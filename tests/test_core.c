/**
 * @file test_core.c
 * The merge-owning path (core.h): a diff without `bays` must not erase what
 * the model already knew, lanes_by_fps must reach the toolheads, and reset
 * must behave exactly as net_link.c and the simulator rely on it to.
 *
 * Plain C, plain assert, no framework.
 */

/* The default CMAKE_BUILD_TYPE here is Release, which puts -DNDEBUG on the
 * command line and would compile every assert() in this file away: the CTest
 * run would then pass without checking anything. Keep them live. */
#undef NDEBUG

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "core/core.h"
#include "backend/status_map.h"
#include "lib/cjson/cJSON.h"
#include "model/mmu_model.h"

/* mmu_model.c logs group edits through actions.c; the test does not care. */
void actions_log(const char *fmt, ...)
{
    (void)fmt;
}

/* A diff carrying only `{"lane": {...}}` must not erase `bays`, `idx` or
 * `act`: core_apply merges it into the owned copy first and maps the whole
 * merged copy, not the diff on its own. */
static void test_merge_survives_diff(void)
{
    mmu_model_t m;
    core_t *core;
    cJSON *full = cJSON_Parse(
        "{\"unit\":\"oams1\",\"idx\":2,\"family\":\"ams1\","
        "\"bays\":[{\"s\":\"l\"},{\"s\":\"r\"}],"
        "\"act\":[\"rfid_scan\"],"
        "\"lane\":{\"id\":\"fps1\",\"op\":\"LOADED\",\"bay\":\"oams1-0\"}}");
    cJSON *diff = cJSON_Parse("{\"lane\":{\"id\":\"fps1\",\"op\":\"UNLOADED\"}}");

    assert(full && diff);
    status_map_reset(&m);
    core = core_create(&m);
    assert(core);

    assert(core_apply(core, "openams_ui", "oams1", full));
    assert(m.unit_count == 1);
    assert(m.units[0].host_idx == 2);
    assert(m.units[0].slot_count == 2);
    assert(m.units[0].rfid);
    assert(m.loaded && m.loaded_unit == 0 && m.loaded_slot == 0);

    assert(core_apply(core, "openams_ui", "oams1", diff));

    /* survived the diff: bays, idx and act came from the full object, which
     * this diff's merge did not touch */
    assert(m.units[0].host_idx == 2);
    assert(m.units[0].slot_count == 2);
    assert(m.units[0].rfid);
    /* landed: the lane change the diff carried */
    assert(!m.loaded);
    assert(m.busy == MMU_BUSY_NONE);

    core_destroy(core);
    cJSON_Delete(full);
    cJSON_Delete(diff);
}

/* A diff naming a unit core has not seen yet creates it, exactly like a full
 * object would (there is nothing to merge into, so the diff becomes the
 * whole of what is known so far). */
static void test_second_unit_created(void)
{
    mmu_model_t m;
    core_t *core;
    cJSON *a = cJSON_Parse(
        "{\"unit\":\"oams1\",\"bays\":[{\"s\":\"l\"}],"
        "\"lane\":{\"id\":\"fps1\",\"op\":\"LOADED\",\"bay\":\"oams1-0\"}}");
    cJSON *b = cJSON_Parse(
        "{\"unit\":\"oams2\",\"bays\":[{\"s\":\"r\"},{\"s\":\"r\"}],"
        "\"lane\":{\"id\":\"fps2\",\"op\":\"UNLOADED\"}}");

    assert(a && b);
    status_map_reset(&m);
    core = core_create(&m);
    assert(core);

    assert(core_apply(core, "openams_ui", "oams1", a));
    assert(m.unit_count == 1);

    assert(core_apply(core, "openams_ui", "oams2", b));
    assert(m.unit_count == 2);
    assert(strcmp(m.units[1].name, "oams2") == 0);
    assert(m.units[1].slot_count == 2);
    /* different lanes, so a different toolhead */
    assert(m.units[0].toolhead != m.units[1].toolhead);

    core_destroy(core);
    cJSON_Delete(a);
    cJSON_Delete(b);
}

/* oams_manager.lanes_by_fps sets each named toolhead's pressure and
 * set_point (creating the toolhead when it is not known yet), leaving a
 * missing field at -1 rather than touching nothing. */
static void test_lanes_sets_pressure(void)
{
    mmu_model_t m;
    core_t *core;
    cJSON *obj = cJSON_Parse(
        "{\"lanes_by_fps\":{"
        "\"fps1\":{\"pressure\":0.42,\"set_point\":0.5},"
        "\"fps2\":{\"pressure\":0.1}}}");
    const mmu_toolhead_t *th1, *th2;

    assert(obj);
    status_map_reset(&m);
    core = core_create(&m);
    assert(core);

    assert(core_apply(core, "oams_manager", NULL, obj));

    assert(m.toolhead_count == 2);
    th1 = mmu_model_toolhead(&m, 0);
    th2 = mmu_model_toolhead(&m, 1);
    assert(th1 && strcmp(th1->id, "fps1") == 0);
    assert(th1->pressure > 0.41f && th1->pressure < 0.43f);
    assert(th1->set_point > 0.49f && th1->set_point < 0.51f);
    assert(th2 && strcmp(th2->id, "fps2") == 0);
    assert(th2->pressure > 0.09f && th2->pressure < 0.11f);
    assert(th2->set_point == -1.0f); /* absent from this lane's object */

    core_destroy(core);
    cJSON_Delete(obj);
}

/* core_reset(true) drops every owned copy and every unit, but keeps the
 * alert history; core_reset(false) drops both. */
static void test_reset(void)
{
    mmu_model_t m;
    core_t *core;
    cJSON *obj = cJSON_Parse(
        "{\"unit\":\"oams1\",\"bays\":[{\"s\":\"l\"}],"
        "\"lane\":{\"id\":\"fps1\",\"op\":\"LOADED\",\"bay\":\"oams1-0\"}}");

    assert(obj);
    status_map_reset(&m);
    core = core_create(&m);
    assert(core);

    assert(core_apply(core, "openams_ui", "oams1", obj));
    assert(m.unit_count == 1);
    mmu_alerts_push_to(&m, "test alert", MMU_SEV_INFO, -1);
    assert(m.alert_count == 1);

    core_reset(core, true);
    assert(m.unit_count == 0);
    assert(m.alert_count == 1);   /* kept */

    /* The owned copy is gone too, so re-applying the same object creates the
     * unit fresh rather than merging into anything left over. */
    assert(core_apply(core, "openams_ui", "oams1", obj));
    assert(m.unit_count == 1);
    assert(m.alert_count == 1);   /* still just the one pushed above */

    core_reset(core, false);
    assert(m.unit_count == 0);
    assert(m.alert_count == 0);   /* dropped */

    core_destroy(core);
    cJSON_Delete(obj);
}

/* The same object applied twice: nothing in the owned copy changed the
 * second time, so core_apply reports it. */
static void test_changed_second_time(void)
{
    mmu_model_t m;
    core_t *core;
    cJSON *obj = cJSON_Parse(
        "{\"unit\":\"oams1\",\"bays\":[{\"s\":\"l\"}],"
        "\"lane\":{\"id\":\"fps1\",\"op\":\"LOADED\",\"bay\":\"oams1-0\"}}");

    assert(obj);
    status_map_reset(&m);
    core = core_create(&m);
    assert(core);

    assert(core_apply(core, "openams_ui", "oams1", obj) == true);
    assert(core_apply(core, "openams_ui", "oams1", obj) == false);

    core_destroy(core);
    cJSON_Delete(obj);
}

/* component_status is 16 characters. When the kind buffer held 15, the
 * stored kind never matched again, every apply took a new entry, and after
 * the table filled the core dropped every later update: the Spoolman
 * component's online, edit and stop flags froze. Many updates in a row must
 * keep reaching the model. */
static void test_component_status_keeps_applying(void)
{
    mmu_model_t m;
    core_t *core;
    int i;

    status_map_reset(&m);
    core = core_create(&m);
    assert(core);

    for(i = 0; i < 40; i++) {
        cJSON *obj = cJSON_Parse((i % 2) ? "{\"spoolman_online\":true,\"edit\":true,\"stop\":true,\"bays\":{}}"
                                         : "{\"spoolman_online\":false,\"edit\":false,\"stop\":false,\"bays\":{}}");

        assert(obj);
        assert(core_apply(core, "component_status", NULL, obj) == true);
        assert(m.spoolman_online == (i % 2 == 1));
        assert(m.spoolman_stop == (i % 2 == 1));
        cJSON_Delete(obj);
    }

    core_destroy(core);
}

int main(void)
{
    test_merge_survives_diff();
    test_second_unit_created();
    test_lanes_sets_pressure();
    test_reset();
    test_changed_second_time();
    test_component_status_keeps_applying();

    printf("test_core: ok\n");
    return 0;
}
