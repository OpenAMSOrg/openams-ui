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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
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

/* Retry sent (core_retry_sent) holds until the host answers that retry: an
 * action_result of kind 3 (MMU_ACTION_RETRY) is taken and ends it; a kind
 * past it is refused and changes nothing. */
static void test_retry_sent_and_answered(void)
{
    mmu_model_t m;
    core_t *core;
    cJSON *lanes = cJSON_Parse(
        "{\"lanes_by_fps\":{\"fps1\":{\"toolchange_fault\":{\"kind\":\"load\",\"group\":\"T1\","
        "\"slot\":null,\"temp\":null,\"step\":\"feed\",\"reason\":\"No progress\","
        "\"panel\":false,\"fed\":false,\"since\":3.0}}}}");
    cJSON *answer = cJSON_Parse("{\"kind\":3,\"ok\":false,\"message\":\"a fault stayed latched\"}");
    cJSON *past = cJSON_Parse("{\"kind\":4,\"ok\":true,\"message\":\"\"}");

    assert(lanes && answer && past);
    status_map_reset(&m);
    core = core_create(&m);
    assert(core);
    assert(core_apply(core, "oams_manager", NULL, lanes));
    assert(m.toolhead_count == 1 && m.toolheads[0].tc_fault);
    assert(core_retry_sent(core, "fps1") && m.toolheads[0].tc_retry_sent);
    assert(!core_retry_sent(core, NULL));
    assert(!core_apply(core, "action_result", NULL, past));
    assert(m.toolheads[0].tc_retry_sent);
    assert(core_apply(core, "action_result", NULL, answer));
    assert(!m.toolheads[0].tc_retry_sent);

    core_destroy(core);
    cJSON_Delete(lanes);
    cJSON_Delete(answer);
    cJSON_Delete(past);
}

/* The two-tag attribution (`tg`, `tc`, `tr`, `to` per bay) rides on `bays`:
 * a diff without `bays` keeps it, a new `bays` replaces it whole, and a bay
 * that stops carrying `tg` has none (core_apply maps the merged copy). */
static void test_tag_read_rides_on_bays(void)
{
    mmu_model_t m;
    core_t *core;
    cJSON *full = cJSON_Parse(
        "{\"unit\":\"oams1\",\"idx\":1,\"family\":\"ams1\",\"act\":[\"rfid_scan\"],"
        "\"bays\":[{\"id\":0,\"s\":\"r\",\"tg\":\"c\",\"tc\":[{\"m\":\"PLA\",\"c\":\"D23B3B\"}]},"
        "{\"id\":1,\"s\":\"r\",\"tg\":\"x\",\"tr\":\"claimed_twice\",\"to\":0}],"
        "\"lane\":{\"id\":\"fps1\",\"op\":\"UNLOADED\"}}");
    cJSON *diff = cJSON_Parse("{\"lane\":{\"id\":\"fps1\",\"op\":\"UNLOADED\",\"runout\":\"idle\"}}");
    cJSON *bays = cJSON_Parse(
        "{\"bays\":[{\"id\":0,\"s\":\"r\",\"tg\":\"a\",\"sp\":{\"id\":4,\"src\":\"t\"}},"
        "{\"id\":1,\"s\":\"r\"}]}");

    assert(full && diff && bays);
    status_map_reset(&m);
    core = core_create(&m);
    assert(core);

    assert(core_apply(core, "openams_ui", "oams1", full));
    assert(m.units[0].slots[0].tag_read == MMU_TAG_READ_CANDIDATE);
    assert(m.units[0].slots[0].tag_candidate_count == 1);
    assert(m.units[0].slots[1].tag_read == MMU_TAG_READ_CONFLICT);
    assert(m.units[0].slots[1].tag_other == 0);

    assert(core_apply(core, "openams_ui", "oams1", diff));
    assert(m.units[0].slots[0].tag_read == MMU_TAG_READ_CANDIDATE);
    assert(m.units[0].slots[0].tag_candidate_count == 1);
    assert(m.units[0].slots[0].tag_candidates[0].color == 0xD23B3B);
    assert(m.units[0].slots[1].tag_reason == MMU_TAG_REASON_CLAIMED_TWICE);

    assert(core_apply(core, "openams_ui", "oams1", bays));
    assert(m.units[0].slots[0].tag_read == MMU_TAG_READ_ATTRIBUTED);
    assert(m.units[0].slots[0].tag_candidate_count == 0);
    assert(m.units[0].slots[0].info_source == MMU_INFO_TAG);
    assert(m.units[0].slots[1].tag_read == MMU_TAG_READ_NONE);
    assert(m.units[0].slots[1].tag_reason == MMU_TAG_REASON_OTHER);
    assert(m.units[0].slots[1].tag_other == -1);

    core_destroy(core);
    cJSON_Delete(full);
    cJSON_Delete(diff);
    cJSON_Delete(bays);
}

/* cJSON's allocator, counted: the blocks it has live. Whatever the core
 * still holds after the test frees its own input is the core's. */
static long s_live;

static void *count_malloc(size_t n)
{
    void *p = malloc(n);

    if(p) s_live++;
    return p;
}

static void count_free(void *p)
{
    if(p) s_live--;
    free(p);
}

static void count_heap(bool on)
{
    cJSON_Hooks hooks = { count_malloc, count_free };

    cJSON_InitHooks(on ? &hooks : NULL);
    s_live = 0;
}

/* The kinds that arrive whole keep no copy (the 4 inch board has no heap to
 * spare for the Spoolman status, about 7 KB with eight bays), still reach the
 * model every time, and report a change only when the content changed. */
static void test_whole_kinds_keep_no_copy(void)
{
    static const char *const status_a =
        "{\"schema_version\":1,\"edit\":true,\"stop\":true,\"spoolman_online\":true,"
        "\"queued_writes\":0,\"bays\":{"
        "\"oams1-0\":{\"state\":\"linked\",\"spool_id\":10,\"source\":\"manual\","
        "\"pending_confirmation\":false,\"error\":null,"
        "\"spool\":{\"material\":\"ABS\",\"color_hex\":\"000000\",\"vendor\":\"Acme\",\"remaining_g\":640}},"
        "\"oams1-1\":{\"state\":\"unlinked\",\"spool_id\":null,\"source\":\"none\","
        "\"pending_confirmation\":false,\"error\":null,\"spool\":null}}}";
    static const char *const status_b =
        "{\"schema_version\":1,\"edit\":true,\"stop\":true,\"spoolman_online\":false,"
        "\"queued_writes\":0,\"bays\":{}}";
    static const char *const spools =
        "[{\"id\":10,\"archived\":false,\"remaining_weight\":640,"
        "\"filament\":{\"material\":\"ABS\",\"color_hex\":\"000000\",\"vendor\":{\"name\":\"Acme\"}}}]";
    mmu_model_t m;
    core_t *core;
    cJSON *obj;

    status_map_reset(&m);
    core = core_create(&m);
    assert(core);
    count_heap(true);

    obj = cJSON_Parse(status_a);
    assert(obj);
    assert(core_apply(core, "component_status", NULL, obj) == true);
    assert(m.spoolman_online && m.spoolman_edit && m.spoolman_stop);
    /* the same status again: mapped, but nothing changed */
    m.spoolman_online = false;
    assert(core_apply(core, "component_status", NULL, obj) == false);
    assert(m.spoolman_online);
    cJSON_Delete(obj);
    assert(s_live == 0);

    obj = cJSON_Parse(status_b);
    assert(obj);
    assert(core_apply(core, "component_status", NULL, obj) == true);
    assert(!m.spoolman_online);
    cJSON_Delete(obj);
    assert(s_live == 0);

    obj = cJSON_Parse(spools);
    assert(obj);
    assert(core_apply(core, "spool_list", NULL, obj) == true);
    assert(core_apply(core, "spool_list", NULL, obj) == false);
    cJSON_Delete(obj);
    assert(s_live == 0);

    count_heap(false);
    core_destroy(core);
}

/* On a dropped socket the copies are freed (net_link.c), but the model, and
 * so the screen, keeps the units' last state; the reconnect's reset and
 * subscribe result then rebuild them. */
static void test_drop_copies_keeps_model(void)
{
    static const char *const full =
        "{\"unit\":\"oams1\",\"idx\":2,\"family\":\"ams1\","
        "\"bays\":[{\"s\":\"l\"},{\"s\":\"r\"}],\"act\":[\"rfid_scan\"],"
        "\"lane\":{\"id\":\"fps1\",\"op\":\"LOADED\",\"bay\":\"oams1-0\"}}";
    mmu_model_t m;
    core_t *core;
    cJSON *obj;

    status_map_reset(&m);
    core = core_create(&m);
    assert(core);
    count_heap(true);

    obj = cJSON_Parse(full);
    assert(obj);
    assert(core_apply(core, "openams_ui", "oams1", obj));
    cJSON_Delete(obj);
    assert(s_live > 0);   /* a kind sent as diffs keeps its merged copy */

    core_drop_copies(core);
    assert(s_live == 0);
    assert(m.unit_count == 1);
    assert(m.units[0].slot_count == 2);
    assert(m.units[0].host_idx == 2);
    core_drop_copies(core);   /* twice is harmless */

    /* the reconnect: READY resets, then the subscribe result applies */
    core_reset(core, true);
    obj = cJSON_Parse(full);
    assert(obj);
    assert(core_apply(core, "openams_ui", "oams1", obj));
    assert(m.unit_count == 1 && m.units[0].slot_count == 2);
    cJSON_Delete(obj);

    core_destroy(core);
    assert(s_live == 0);
    count_heap(false);
    core_drop_copies(NULL);
}

int main(void)
{
    test_merge_survives_diff();
    test_second_unit_created();
    test_lanes_sets_pressure();
    test_reset();
    test_changed_second_time();
    test_component_status_keeps_applying();
    test_retry_sent_and_answered();
    test_tag_read_rides_on_bays();
    test_whole_kinds_keep_no_copy();
    test_drop_copies_keeps_model();

    printf("test_core: ok\n");
    return 0;
}
