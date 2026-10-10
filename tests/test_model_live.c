/* Live-data fields of the dummy model: the C2 additions (see
 * docs/contracts/C2_status.md 4.2/4.5/5.4/5.6). Plain C, plain assert. */

/* The default CMAKE_BUILD_TYPE here is Release, which puts -DNDEBUG on the
 * command line and would compile every assert() in this file away: the CTest
 * run would then pass without checking anything. Keep them live. */
#undef NDEBUG

#include "model/mmu_model.h"
#include "backend/status_map.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* The model logs group edits through the app's action log. The real one lives
 * in actions.c and drags in the UI, so the test links this no-op instead. */
void actions_log(const char *fmt, ...)
{
    (void)fmt;
}

/* ---------------------------------------------------------------- helpers */

/* Simulate what map_fault does when a new fault code arrives: push one entry
 * into m->alert_pending (the same logic map_fault uses). */
static void push_pending(mmu_model_t *m, const char *text, mmu_severity_t sev)
{
    int n = (int)m->alert_pending_count;

    if(n >= 4) return;
    memset(&m->alert_pending[n], 0, sizeof(m->alert_pending[n]));
    snprintf(m->alert_pending[n].text, sizeof(m->alert_pending[n].text), "%s", text);
    m->alert_pending[n].severity = sev;
    m->alert_pending[n].unit     = -1;
    m->alert_pending[n].toolhead = -1;
    m->alert_pending[n].unread   = true;
    m->alert_pending_count = (uint8_t)(n + 1);
}

/* ------------------------------------------------------------- merge tests */

/* A fault mapped into a shadow appears in the ui's history exactly once after
 * a merge (pending queue is drained, not duplicated). */
static void test_merge_basic(void)
{
    mmu_model_t shadow, ui;

    memset(&shadow, 0, sizeof(shadow));
    memset(&ui,     0, sizeof(ui));

    push_pending(&shadow, "Fault F001", MMU_SEV_PAUSE);
    assert(shadow.alert_pending_count == 1);
    assert(ui.alert_count == 0);

    mmu_model_merge_live(&ui, &shadow);

    assert(ui.alert_count == 1);
    assert(shadow.alert_pending_count == 0);
    assert(strstr(ui.alerts[0].text, "F001") != NULL);
}

/* The ui's read marks survive a later merge (the history block is preserved). */
static void test_merge_read_marks(void)
{
    mmu_model_t shadow, ui;

    memset(&shadow, 0, sizeof(shadow));
    memset(&ui,     0, sizeof(ui));

    /* First merge: F002 goes into ui's history. */
    push_pending(&shadow, "Fault F002", MMU_SEV_PAUSE);
    mmu_model_merge_live(&ui, &shadow);
    assert(ui.alert_count == 1);

    /* Mark the alert read. */
    ui.alerts[0].unread = false;
    ui.alert_unread = 0;

    /* Second merge: F003 arrives on the shadow. */
    push_pending(&shadow, "Fault F003", MMU_SEV_STOP);
    mmu_model_merge_live(&ui, &shadow);

    /* ui now holds both alerts; the read mark on the first is preserved. */
    assert(ui.alert_count == 2);
    /* Newest first: F003 is index 0, F002 is index 1. */
    assert(strstr(ui.alerts[0].text, "F003") != NULL);
    assert(!ui.alerts[1].unread);  /* F002's read mark kept */
}

/* A second fault with a new code adds exactly one more alert. */
static void test_merge_second_fault(void)
{
    mmu_model_t shadow, ui;

    memset(&shadow, 0, sizeof(shadow));
    memset(&ui,     0, sizeof(ui));

    push_pending(&shadow, "Fault F004", MMU_SEV_PAUSE);
    mmu_model_merge_live(&ui, &shadow);
    assert(ui.alert_count == 1);

    push_pending(&shadow, "Fault F005", MMU_SEV_STOP);
    mmu_model_merge_live(&ui, &shadow);
    assert(ui.alert_count == 2);
    assert(strstr(ui.alerts[0].text, "F005") != NULL);
}

/* mmu_model_merge_live() always leaves the globals equal to this unit's
 * toolhead (U1a), whether ui and live are the same model (the simulator's
 * self-merge) or different ones (the ESP32's shadow -> UI copy). */
static void test_merge_refreshes_this(void)
{
    mmu_model_t shadow, ui;
    cJSON *obj = cJSON_Parse(
        "{\"unit\":\"oams1\",\"family\":\"ams1\",\"bays\":[{\"s\":\"l\"}],"
        "\"lane\":{\"id\":\"fps1\",\"op\":\"LOADED\",\"bay\":\"oams1-0\"}}");
    const mmu_toolhead_t *th;

    assert(obj);
    status_map_reset(&shadow);
    memset(&ui, 0, sizeof(ui));

    assert(status_map_apply_openams_ui(&shadow, obj));
    assert(mmu_model_merge_live(&ui, &shadow));

    assert(ui.loaded && ui.loaded_unit == 0 && ui.loaded_slot == 0);

    th = mmu_model_toolhead(&ui, ui.units[ui.this_unit].toolhead);
    assert(th);
    assert(th->loaded == ui.loaded);
    assert(th->loaded_unit == ui.loaded_unit);
    assert(th->loaded_slot == ui.loaded_slot);
    assert(th->busy == ui.busy);
    assert(th->has_error == ui.has_error);
    assert(th->motion_settled == ui.motion_settled);

    /* The self-merge path (ui == live, as the simulator uses it) refreshes
     * just the same. */
    assert(mmu_model_merge_live(&shadow, &shadow));
    th = mmu_model_toolhead(&shadow, shadow.units[shadow.this_unit].toolhead);
    assert(th);
    assert(th->loaded_unit == shadow.loaded_unit);
    assert(th->loaded_slot == shadow.loaded_slot);

    cJSON_Delete(obj);
}

/* Low-filament alert fires once on the crossing, not again until recovery. */
static void test_low_alert(void)
{
    mmu_model_t m;
    const char *cs_high =
        "{\"spoolman_online\":false,\"bays\":{"
        "\"oams1-0\":{\"state\":\"linked\",\"spool_id\":1,\"source\":\"tag\","
        "\"pending_confirmation\":false,\"error\":null,"
        "\"spool\":{\"material\":\"PLA\",\"color_hex\":\"FF0000\","
        "\"vendor\":\"X\",\"remaining_g\":300,\"remaining_pct\":30}}}}";
    const char *cs_low =
        "{\"spoolman_online\":false,\"bays\":{"
        "\"oams1-0\":{\"state\":\"linked\",\"spool_id\":1,\"source\":\"tag\","
        "\"pending_confirmation\":false,\"error\":null,"
        "\"spool\":{\"material\":\"PLA\",\"color_hex\":\"FF0000\","
        "\"vendor\":\"X\",\"remaining_g\":50,\"remaining_pct\":5}}}}";
    const char *cs_low2 =
        "{\"spoolman_online\":false,\"bays\":{"
        "\"oams1-0\":{\"state\":\"linked\",\"spool_id\":1,\"source\":\"tag\","
        "\"pending_confirmation\":false,\"error\":null,"
        "\"spool\":{\"material\":\"PLA\",\"color_hex\":\"FF0000\","
        "\"vendor\":\"X\",\"remaining_g\":30,\"remaining_pct\":3}}}}";

    cJSON *o;
    status_map_reset(&m);

    /* Start at 30 % (above threshold): no alert. */
    o = cJSON_Parse(cs_high); assert(o);
    status_map_apply_component_status(&m, o); cJSON_Delete(o);
    assert(m.alert_pending_count == 0);

    /* Cross into < 15 %: one alert queued. */
    o = cJSON_Parse(cs_low); assert(o);
    status_map_apply_component_status(&m, o); cJSON_Delete(o);
    assert(m.alert_pending_count == 1);
    assert(m.alert_pending[0].kind == MMU_ALERT_LOW_FILAMENT);   /* the sentence is made by the view */
    assert(m.alert_pending[0].unit == 0 && m.alert_pending[0].arg[0] == 0);

    /* Still below 15 % (3 %): no second alert. */
    m.alert_pending_count = 0;
    o = cJSON_Parse(cs_low2); assert(o);
    status_map_apply_component_status(&m, o); cJSON_Delete(o);
    assert(m.alert_pending_count == 0);

    /* Recover above 15 %: no alert on recovery itself. */
    m.alert_pending_count = 0;
    o = cJSON_Parse(cs_high); assert(o);
    status_map_apply_component_status(&m, o); cJSON_Delete(o);
    assert(m.alert_pending_count == 0);

    /* Cross below 15 % again: one new alert. */
    o = cJSON_Parse(cs_low); assert(o);
    status_map_apply_component_status(&m, o); cJSON_Delete(o);
    assert(m.alert_pending_count == 1);
}

/* The component's `stop` marks a host with a whole-sequence stop; one that
 * sends none (an older component) has none. */
static void test_component_stop_flag(void)
{
    mmu_model_t m;
    cJSON *o;

    status_map_reset(&m);
    o = cJSON_Parse("{\"bays\":{},\"stop\":true}"); assert(o);
    status_map_apply_component_status(&m, o); cJSON_Delete(o);
    assert(m.spoolman_stop && !m.spoolman_edit);
    o = cJSON_Parse("{\"bays\":{},\"edit\":true}"); assert(o);
    status_map_apply_component_status(&m, o); cJSON_Delete(o);
    assert(!m.spoolman_stop && m.spoolman_edit);
}

/* Marking alerts read stamps the model clock on them, and a merge never runs
 * the clock backward. */
static void test_alert_clock(void)
{
    mmu_model_t shadow, ui;

    memset(&shadow, 0, sizeof shadow);
    memset(&ui, 0, sizeof ui);
    ui.now_ms = 5000;
    shadow.now_ms = 3000;
    push_pending(&shadow, "Fault F006", MMU_SEV_PAUSE);
    mmu_model_merge_live(&ui, &shadow);
    assert(ui.now_ms == 5000);                  /* the later clock wins */
    assert(ui.alerts[0].raised_ms == 5000);

    g_mmu = ui;
    g_mmu.now_ms = 9000;
    mmu_alerts_mark_read();
    assert(!g_mmu.alerts[0].unread && g_mmu.alerts[0].read_ms == 9000);
    g_mmu.now_ms = 9000 + MMU_ALERT_BADGE_READ_MS;
    assert(mmu_alert_badge_expired(&g_mmu, &g_mmu.alerts[0]));
}

/* Shortfall alert fires for known-but-insufficient groups; all-unknown
 * groups do not produce an alert. */
static void test_shortfall(void)
{
    mmu_model_t m;
    cJSON *meta;

    status_map_reset(&m);

    /* unit with two slots: slot 0 = 60 g (known), slot 1 = unknown. */
    m.unit_count = 1;
    snprintf(m.units[0].name, sizeof m.units[0].name, "oams1");
    m.units[0].slot_count = 3;
    m.units[0].slots[0].remaining_g = 60;
    m.units[0].slots[1].remaining_g = -1; /* unknown */
    m.units[0].slots[2].remaining_g = -1; /* unknown */

    /* T0 has slot 0 (known 60 g) + slot 1 (unknown): partially known. */
    m.group_count = 2;
    snprintf(m.groups[0].name, sizeof m.groups[0].name, "T0");
    m.groups[0].count = 2;
    m.groups[0].members[0].unit = 0; m.groups[0].members[0].slot = 0;
    m.groups[0].members[1].unit = 0; m.groups[0].members[1].slot = 1;
    /* T1 has slot 2 only: all-unknown. */
    snprintf(m.groups[1].name, sizeof m.groups[1].name, "T1");
    m.groups[1].count = 1;
    m.groups[1].members[0].unit = 0; m.groups[1].members[0].slot = 2;

    /* T0 needs 100 g, T1 needs 50 g. */
    meta = cJSON_Parse("{\"filament_weights\":[100,50]}");
    assert(meta);
    assert(status_map_apply_metadata(&m, meta));
    cJSON_Delete(meta);

    /* T0: 60 g known < 100 g needed → shortfall. */
    assert(m.job_short & 0x01u);
    /* T1: all-unknown → no shortfall marked. */
    assert(!(m.job_short & 0x02u));
    /* One pending alert for T0. */
    assert(m.alert_pending_count == 1);

    /* With exactly enough: no shortfall for T0. */
    m.alert_pending_count = 0;
    m.job_short = 0;
    m.units[0].slots[0].remaining_g = 100;
    meta = cJSON_Parse("{\"filament_weights\":[100,0]}");
    assert(meta);
    status_map_apply_metadata(&m, meta);
    cJSON_Delete(meta);
    assert(m.job_short == 0);
    assert(m.alert_pending_count == 0);
}

/* Everything a slot carries when the backend has told us nothing beyond the
 * link: no Spoolman id, no weight, no pending prompt, calibrated, at rest. A
 * tag read is the one source that fills spool_id/remaining_g with dummy
 * facts, so those two are checked by the caller. */
static void check_slot_at_rest(const mmu_slot_t *slot)
{
    assert(!slot->pending_confirmation);
    assert(slot->cal_state == MMU_CAL_CONFIGURED);
    assert(!slot->positioning);

    if(slot->info_source != MMU_INFO_TAG) {
        assert(slot->spool_id == -1);
        assert(slot->remaining_g == -1);
    }
}

int main(void)
{
    int i, j;

    mmu_model_init();

    /* --- the freshly initialized model ---------------------------------- */
    assert(g_mmu.motion_settled);   /* idle: no load running, no error latched */
    assert(g_mmu.spoolman_online);
    assert(g_mmu.unit_count > 0);

    for(i = 0; i < g_mmu.unit_count; i++) {
        const mmu_unit_t *u = &g_mmu.units[i];

        assert(u->normal_motion);         /* motion-verified unless told otherwise */
        assert(!u->follower_uncertain);

        for(j = 0; j < u->slot_count; j++) {
            const mmu_slot_t *s = &u->slots[j];

            check_slot_at_rest(s);
            if(s->info_source == MMU_INFO_TAG) {
                assert(s->spool_id > 0);
                assert(s->remaining_g >= 0 && s->remaining_g <= 1000);
            }
        }
    }

    /* --- every named scenario ------------------------------------------- */
    for(i = 0; i < mmu_model_scenario_count(); i++) {
        const char *name = mmu_model_scenario_name(i);

        mmu_model_set_scenario(name);
        assert(strcmp(g_mmu.scenario_name, name) == 0);

        /* C2 4.2/5.6, as the dummy model stands it: motion is settled exactly
         * when no load/unload is running and no error is latched. */
        assert(g_mmu.motion_settled ==
               (g_mmu.busy == MMU_BUSY_NONE && !g_mmu.has_error));

        for(j = 0; j < g_mmu.unit_count; j++) {
            const mmu_unit_t *u = &g_mmu.units[j];
            int k;

            for(k = 0; k < u->slot_count; k++) {
                const mmu_slot_t *s = &u->slots[k];

                if(s->info_source == MMU_INFO_TAG) {
                    assert(s->spool_id > 0);
                    if(s->remaining_pct >= 0) {
                        assert(s->remaining_g == 10 * s->remaining_pct);
                    }
                }
                assert(s->remaining_g == -1 ||
                       (s->remaining_g >= 0 && s->remaining_g <= 1000));
            }
        }
    }

    test_merge_basic();
    test_merge_read_marks();
    test_merge_second_fault();
    test_merge_refreshes_this();
    test_low_alert();
    test_shortfall();
    test_component_stop_flag();
    test_alert_clock();

    printf("test_model_live: ok\n");
    return 0;
}
