/**
 * @file test_view.c
 * The view builders (src/view/view.h): the label/spare rules, ink, low/pct
 * text, pressure levels, a disabled action's reason, the on-demand shortfall,
 * the unit letter, tile actions and forms, the language tables and their
 * fallback to English, and that no builder allocates and no English string is
 * cut (docs/design/UNIFIED_UI.md 4, 9).
 *
 * Plain C, plain assert, no framework. The older tests start from
 * status_map_reset(&g_mmu) and poke the fields they need directly; the
 * U1c-fix1 tests (unit title/letter, sublabel, subtitle, adapter, tile
 * actions, local-model-only) build local models and never touch g_mmu, apart
 * from the one test that fills it with a different scenario on purpose.
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
#include "core/core.h"
#include "lib/cjson/cJSON.h"
#include "model/mmu_model.h"
#include "view/strings.h"
#include "view/view.h"
#include "view/view_json.h"

#ifndef VIEW_FIXTURE_DIR
#error "compile this test with -DVIEW_FIXTURE_DIR=<abs path to tests/fixtures/recorded>"
#endif

/* mmu_model.c logs group edits through actions.c; the test does not care. */
void actions_log(const char *fmt, ...)
{
    (void)fmt;
}

/* ------------------------------------------------------------------ setup */

/* A slot with fresh, "unknown" defaults (status_map.c's own slot_clear(),
 * mirrored: it is file-static there). */
static void reset_slot(mmu_slot_t *s)
{
    memset(s, 0, sizeof(*s));
    s->remaining_pct = -1;
    s->nozzle_min_c = -1;
    s->nozzle_max_c = -1;
    s->k_value_x1000 = -1;
    s->spool_id = -1;
    s->remaining_g = -1;
    s->state = MMU_SLOT_EMPTY;
    s->info_source = MMU_INFO_UNKNOWN;
    s->cal_state = MMU_CAL_UNKNOWN;
    s->global_id = -1;
}

/* One unit named @p name with @p n fresh slots, connected. Returns its
 * index (always 0..MMU_MAX_UNITS-1 here; the tests only ever add one or two). */
static int add_unit_to(mmu_model_t *m, const char *name, int n)
{
    int idx = (int)m->unit_count;
    mmu_unit_t *u = &m->units[idx];
    int i;

    memset(u, 0, sizeof(*u));
    snprintf(u->name, sizeof(u->name), "%s", name);
    u->kind = MMU_UNIT_4SLOT;
    u->variant = MMU_VARIANT_AMS;
    u->connected = true;
    u->humidity_pct = -1;
    u->temp_c = -100;
    u->host_idx = -1;
    u->toolhead = -1;
    u->slot_count = (uint8_t)n;
    for(i = 0; i < n; i++) reset_slot(&u->slots[i]);
    m->unit_count++;
    return idx;
}

static int add_unit(const char *name, int n)
{
    return add_unit_to(&g_mmu, name, n);
}

/* -------------------------------------------------------------- ink tests */

static void test_ink(void)
{
    view_tile_t t;

    status_map_reset(&g_mmu);
    add_unit("A", 1);
    g_mmu.units[0].slots[0].state = MMU_SLOT_READY;
    g_mmu.units[0].slots[0].info_source = MMU_INFO_MANUAL;
    snprintf(g_mmu.units[0].slots[0].material, sizeof(g_mmu.units[0].slots[0].material), "PLA");

    g_mmu.units[0].slots[0].color_known = true;

    g_mmu.units[0].slots[0].color = 0x000000; /* black */
    assert(view_tile(&g_mmu, 0, 0, &t));
    assert(t.ink == VIEW_INK_LIGHT);

    g_mmu.units[0].slots[0].color = 0xFFFFFF; /* white */
    assert(view_tile(&g_mmu, 0, 0, &t));
    assert(t.ink == VIEW_INK_DARK);

    g_mmu.units[0].slots[0].color = 0x808080; /* mid gray: luminance 128 <= 160 */
    assert(view_tile(&g_mmu, 0, 0, &t));
    assert(t.ink == VIEW_INK_LIGHT);
}

/* ---------------------------------------------- unknown color versus black */

/* A spool with no known color has no color to emit (the renderers draw the
 * neutral surface); a spool that is known to be black still emits #000000. */
static void test_color_known(void)
{
    view_tile_t t;
    mmu_slot_t *s;
    cJSON *root;
    const cJSON *bay;

    status_map_reset(&g_mmu);
    add_unit("A", 1);
    s = &g_mmu.units[0].slots[0];
    s->state = MMU_SLOT_READY;
    s->info_source = MMU_INFO_MANUAL;
    snprintf(s->material, sizeof(s->material), "PLA");

    /* nobody told us the color */
    assert(view_tile(&g_mmu, 0, 0, &t));
    assert(!t.color_known);
    assert(t.ink == VIEW_INK_LIGHT);
    root = view_json_build(&g_mmu);
    bay = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(
              cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "unassigned_units"), 0),
              "bays"), 0);
    assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(bay, "color")));
    assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(bay, "gradient_top")));
    assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(bay, "gradient_bottom")));
    cJSON_Delete(root);

    /* known to be black */
    s->color = 0x000000;
    s->color_known = true;
    assert(view_tile(&g_mmu, 0, 0, &t));
    assert(t.color_known && t.color == 0x000000);
    root = view_json_build(&g_mmu);
    bay = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(
              cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "unassigned_units"), 0),
              "bays"), 0);
    assert(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(bay, "color")));
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(bay, "color")->valuestring, "#000000") == 0);
    cJSON_Delete(root);
}

/* -------------------------------------------------------- low / pct_text */

static void test_low_and_pct_text(void)
{
    view_tile_t t;

    status_map_reset(&g_mmu);
    add_unit("A", 1);
    g_mmu.units[0].slots[0].state = MMU_SLOT_READY;
    g_mmu.units[0].slots[0].info_source = MMU_INFO_MANUAL;

    g_mmu.units[0].slots[0].remaining_pct = 14;
    assert(view_tile(&g_mmu, 0, 0, &t));
    assert(t.low == true);
    assert(strcmp(t.pct_text, "14%") == 0);

    g_mmu.units[0].slots[0].remaining_pct = 15;
    assert(view_tile(&g_mmu, 0, 0, &t));
    assert(t.low == false);
    assert(strcmp(t.pct_text, "15%") == 0);

    g_mmu.units[0].slots[0].remaining_pct = -1;
    assert(view_tile(&g_mmu, 0, 0, &t));
    assert(t.low == false);
    assert(strcmp(t.pct_text, "?") == 0);
}

/* ---------------------------------------------------- label / spare rules */

static void test_label_and_spare(void)
{
    view_tile_t loaded, spare1, spare2, ungrouped;

    status_map_reset(&g_mmu);
    add_unit("A", 4);

    /* bay 0 loaded, bays 1 and 2 ready spares, bay 3 ungrouped. */
    g_mmu.units[0].slots[0].state = MMU_SLOT_LOADED;
    g_mmu.units[0].slots[0].info_source = MMU_INFO_MANUAL;
    g_mmu.units[0].slots[1].state = MMU_SLOT_READY;
    g_mmu.units[0].slots[1].info_source = MMU_INFO_MANUAL;
    g_mmu.units[0].slots[1].remaining_pct = 80;
    g_mmu.units[0].slots[2].state = MMU_SLOT_READY;
    g_mmu.units[0].slots[2].info_source = MMU_INFO_MANUAL;
    g_mmu.units[0].slots[2].remaining_pct = 80;
    g_mmu.units[0].slots[3].state = MMU_SLOT_READY;
    g_mmu.units[0].slots[3].info_source = MMU_INFO_MANUAL;

    snprintf(g_mmu.groups[0].name, sizeof(g_mmu.groups[0].name), "T0");
    g_mmu.groups[0].count = 3;
    g_mmu.groups[0].members[0].unit = 0; g_mmu.groups[0].members[0].slot = 0;
    g_mmu.groups[0].members[1].unit = 0; g_mmu.groups[0].members[1].slot = 1;
    g_mmu.groups[0].members[2].unit = 0; g_mmu.groups[0].members[2].slot = 2;
    g_mmu.group_count = 1;

    assert(view_tile(&g_mmu, 0, 0, &loaded));
    assert(loaded.has_tool && strcmp(loaded.tool, "T0") == 0);
    assert(strcmp(loaded.label, "T0") == 0);
    assert(loaded.has_sublabel && strcmp(loaded.sublabel, "Spool 1") == 0);
    assert(loaded.spare.present == true);
    assert(loaded.spare.bare == true);   /* the bare mark: a usable spare exists */

    assert(view_tile(&g_mmu, 0, 1, &spare1));
    assert(spare1.spare.present && !spare1.spare.bare && spare1.spare.position == 1);

    assert(view_tile(&g_mmu, 0, 2, &spare2));
    assert(spare2.spare.present && !spare2.spare.bare && spare2.spare.position == 2);

    assert(view_tile(&g_mmu, 0, 3, &ungrouped));
    assert(!ungrouped.has_tool);
    assert(strcmp(ungrouped.label, "?") == 0);      /* a bay in no group */
    assert(ungrouped.has_sublabel && strcmp(ungrouped.sublabel, "Spool 4") == 0);
    assert(!ungrouped.spare.present);
}

/* ------------------------------------------------------------- pressure */

static void test_pressure_label(void)
{
    view_toolhead_t th;

    status_map_reset(&g_mmu);
    snprintf(g_mmu.toolheads[0].id, sizeof(g_mmu.toolheads[0].id), "fps1");
    g_mmu.toolheads[0].current_group = -1;
    g_mmu.toolhead_count = 1;

    /* one neutral reading whatever the value: no level, a label and a scale */
    g_mmu.toolheads[0].pressure = 0.93f;
    g_mmu.toolheads[0].set_point = 0.5f;
    assert(view_toolhead(&g_mmu, 0, &th));
    assert(th.pressure.present && strcmp(th.pressure.label, "Pressure") == 0);
    assert(strcmp(th.pressure.text, "0.930") == 0);
}

/* -------------------------------------------------------- disabled action */

static void test_disabled_action_reason(void)
{
    view_actions_t acts;
    int i;

    status_map_reset(&g_mmu);
    add_unit("oams1", 1);
    g_mmu.units[0].host_idx = 1;
    snprintf(g_mmu.units[0].lane, sizeof(g_mmu.units[0].lane), "fps1");
    g_mmu.units[0].slots[0].state = MMU_SLOT_READY;
    g_mmu.units[0].slots[0].info_source = MMU_INFO_MANUAL;
    g_mmu.units[0].slots[0].positioning = true;
    g_mmu.units[0].slots[0].global_id = 0;

    snprintf(g_mmu.groups[0].name, sizeof(g_mmu.groups[0].name), "T0");
    g_mmu.groups[0].count = 1;
    g_mmu.groups[0].members[0].unit = 0;
    g_mmu.groups[0].members[0].slot = 0;
    g_mmu.group_count = 1;

    /* the tile's "load" action must be present, disabled, and say why */
    assert(view_tile_actions(&g_mmu, 0, 0, &acts));
    assert(acts.count >= 1);
    for(i = 0; i < (int)acts.count; i++) {
        if(strcmp(acts.items[i].id, "load") == 0) {
            assert(acts.items[i].enabled == false);
            assert(strcmp(acts.items[i].reason, "The spool is still being positioned") == 0);
            return;
        }
    }
    fprintf(stderr, "test_view: no \"load\" action on a positioning tile\n");
    assert(0);
}

/* --------------------------------------------------------- edit spool */

static const view_action_t *find_action(const view_actions_t *a, const char *id);
static void action_ids(const view_actions_t *a, char *out, size_t n);

static const view_field_t *edit_field(const view_actions_t *acts, const view_action_t *a, const char *id)
{
    int i;

    for(i = 0; i < (int)a->form.field_count; i++) {
        const view_field_t *f = &acts->fields[a->form.field_first + i];

        if(strcmp(f->id, id) == 0) return f;
    }
    return NULL;
}

/* The ring's "Edit spool": after load and unload, dimmed with a reason while the
 * host cannot take it, and a form that starts from the bay's own facts. */
static void test_edit_action(void)
{
    static view_actions_t acts;
    const view_action_t *a;
    const view_field_t *f;
    mmu_slot_t *s;
    cJSON *arr;
    char ids[256];

    status_map_reset(&g_mmu);
    add_unit("oams1", 3);
    s = &g_mmu.units[0].slots[0];
    s->state = MMU_SLOT_READY;
    s->info_source = MMU_INFO_MANUAL;
    s->spool_id = 12;
    s->color = 0x123456;
    s->color_known = true;
    snprintf(s->material, sizeof s->material, "Nylon X");
    snprintf(s->brand, sizeof s->brand, "Acme Co");
    s->remaining_g = 430;
    s->remaining_pct = 43;
    g_mmu.units[0].slots[1].state = MMU_SLOT_READY;
    g_mmu.units[0].slots[1].info_source = MMU_INFO_MANUAL;
    g_mmu.units[0].slots[1].spool_id = -1;
    g_mmu.units[0].slots[2].state = MMU_SLOT_EMPTY;
    arr = cJSON_Parse("[{\"name\":\"Polymaker\"},{\"name\":\"bambu lab\"}]");
    assert(arr && status_map_apply_vendor_list(&g_mmu, arr));
    cJSON_Delete(arr);

    /* a host with no edit endpoint: the action is there, dimmed, and says why */
    assert(view_tile_actions(&g_mmu, 0, 0, &acts));
    action_ids(&acts, ids, sizeof ids);
    assert(strstr(ids, "load,unload,edit_spool,spool_list_refresh,") == ids);
    a = find_action(&acts, "edit_spool");
    assert(a->target == VIEW_TARGET_RING);
    assert(!a->enabled && a->reason == view_str(VS_REASON_EDIT_NO_SPOOLMAN));

    g_mmu.spoolman_edit = true;
    assert(view_tile_actions(&g_mmu, 0, 0, &acts));
    a = find_action(&acts, "edit_spool");
    assert(!a->enabled && a->reason == view_str(VS_REASON_SPOOLMAN_OFFLINE));

    g_mmu.spoolman_online = true;
    assert(view_tile_actions(&g_mmu, 0, 0, &acts));
    a = find_action(&acts, "edit_spool");
    assert(a->enabled && a->reason[0] == '\0');
    assert(strcmp(a->line, "spool edit oams11 material={material} color={color} vendor={vendor} "
                           "remaining={remaining} initial={initial} pa={pa}") == 0);
    assert(a->form.present && a->form.field_count == 6);

    /* the bay's own material, color and vendor are each one more choice */
    f = edit_field(&acts, a, "material");
    assert(f && f->kind == VIEW_FIELD_SELECT && f->option_count == MMU_MATERIAL_COUNT + 1);
    assert((int)f->value == MMU_MATERIAL_COUNT);
    assert(strcmp(acts.options[f->option_first + MMU_MATERIAL_COUNT].label, "Nylon X") == 0);
    f = edit_field(&acts, a, "color");
    assert(f && f->option_count == (uint8_t)(view_palette_count() + 1));
    assert((int)f->value == 0x123456);
    assert(acts.options[f->option_first + view_palette_count()].value == 0x123456);
    assert(strcmp(acts.options[f->option_first + view_palette_count()].label, "#123456") == 0);
    f = edit_field(&acts, a, "vendor");
    /* Generic first, the host's vendors sorted (bambu lab, Polymaker), the 27
     * curated brands they do not already name, then the bay's own Acme Co */
    assert(f && f->option_count == 31);
    assert(strcmp(acts.options[f->option_first].label, "Generic") == 0);
    assert(strcmp(acts.options[f->option_first + 1].label, "bambu lab") == 0);
    assert(strcmp(acts.options[f->option_first + 2].label, "Polymaker") == 0);
    assert(strcmp(acts.options[f->option_first + 30].label, "Acme Co") == 0);
    assert((int)f->value == 30);
    f = edit_field(&acts, a, "remaining");
    assert(f && f->kind == VIEW_FIELD_NUMBER && (int)f->value == 430 && strcmp(f->unit, "g") == 0);
    f = edit_field(&acts, a, "initial");
    assert(f && (int)f->value == 1000);
    f = edit_field(&acts, a, "pa");
    assert(f && f->kind == VIEW_FIELD_NUMBER && f->max == 2.0f);

    /* a bay with no spool record offers "not set" for its color, and Generic */
    assert(view_tile_actions(&g_mmu, 0, 1, &acts));
    a = find_action(&acts, "edit_spool");
    assert(a->enabled);
    f = edit_field(&acts, a, "color");
    assert((int)f->value == -1 && acts.options[f->option_first].value == -1);
    f = edit_field(&acts, a, "vendor");
    assert(strcmp(acts.options[f->option_first + (int)f->value].label, "Generic") == 0);

    /* an empty bay: dimmed, because there is nothing to edit */
    assert(view_tile_actions(&g_mmu, 0, 2, &acts));
    a = find_action(&acts, "edit_spool");
    assert(!a->enabled && a->reason == view_str(VS_REASON_BAY_EMPTY));

    /* the palette names nearest colors */
    assert(view_palette_count() == 18);
    assert(view_palette_color(3) == 0x000000 && strcmp(view_palette_name(3), "black") == 0);
    assert(strcmp(view_color_name(0x0A2989), "blue") == 0);
    assert(strcmp(view_color_name(0x101010), "black") == 0);
}

/* "Load" on a bay nobody has identified carries the spool form, so the load the
 * user came for is preceded by the one answer only they can give (PRINCIPLES.md
 * 13). A known bay's Load is the plain line, and the unknown one is dimmed only
 * while the edit itself cannot be taken. */
static void test_save_and_load_action(void)
{
    static view_actions_t acts;
    const view_action_t *a;
    const view_field_t *f;
    mmu_slot_t *s;
    char ids[256];
    int i;

    status_map_reset(&g_mmu);
    add_unit("oams1", 3);
    g_mmu.spoolman_edit = true;
    g_mmu.spoolman_online = true;
    /* bay 0 known, bay 1 ready with nothing saying what is in it, bay 2 empty */
    s = &g_mmu.units[0].slots[0];
    s->state = MMU_SLOT_READY;
    s->info_source = MMU_INFO_MANUAL;
    snprintf(s->material, sizeof s->material, "PETG");
    s = &g_mmu.units[0].slots[1];
    s->state = MMU_SLOT_READY;
    s->info_source = MMU_INFO_UNKNOWN;
    s->spool_id = -1;
    s->material[0] = '\0';
    g_mmu.units[0].slots[2].state = MMU_SLOT_EMPTY;
    g_mmu.units[0].slots[0].cal_state = MMU_CAL_CONFIGURED;
    g_mmu.units[0].slots[1].cal_state = MMU_CAL_CONFIGURED;
    g_mmu.units[0].slots[0].global_id = 0;
    g_mmu.units[0].slots[1].global_id = 1;
    /* tool T0 holds bays 0 and 1 */
    snprintf(g_mmu.groups[0].name, sizeof(g_mmu.groups[0].name), "T0");
    g_mmu.groups[0].count = 2;
    g_mmu.groups[0].members[0].unit = 0; g_mmu.groups[0].members[0].slot = 0;
    g_mmu.groups[0].members[1].unit = 0; g_mmu.groups[0].members[1].slot = 1;
    g_mmu.group_count = 1;

    /* known: Load is the plain line, no form, and nothing is required (its
     * enabled state is the plain Load's own) */
    assert(view_tile_actions(&g_mmu, 0, 0, &acts));
    a = find_action(&acts, "load");
    assert(a && a->enabled && !a->form.present);
    assert(strcmp(a->line, "load oams11") == 0);
    for(i = 0; i < (int)acts.field_count; i++) assert(!acts.fields[i].required);

    /* unknown: same id and place, the form, "Save and load", the line */
    assert(view_tile_actions(&g_mmu, 0, 1, &acts));
    action_ids(&acts, ids, sizeof ids);
    assert(strstr(ids, "load,unload,edit_spool,") == ids);
    a = find_action(&acts, "load");
    assert(a->enabled && a->reason[0] == '\0');
    assert(a->label == view_str(VS_ACTION_LOAD));
    assert(strcmp(a->line, "spool edit oams12 material={material} color={color} vendor={vendor} "
                           "remaining={remaining} initial={initial} pa={pa} then load") == 0);
    assert(a->form.present && a->form.field_count == 6);
    assert(a->form.submit_label == view_str(VS_SAVE_AND_LOAD));
    assert(strcmp(a->form.submit_label, "Save and load") == 0);

    f = edit_field(&acts, a, "material");
    assert(f && f->kind == VIEW_FIELD_SELECT && f->required && (int)f->value == -1);
    assert(f->option_count == MMU_MATERIAL_COUNT + 1);
    assert(acts.options[f->option_first].value == -1);
    assert(strcmp(acts.options[f->option_first].label, "Choose a material") == 0);
    /* the other fields are the editor's own, none of them required */
    for(i = 0; i < (int)a->form.field_count; i++) {
        f = &acts.fields[a->form.field_first + i];
        assert(f->required == (strcmp(f->id, "material") == 0));
    }
    /* and Edit spool beside it is the editor it always was */
    a = find_action(&acts, "edit_spool");
    assert(a->enabled && strstr(a->line, "then load") == NULL);
    assert(!edit_field(&acts, a, "material")->required);
    assert(strcmp(a->form.submit_label, "Save edit") == 0);

    /* the host cannot take an edit: dimmed, with Edit spool's own reason */
    g_mmu.spoolman_online = false;
    assert(view_tile_actions(&g_mmu, 0, 1, &acts));
    a = find_action(&acts, "load");
    assert(!a->enabled && a->reason == view_str(VS_REASON_SPOOLMAN_OFFLINE));
    assert(a->reason == find_action(&acts, "edit_spool")->reason);
    g_mmu.spoolman_edit = false;
    assert(view_tile_actions(&g_mmu, 0, 1, &acts));
    a = find_action(&acts, "load");
    assert(!a->enabled && a->reason == view_str(VS_REASON_EDIT_NO_SPOOLMAN));
    assert(a->reason == find_action(&acts, "edit_spool")->reason);
    /* a known bay is not dimmed by that: nothing about it needs the edit */
    assert(view_tile_actions(&g_mmu, 0, 0, &acts));
    a = find_action(&acts, "load");
    assert(a->enabled && !a->form.present);

    /* the load that follows the save has its own rules: a bay in no tool would
     * be saved for a load that is then refused, so it is dimmed in the load's
     * words and the form is still the one the row will open */
    g_mmu.spoolman_edit = true;
    g_mmu.spoolman_online = true;
    g_mmu.group_count = 0;
    assert(view_tile_actions(&g_mmu, 0, 1, &acts));
    a = find_action(&acts, "load");
    assert(!a->enabled && a->reason == view_str(VS_REASON_NOT_GROUPED));
    assert(a->form.present && a->form.submit_label == view_str(VS_SAVE_AND_LOAD));
}

/* ------------------------------------------------------------- shortfall */

static void test_shortfall_recomputed(void)
{
    view_toolhead_t th;

    status_map_reset(&g_mmu);
    add_unit("A", 1);
    g_mmu.units[0].slots[0].state = MMU_SLOT_READY;
    g_mmu.units[0].slots[0].info_source = MMU_INFO_MANUAL;
    g_mmu.units[0].slots[0].remaining_g = 80;
    g_mmu.units[0].toolhead = 0;

    g_mmu.toolhead_count = 1;
    g_mmu.toolheads[0].current_group = -1; /* no group loaded yet */
    g_mmu.job_need_g[1] = 120;             /* the metadata already arrived */

    /* the group T1 does not exist yet: no notice, even though job_need_g[1]
     * is set - this is the case the one-shot metadata-time check missed. */
    assert(view_toolhead(&g_mmu, 0, &th));
    assert(th.message.tone == VIEW_TONE_NEUTRAL);

    /* the group arrives later and gets loaded onto this toolhead. */
    snprintf(g_mmu.groups[0].name, sizeof(g_mmu.groups[0].name), "T1");
    g_mmu.groups[0].count = 1;
    g_mmu.groups[0].members[0].unit = 0;
    g_mmu.groups[0].members[0].slot = 0;
    g_mmu.group_count = 1;
    g_mmu.toolheads[0].current_group = 0;

    assert(view_toolhead(&g_mmu, 0, &th));
    assert(th.message.tone == VIEW_TONE_INFO);
    assert(strcmp(th.message.text, "T1 needs 120 g; its spools have 80 g") == 0);
}


/* ------------------------------------------------ helpers for the fix tests */

#define MID_DOT "\xc2\xb7"

/* A local model with one or two units, on nothing but itself. */
static mmu_model_t s_local, s_other;

static const view_action_t *find_action(const view_actions_t *a, const char *id)
{
    int i;

    for(i = 0; i < (int)a->count; i++) {
        if(strcmp(a->items[i].id, id) == 0) return &a->items[i];
    }
    return NULL;
}

/* The ids of @p a joined by ',' in order, for one whole-list compare. */
static void action_ids(const view_actions_t *a, char *out, size_t n)
{
    int i;

    out[0] = '\0';
    for(i = 0; i < (int)a->count; i++) {
        size_t used = strlen(out);

        snprintf(out + used, n - used, "%s%s", i ? "," : "", a->items[i].id);
    }
}

/* ------------------------------- fix 1: unit title, subtitle and offline text */

static void test_unit_title_and_subtitle(void)
{
    view_unit_t u1, u2, u3;

    status_map_reset(&s_local);
    add_unit_to(&s_local, "oams2", 4);
    s_local.units[0].host_idx = 2;
    s_local.units[0].variant = MMU_VARIANT_AMS_HT;
    add_unit_to(&s_local, "oams1", 4);
    s_local.units[1].host_idx = 1;
    s_local.units[1].variant = MMU_VARIANT_AMS2_PRO;
    add_unit_to(&s_local, "oams3", 4);
    s_local.units[2].host_idx = 3;
    s_local.units[2].variant = MMU_VARIANT_AMS;
    s_local.units[2].connected = false;

    assert(view_unit(&s_local, 0, &u2));
    assert(view_unit(&s_local, 1, &u1));
    assert(view_unit(&s_local, 2, &u3));

    /* the title is the family, the subtitle the unit's config name; no letters */
    assert(strcmp(u1.title, "AMS 2 Pro") == 0 && strcmp(u1.subtitle, "oams1") == 0);
    assert(strcmp(u2.title, "AMS HT") == 0 && strcmp(u2.subtitle, "oams2") == 0);
    assert(strcmp(u3.title, "AMS") == 0 && strcmp(u3.subtitle, "oams3") == 0);
    assert(strcmp(u1.subtitle, "oams1") == 0);
    assert(u1.online && u1.status_text[0] == '\0');
    assert(!u3.online && strcmp(u3.status_text, "Offline") == 0);
    /* an offline unit's reading is unknown, never a stale value: no env, no dryer */
    s_local.units[2].has_temp = true; s_local.units[2].temp_c = 24;
    s_local.units[2].can_dry = true;
    assert(view_unit(&s_local, 2, &u3) && !u3.env.present && !u3.dryer.present);
    s_local.units[1].has_temp = true; s_local.units[1].temp_c = 24;
    assert(view_unit(&s_local, 1, &u1) && u1.env.present);
}

/* A klipper-mmu family carries the product's own name, and the title is that
 * name. The family only names the unit: the variant - and with it the tile
 * kind, the dryer limits, everything else - stays the one it falls back to. */
static void apply_family(mmu_model_t *m, const char *name, const char *family)
{
    char json[160];
    cJSON *j;

    snprintf(json, sizeof json,
             "{\"unit\":\"%s\",\"family\":\"%s\",\"bays\":[{\"s\":\"e\"}]}", name, family);
    j = cJSON_Parse(json);
    assert(j && status_map_apply_openams_ui(m, j));
    cJSON_Delete(j);
}

static void test_family_product_title(void)
{
    static const struct { const char *family; const char *title; } k_products[] = {
        { "boxturtle",      "BoxTurtle" },
        { "nightowl",       "NightOwl" },
        { "emu",            "EMU" },
        { "kms",            "KMS" },
        { "qidi_box",       "QIDI Box" },
        { "angry_beaver",   "Angry Beaver" },
        { "3ms",            "3MS" },
        { "quattrobox_1_0", "QuattroBox 1.0" },
        { "quattrobox_1_1", "QuattroBox 1.1" },
        { "quattrobox_2",   "QuattroBox 2" },
        { "custom",         "Custom MMU" },
        { "ercf_1_1",       "ERCF 1.1" },
        { "ercf_2_0",       "ERCF 2.0" },
        { "ercf_3_0",       "ERCF 3.0" },
        { "tradrack",       "Tradrack" },
        { "3d_chameleon",   "3D Chameleon" },
        { "htlf",           "HTLF" },
        { "mmx6",           "MMX6" },
        { "mmx",            "MMX" },
        { "low_rider",      "Low Rider" },
        { "pico_mmu",       "Pico MMU" },
        { "btt_vivid",      "BTT ViViD" },
        { "claymore",       "Claymore" },
    };
    mmu_model_t m;
    view_unit_t u;
    size_t i;

    for(i = 0; i < sizeof(k_products) / sizeof(k_products[0]); i++) {
        status_map_reset(&m);
        apply_family(&m, "oams1", k_products[i].family);

        assert(view_unit(&m, 0, &u));
        assert(strcmp(u.title, k_products[i].title) == 0);
        assert(strcmp(u.info.family, k_products[i].title) == 0);
        assert(m.units[0].variant == MMU_VARIANT_AMS);
        assert(m.units[0].kind == MMU_UNIT_4SLOT);
    }

    /* An AMS family is still titled by its variant, and a projection that names
     * no product leaves no product title behind. */
    status_map_reset(&m);
    apply_family(&m, "oams1", "ams2");
    assert(view_unit(&m, 0, &u) && strcmp(u.title, "AMS 2 Pro") == 0);
    apply_family(&m, "oams1", "boxturtle");
    assert(view_unit(&m, 0, &u) && strcmp(u.title, "BoxTurtle") == 0);
    assert(m.units[0].variant == MMU_VARIANT_AMS2_PRO);
    apply_family(&m, "oams1", "a_family_from_the_future");
    assert(view_unit(&m, 0, &u) && strcmp(u.title, "AMS 2 Pro") == 0);
}

/* ------------------------------------------------ fix 2: tile label/sublabel */

static void test_tile_sublabel(void)
{
    view_tile_t a1, b3, plain;

    status_map_reset(&s_local);
    add_unit_to(&s_local, "oams1", 4);
    s_local.units[0].host_idx = 1;
    add_unit_to(&s_local, "oams2", 4);
    s_local.units[1].host_idx = 2;
    s_local.units[0].slots[0].state = MMU_SLOT_READY;
    s_local.units[0].slots[0].info_source = MMU_INFO_MANUAL;
    s_local.units[1].slots[2].state = MMU_SLOT_READY;
    s_local.units[1].slots[2].info_source = MMU_INFO_MANUAL;

    snprintf(s_local.groups[0].name, sizeof(s_local.groups[0].name), "T0");
    s_local.groups[0].count = 2;
    s_local.groups[0].members[0].unit = 0; s_local.groups[0].members[0].slot = 0;
    s_local.groups[0].members[1].unit = 1; s_local.groups[0].members[1].slot = 2;
    s_local.group_count = 1;

    assert(view_tile(&s_local, 0, 0, &a1));
    assert(strcmp(a1.label, "T0") == 0);
    assert(a1.has_sublabel && strcmp(a1.sublabel, "Spool 1") == 0);
    assert(strcmp(a1.slot_id, "oams11") == 0);   /* the global id stays in slot_id */

    assert(view_tile(&s_local, 1, 2, &b3));
    assert(strcmp(b3.label, "T0") == 0);
    assert(b3.has_sublabel && strcmp(b3.sublabel, "Spool 3") == 0);
    assert(strcmp(b3.slot_id, "oams23") == 0);

    /* ungrouped: the label is "?" and the sublabel still names the bay */
    assert(view_tile(&s_local, 1, 0, &plain));
    assert(!plain.has_tool);
    assert(strcmp(plain.label, "?") == 0);
    assert(plain.has_sublabel && strcmp(plain.sublabel, "Spool 1") == 0);
}

/* ------------------------------ fix 3: the toolhead title, subtitle and message */

static void test_toolhead_title_and_message(void)
{
    view_toolhead_t th;

    status_map_reset(&s_local);
    add_unit_to(&s_local, "oams1", 4);
    s_local.units[0].host_idx = 1;
    s_local.units[0].toolhead = 0;
    s_local.toolhead_count = 1;
    snprintf(s_local.toolheads[0].id, sizeof(s_local.toolheads[0].id), "fps1");
    snprintf(s_local.toolheads[0].extruder, sizeof(s_local.toolheads[0].extruder), "dragon_burner");
    s_local.toolheads[0].current_group = -1;
    s_local.toolheads[0].loaded = true;
    s_local.toolheads[0].loaded_unit = 0;
    s_local.toolheads[0].loaded_slot = 0;
    s_local.toolheads[0].loaded_ext = -1;
    s_local.toolheads[0].runout_from_unit = s_local.toolheads[0].runout_to_unit = -1;
    s_local.units[0].slots[0].state = MMU_SLOT_LOADED;
    s_local.units[0].slots[0].color = 0xff0000;
    s_local.units[0].slots[0].color_known = true;

    /* title = the extruder's display name; subtitle = the FPS id; the loaded
     * tool is only an overlay ("?" while its bay is in no group). */
    assert(view_toolhead(&s_local, 0, &th));
    assert(strcmp(th.title, "DragonBurner") == 0);
    assert(strcmp(th.subtitle, "fps1") == 0);
    assert(strcmp(th.id, "fps1") == 0);
    assert(th.tool.present && strcmp(th.tool.label, "?") == 0 && th.tool.color == 0xff0000);
    assert(th.message.tone == VIEW_TONE_NEUTRAL && strcmp(th.message.text, "Filament loaded") == 0);

    snprintf(s_local.toolheads[0].extruder, sizeof(s_local.toolheads[0].extruder), "extruder");
    assert(view_toolhead(&s_local, 0, &th));
    assert(strcmp(th.title, "Extruder") == 0);

    /* a runout is one line naming the tool and the spare's spool */
    snprintf(s_local.groups[0].name, sizeof(s_local.groups[0].name), "T0");
    s_local.groups[0].count = 2;
    s_local.groups[0].members[0].unit = 0; s_local.groups[0].members[0].slot = 0;
    s_local.groups[0].members[1].unit = 0; s_local.groups[0].members[1].slot = 1;
    s_local.group_count = 1;
    s_local.toolheads[0].runout_active = true;
    s_local.toolheads[0].runout_from_unit = 0; s_local.toolheads[0].runout_from_slot = 0;
    s_local.toolheads[0].runout_to_unit = 0;   s_local.toolheads[0].runout_to_slot = 1;
    assert(view_toolhead(&s_local, 0, &th));
    assert(th.message.tone == VIEW_TONE_INFO);
    assert(strcmp(th.message.text, "T0 ran out; switching to oams1 Spool 2") == 0);

    /* a runout with no target and no spare left says so plainly, as an error */
    s_local.toolheads[0].runout_to_unit = -1; s_local.toolheads[0].runout_to_slot = -1;
    s_local.groups[0].count = 1;
    assert(view_toolhead(&s_local, 0, &th));
    assert(th.message.tone == VIEW_TONE_ERROR);
    assert(strcmp(th.message.text, "T0 ran out; no spare spool is left") == 0);
    /* ...but with a spare it only says the tool ran out (the host has not named the target yet) */
    s_local.groups[0].count = 2;
    s_local.units[0].slots[1].state = MMU_SLOT_READY;
    assert(view_toolhead(&s_local, 0, &th));
    assert(th.message.tone == VIEW_TONE_INFO && strcmp(th.message.text, "T0 ran out") == 0);
    s_local.toolheads[0].runout_to_unit = 0; s_local.toolheads[0].runout_to_slot = 1;

    /* an error outranks the runout */
    s_local.toolheads[0].has_error = true;
    snprintf(s_local.toolheads[0].error_text, sizeof(s_local.toolheads[0].error_text), "Load failed");
    assert(view_toolhead(&s_local, 0, &th));
    assert(th.message.tone == VIEW_TONE_ERROR && strcmp(th.message.text, "Load failed") == 0);
}

/* ------------------------------------------- fix 4: adapter unknown vs missing */

/* A system-wide alert (no unit, no toolhead) reaches no toolhead and no unit:
 * the demo's dual scenario has one, and it once showed on the lane as well as
 * in the header because a zeroed alert names toolhead 0. */
static void test_system_alert_reaches_no_toolhead(void)
{
    view_toolhead_t th;
    view_alerts_t g;
    int i;

    mmu_model_set_scenario("dual");
    for(i = 0; i < (int)g_mmu.alert_count; i++) {
        const mmu_alert_t *a = &g_mmu.alerts[i];

        if(a->unit < 0) assert(a->toolhead == -1 && !a->lane_fault);
    }
    for(i = 0; i < (int)g_mmu.toolhead_count; i++) {
        int k;

        assert(view_toolhead(&g_mmu, i, &th));
        assert(!th.alert.present);
        assert(view_toolhead_alerts(&g_mmu, i, &g));
        for(k = 0; k < g.count; k++) assert(strstr(g.items[k].text, "Dual extruder") == NULL);
    }
    mmu_model_set_scenario("idle");
}

/* An errored tile that no lane fault covers is named in the message row, so
 * the error is visible without a tap; a real fault, and a load in progress,
 * keep the row. */
static void test_tile_error_message(void)
{
    view_toolhead_t th;

    status_map_reset(&s_local);
    add_unit_to(&s_local, "B", 4);
    s_local.units[0].toolhead = 0;
    s_local.toolhead_count = 1;
    snprintf(s_local.toolheads[0].id, sizeof(s_local.toolheads[0].id), "fps1");
    s_local.toolheads[0].current_group = -1;
    s_local.units[0].slots[3].state = MMU_SLOT_ERROR;

    assert(view_toolhead(&s_local, 0, &th));
    assert(th.message.tone == VIEW_TONE_ERROR);
    assert(strcmp(th.message.text, "B Spool 4 reported an error") == 0);

    s_local.toolheads[0].busy = MMU_BUSY_LOAD;      /* a step message wins */
    assert(view_toolhead(&s_local, 0, &th));
    assert(th.message.tone == VIEW_TONE_NEUTRAL && strcmp(th.message.text, "Loading filament") == 0);
    s_local.toolheads[0].busy = MMU_BUSY_NONE;

    s_local.toolheads[0].has_error = true;          /* a real fault wins */
    snprintf(s_local.toolheads[0].error_text, sizeof(s_local.toolheads[0].error_text), "Feeder stalled");
    assert(view_toolhead(&s_local, 0, &th));
    assert(strcmp(th.message.text, "Feeder stalled") == 0);
    s_local.toolheads[0].has_error = false;
}

static void test_adapter_unknown_is_not_missing(void)
{
    view_unit_t u;
    cJSON *j;

    status_map_reset(&s_local);
    add_unit_to(&s_local, "oams1", 4);
    s_local.units[0].can_dry = true;

    /* the model flag, directly: unknown, missing, present */
    s_local.units[0].has_power_adapter = false;
    s_local.units[0].power_adapter = false;
    assert(view_unit(&s_local, 0, &u));
    assert(u.dryer.present && !u.dryer.adapter_missing);

    s_local.units[0].has_power_adapter = true;
    assert(view_unit(&s_local, 0, &u));
    assert(u.dryer.adapter_missing);

    s_local.units[0].power_adapter = true;
    assert(view_unit(&s_local, 0, &u));
    assert(!u.dryer.adapter_missing);

    /* and from what the host sends: null, false, true */
    status_map_reset(&s_local);
    j = cJSON_Parse("{\"unit\":\"oams1\",\"bays\":[{}],"
                    "\"dryer\":{\"state\":\"off\",\"adapter\":null}}");
    assert(j && status_map_apply_openams_ui(&s_local, j));
    cJSON_Delete(j);
    assert(view_unit(&s_local, 0, &u));
    assert(u.dryer.present && !s_local.units[0].has_power_adapter && !u.dryer.adapter_missing);

    j = cJSON_Parse("{\"unit\":\"oams1\",\"bays\":[{}],"
                    "\"dryer\":{\"state\":\"off\",\"adapter\":false}}");
    assert(j && status_map_apply_openams_ui(&s_local, j));
    cJSON_Delete(j);
    assert(view_unit(&s_local, 0, &u));
    assert(s_local.units[0].has_power_adapter && u.dryer.adapter_missing);

    j = cJSON_Parse("{\"unit\":\"oams1\",\"bays\":[{}],"
                    "\"dryer\":{\"state\":\"off\",\"adapter\":true}}");
    assert(j && status_map_apply_openams_ui(&s_local, j));
    cJSON_Delete(j);
    assert(view_unit(&s_local, 0, &u));
    assert(s_local.units[0].has_power_adapter && !u.dryer.adapter_missing);

    /* the key absent altogether is unknown too */
    j = cJSON_Parse("{\"unit\":\"oams1\",\"bays\":[{}],\"dryer\":{\"state\":\"off\"}}");
    assert(j && status_map_apply_openams_ui(&s_local, j));
    cJSON_Delete(j);
    assert(view_unit(&s_local, 0, &u));
    assert(!s_local.units[0].has_power_adapter && !u.dryer.adapter_missing);
}

/* --------------------------------------------------------- fix 5: tile actions */

/* oams1 (host idx 1, lane fps1, RFID) with a T0 group of its bays 0 and 1;
 * toolhead fps1. Bay 0 loaded, bay 1 ready, bay 2 empty, bay 3 ready + linked
 * to spool 7 (and in no group). */
static void build_actions_model(mmu_model_t *m, int spools)
{
    mmu_unit_t *u;

    status_map_reset(m);
    add_unit_to(m, "oams1", 4);
    u = &m->units[0];
    u->host_idx = 1;
    u->rfid = true;
    u->toolhead = 0;
    snprintf(u->lane, sizeof(u->lane), "fps1");
    m->toolhead_count = 1;
    snprintf(m->toolheads[0].id, sizeof(m->toolheads[0].id), "fps1");
    m->toolheads[0].current_group = -1;
    m->toolheads[0].loaded = true;
    m->toolheads[0].loaded_unit = 0;
    m->toolheads[0].loaded_slot = 0;
    m->toolheads[0].runout_from_unit = m->toolheads[0].runout_to_unit = -1;

    u->slots[0].state = MMU_SLOT_LOADED;
    u->slots[0].global_id = 0;
    u->slots[1].state = MMU_SLOT_READY;
    u->slots[1].global_id = 1;
    u->slots[2].state = MMU_SLOT_EMPTY;
    u->slots[2].global_id = 2;
    u->slots[3].state = MMU_SLOT_READY;
    u->slots[3].global_id = 3;
    u->slots[3].spool_id = 7;
    u->slots[0].info_source = u->slots[1].info_source = u->slots[3].info_source = MMU_INFO_MANUAL;

    snprintf(m->groups[0].name, sizeof(m->groups[0].name), "T0");
    m->groups[0].count = 2;
    m->groups[0].members[0].unit = 0; m->groups[0].members[0].slot = 0;
    m->groups[0].members[1].unit = 0; m->groups[0].members[1].slot = 1;
    m->group_count = 1;

    if(spools >= 1) {
        m->spools[0].id = 7;
        snprintf(m->spools[0].vendor, sizeof(m->spools[0].vendor), "Prusament");
        snprintf(m->spools[0].material, sizeof(m->spools[0].material), "PLA");
        m->spools[0].remaining_g = 850;
    }
    if(spools >= 2) {
        m->spools[1].id = 12;
        snprintf(m->spools[1].vendor, sizeof(m->spools[1].vendor), "Bambu");
        snprintf(m->spools[1].material, sizeof(m->spools[1].material), "PETG");
        m->spools[1].remaining_g = 120;
    }
    m->spool_count = (uint8_t)spools;
}

static void test_tile_actions(void)
{
    view_actions_t acts;
    char ids[128];
    const view_action_t *a;
    const view_field_t *f;

    /* --- no spool list yet: the loaded tile --------------------------- */
    build_actions_model(&s_local, 0);
    assert(view_tile_actions(&s_local, 0, 0, &acts));
    action_ids(&acts, ids, sizeof ids);
    assert(strcmp(ids, "load,unload,edit_spool,spool_list_refresh,unlink,change_group,reread,calibrate") == 0);

    a = find_action(&acts, "unload");
    assert(strcmp(a->line, "unload oams11") == 0);   /* the bay's own line: only a loaded bay passes */
    assert(a->style == VIEW_STYLE_NORMAL && a->enabled && !a->confirm.present);
    a = find_action(&acts, "spool_list_refresh");
    assert(strcmp(a->line, "spool list") == 0 && a->enabled && !a->form.present);
    a = find_action(&acts, "reread");
    assert(strcmp(a->line, "reread oams11") == 0 && a->enabled);
    a = find_action(&acts, "calibrate");
    assert(strcmp(a->line, "calibrate oams11") == 0 && a->enabled);
    assert(a->confirm.present && a->confirm.title[0] && a->confirm.ok_label[0]);

    /* printing: the unload asks first, like the toolhead's */
    s_local.printing = true;
    assert(view_tile_actions(&s_local, 0, 0, &acts));
    assert(find_action(&acts, "unload")->confirm.present);
    s_local.printing = false;

    /* --- the ready tile ----------------------------------------------- */
    assert(view_tile_actions(&s_local, 0, 1, &acts));
    action_ids(&acts, ids, sizeof ids);
    assert(strcmp(ids, "load,unload,edit_spool,spool_list_refresh,unlink,change_group,reread,calibrate") == 0);
    a = find_action(&acts, "load");
    assert(strcmp(a->line, "load oams12") == 0 && a->enabled && a->reason[0] == '\0');

    /* --- the empty tile: the same set, load and unload dimmed with a reason */
    assert(view_tile_actions(&s_local, 0, 2, &acts));
    assert(!find_action(&acts, "load")->enabled && strcmp(find_action(&acts, "load")->reason, "No spool is inserted") == 0);
    assert(!find_action(&acts, "unload")->enabled && strcmp(find_action(&acts, "unload")->reason, "This spool is not loaded") == 0);
    assert(!find_action(&acts, "reread")->enabled);
    action_ids(&acts, ids, sizeof ids);
    assert(strcmp(ids, "load,unload,edit_spool,spool_list_refresh,unlink,change_group,reread,calibrate") == 0);

    /* --- a ready tile in no group: load is there, disabled, and says why */
    assert(view_tile_actions(&s_local, 0, 3, &acts));
    a = find_action(&acts, "load");
    assert(a && !a->enabled && strcmp(a->reason, "Assign this spool to a tool first") == 0);

    /* --- a two-spool list: assign_spool with a select form -------------- */
    build_actions_model(&s_local, 2);
    assert(view_tile_actions(&s_local, 0, 1, &acts));
    action_ids(&acts, ids, sizeof ids);
    assert(strcmp(ids, "load,unload,edit_spool,assign_spool,unlink,change_group,reread,calibrate") == 0);
    a = find_action(&acts, "assign_spool");
    assert(strcmp(a->line, "link oams12 {spool}") == 0 && a->enabled);
    assert(a->form.present && a->form.field_count == 1);
    f = &acts.fields[a->form.field_first];
    assert(strcmp(f->id, "spool") == 0 && f->kind == VIEW_FIELD_SELECT);
    assert(f->option_count == 2);
    assert(acts.options[f->option_first].value == 7);
    assert(strcmp(acts.options[f->option_first].label, "Prusament PLA " MID_DOT " 850 g") == 0);
    assert(acts.options[f->option_first + 1].value == 12);
    assert(strcmp(acts.options[f->option_first + 1].label, "Bambu PETG " MID_DOT " 120 g") == 0);
    assert((int)f->value == 7);                     /* unlinked bay: the first spool */

    /* --- the linked tile: unlink joins, its own spool is the default ---- */
    build_actions_model(&s_local, 2);
    s_local.units[0].slots[3].spool_id = 12;
    assert(view_tile_actions(&s_local, 0, 3, &acts));
    action_ids(&acts, ids, sizeof ids);
    assert(strcmp(ids, "load,unload,edit_spool,assign_spool,unlink,change_group,reread,calibrate") == 0);
    a = find_action(&acts, "unlink");
    assert(strcmp(a->line, "unlink oams14") == 0 && a->enabled);
    assert((int)acts.fields[find_action(&acts, "assign_spool")->form.field_first].value == 12);

    /* --- no RFID reader on the unit: no reread --------------------------- */
    s_local.units[0].rfid = false;
    assert(view_tile_actions(&s_local, 0, 1, &acts));
    assert(find_action(&acts, "reread") == NULL);

    /* --- out of range ----------------------------------------------------- */
    assert(!view_tile_actions(&s_local, 0, 4, &acts) && acts.count == 0);
    assert(!view_tile_actions(&s_local, 3, 0, &acts));
}

/* The "Confirm" tag carries the confirmation itself (UNIFIED_UI 7,
 * SpoolConfirm): one primary tag action whose form asks how much is left,
 * starting from the spool's full (initial) weight, sent as `spool confirm
 * <slot> <grams>`. Only the tile wearing that tag offers it. */
static void test_confirm_action(void)
{
    static view_actions_t acts;
    view_tile_t t;
    char ids[160];
    const view_action_t *a;
    const view_field_t *f;

    /* --- nothing pending: no such action ---------------------------------- */
    build_actions_model(&s_local, 0);
    assert(view_tile_actions(&s_local, 0, 1, &acts));
    assert(find_action(&acts, "confirm_spool") == NULL);

    /* --- a ready bay waiting for its confirmation --------------------------- */
    s_local.units[0].slots[1].pending_confirmation = true;
    s_local.units[0].slots[1].initial_g = 750;
    s_local.units[0].slots[1].remaining_g = 300;
    assert(view_tile(&s_local, 0, 1, &t) && t.has_tag && t.tag == view_str(VS_TAG_CONFIRM));
    assert(view_tile_actions(&s_local, 0, 1, &acts));
    action_ids(&acts, ids, sizeof ids);
    assert(strcmp(ids, "load,unload,edit_spool,spool_list_refresh,unlink,change_group,reread,calibrate,"
                       "confirm_spool") == 0);
    a = find_action(&acts, "confirm_spool");
    assert(a->target == VIEW_TARGET_TAG && a->style == VIEW_STYLE_PRIMARY);
    assert(a->label == view_str(VS_ACTION_CONFIRM_SPOOL));
    assert(strcmp(a->line, "spool confirm oams12 {grams}") == 0);
    assert(a->enabled && a->reason[0] == '\0' && !a->confirm.present);
    assert(a->form.present && a->form.field_count == 1);
    assert(a->form.submit_label == view_str(VS_BTN_CONFIRM) && !a->form.require_change);
    f = &acts.fields[a->form.field_first];
    assert(strcmp(f->id, "grams") == 0 && f->kind == VIEW_FIELD_NUMBER && !f->required);
    assert(f->label == view_str(VS_FIELD_REMAINING_LABEL) && strcmp(f->unit, "g") == 0);
    assert((int)f->value == 750 && f->min == 0.0 && f->max == 10000.0 && f->step == 1.0);

    /* --- the loaded bay, whose size nobody said: Spoolman's 1000 ----------- */
    s_local.units[0].slots[0].pending_confirmation = true;
    assert(view_tile_actions(&s_local, 0, 0, &acts));
    a = find_action(&acts, "confirm_spool");
    assert(a && strcmp(a->line, "spool confirm oams11 {grams}") == 0);
    assert((int)acts.fields[a->form.field_first].value == 1000);

    /* --- a more urgent tag says something else: no confirmation there ----- */
    s_local.units[0].slots[2].pending_confirmation = true;          /* empty: "Empty" */
    assert(view_tile_actions(&s_local, 0, 2, &acts));
    assert(find_action(&acts, "confirm_spool") == NULL);
    s_local.units[0].slots[1].state = MMU_SLOT_ERROR;               /* "Error" */
    assert(view_tile_actions(&s_local, 0, 1, &acts));
    assert(find_action(&acts, "confirm_spool") == NULL);

    /* --- an unidentified bay waiting too: every form keeps its fields (the
     * pool holds both spool editors, the assign and group forms and this one) */
    build_actions_model(&s_local, 2);
    s_local.units[0].slots[1].info_source = MMU_INFO_UNKNOWN;
    s_local.units[0].slots[1].pending_confirmation = true;
    assert(view_tile_actions(&s_local, 0, 1, &acts));
    a = find_action(&acts, "confirm_spool");
    assert(a && a->form.present && a->form.field_count == 1);
    assert(find_action(&acts, "load")->form.field_count == 6);
    assert(acts.field_count == 15);
}

/* --- fix 6: "Change group..." and the groups list ------------------------ */

static void test_change_group_action(void)
{
    view_actions_t acts;
    const view_action_t *a;
    const view_field_t *f;
    char ids[128];
    int i;

    /* The list of a bay in T0: every group on the lane, then "No group" and
     * "New group"; the default is the bay's own group. */
    build_actions_model(&s_local, 0);
    snprintf(s_local.groups[1].name, sizeof(s_local.groups[1].name), "T1");
    s_local.groups[1].count = 0;
    s_local.group_count = 2;

    assert(view_tile_actions(&s_local, 0, 1, &acts));
    a = find_action(&acts, "change_group");
    assert(a && strcmp(a->line, "change group {group} oams12") == 0 && a->enabled);
    assert(a->form.present && a->form.field_count == 1);
    f = &acts.fields[a->form.field_first];
    assert(strcmp(f->id, "group") == 0 && f->kind == VIEW_FIELD_SELECT);
    assert((int)f->value == 0);                       /* the bay's own group */
    assert(f->option_count == 4);
    assert(acts.options[f->option_first].value == 0 && strcmp(acts.options[f->option_first].label, "T0") == 0);
    assert(acts.options[f->option_first + 1].value == 1 && strcmp(acts.options[f->option_first + 1].label, "T1") == 0);
    assert(acts.options[f->option_first + 2].value == MMU_GROUP_CHOICE_NONE);
    assert(strcmp(acts.options[f->option_first + 2].label, "No group") == 0);
    assert(acts.options[f->option_first + 3].value == MMU_GROUP_CHOICE_NEW);
    assert(strcmp(acts.options[f->option_first + 3].label, "New group") == 0);

    /* A bay in no group has no group of its own to name, and "No group" is
     * the one choice the mapper refuses on it, so the default is the lane's
     * first group - T0 here. */
    assert(view_tile_actions(&s_local, 0, 3, &acts));
    f = &acts.fields[find_action(&acts, "change_group")->form.field_first];
    assert((int)f->value == 0);

    /* The group loaded in the toolhead is no bay's destination, so the default
     * skips it and takes the lane's next group. */
    s_local.loaded = true;
    s_local.current_group = 0;
    assert(view_tile_actions(&s_local, 0, 3, &acts));
    f = &acts.fields[find_action(&acts, "change_group")->form.field_first];
    assert((int)f->value == 1);
    assert(find_action(&acts, "change_group")->enabled);
    s_local.loaded = false;
    s_local.current_group = -1;

    /* A group of another lane is not offered: its members are all on fps2. */
    build_actions_model(&s_local, 0);
    add_unit_to(&s_local, "oams2", 4);
    s_local.units[1].toolhead = 1;
    s_local.units[1].lane[0] = '\0';
    s_local.toolhead_count = 2;
    snprintf(s_local.toolheads[1].id, sizeof(s_local.toolheads[1].id), "fps2");
    snprintf(s_local.groups[1].name, sizeof(s_local.groups[1].name), "T1");
    s_local.groups[1].count = 1;
    s_local.groups[1].members[0].unit = 1;
    s_local.groups[1].members[0].slot = 0;
    s_local.group_count = 2;

    assert(view_tile_actions(&s_local, 0, 1, &acts));
    f = &acts.fields[find_action(&acts, "change_group")->form.field_first];
    assert(f->option_count == 3);                      /* T0, No group, New group */

    /* The three refusals, one string each, on the action itself. */
    build_actions_model(&s_local, 0);
    assert(view_tile_actions(&s_local, 0, 1, &acts));
    assert(find_action(&acts, "change_group")->enabled);

    s_local.busy = MMU_BUSY_LOAD;
    assert(view_tile_actions(&s_local, 0, 1, &acts));
    a = find_action(&acts, "change_group");
    assert(!a->enabled && strcmp(a->reason, "The toolhead is busy") == 0);
    s_local.busy = MMU_BUSY_NONE;

    s_local.runout_active = true;
    assert(view_tile_actions(&s_local, 0, 1, &acts));
    a = find_action(&acts, "change_group");
    assert(!a->enabled && strcmp(a->reason, "The toolhead is handling a runout") == 0);
    s_local.runout_active = false;

    s_local.loaded = true;
    s_local.current_group = 0;
    assert(view_tile_actions(&s_local, 0, 1, &acts));
    a = find_action(&acts, "change_group");
    assert(!a->enabled && strcmp(a->reason, "This group is loaded in the toolhead") == 0);

    /* Dimmed, never hidden: the action is still in its place. */
    action_ids(&acts, ids, sizeof ids);
    assert(strstr(ids, "change_group") != NULL);
    for(i = 0; i < (int)acts.count; i++) assert(acts.items[i].id[0]);
}

static void test_groups_list(void)
{
    view_group_t vg;
    view_ungrouped_t ug;
    view_actions_t gacts;
    const view_action_t *a;
    int n;

    build_actions_model(&s_local, 0);
    /* oams2 on the same lane, and T0 spans both units. */
    add_unit_to(&s_local, "oams2", 4);
    s_local.units[1].toolhead = 0;
    snprintf(s_local.units[1].lane, sizeof(s_local.units[1].lane), "fps1");
    s_local.units[1].slots[1].state = MMU_SLOT_READY;
    s_local.units[1].slots[1].global_id = 5;
    s_local.units[1].slots[1].info_source = MMU_INFO_MANUAL;
    s_local.units[1].slots[1].color_known = true;
    s_local.units[1].slots[1].color = 0x2f8cf0;
    snprintf(s_local.units[1].slots[1].material, sizeof(s_local.units[1].slots[1].material), "PLA");
    s_local.units[1].slots[2].state = MMU_SLOT_READY;
    s_local.units[1].slots[2].global_id = 6;
    s_local.groups[0].members[2].unit = 1;
    s_local.groups[0].members[2].slot = 1;
    s_local.groups[0].count = 3;

    n = view_group_count(&s_local, 0);
    assert(n == 1);
    assert(view_group(&s_local, 0, 0, &vg));
    assert(strcmp(vg.name, "T0") == 0 && vg.member_count == 3);

    /* This unit's own bays, named like their tiles, carrying their state. */
    assert(vg.members[0].mine && !vg.members[0].dim);
    assert(strcmp(vg.members[0].slot_id, "oams11") == 0);
    assert(strcmp(vg.members[0].label, "Spool 1") == 0);
    assert(strcmp(vg.members[0].state, "Loaded") == 0);
    assert(!vg.members[0].color_known);              /* no color in this fixture */

    /* Another unit's bay: named with its unit and dimmed, never a thing to
     * manage from here, but still carrying its state and its tile's material,
     * which say whether the fallback would match and is ready. */
    assert(!vg.members[2].mine && vg.members[2].dim);
    assert(strcmp(vg.members[2].slot_id, "oams22") == 0);
    assert(strcmp(vg.members[2].label, "oams2 Spool 2") == 0);
    assert(vg.members[2].state[0] != '\0');
    {
        view_tile_t other;

        assert(view_tile(&s_local, 1, 1, &other));
        assert(strcmp(vg.members[2].material, other.material) == 0);
        assert(vg.members[2].color_known == other.color_known);
    }

    /* The first ready member with filament in order is the next spare. */
    assert(!vg.members[0].spare);
    assert(vg.members[1].spare);
    assert(strcmp(vg.members[1].slot_id, "oams12") == 0);

    /* The loaded group cannot be edited, and says which rule refuses. */
    s_local.loaded = true;
    s_local.current_group = 0;
    assert(view_group(&s_local, 0, 0, &vg));
    assert(!vg.editable && strcmp(vg.reason, "This group is loaded in the toolhead") == 0);
    s_local.loaded = false;
    s_local.current_group = -1;

    assert(view_group_actions(&s_local, 0, 0, &gacts));
    {
        a = find_action(&gacts, "delete_group");
        assert(a && strcmp(a->line, "delete group T0") == 0);
        assert(a->style == VIEW_STYLE_DANGER);
    }

    /* Out of range: no group, no actions. */
    assert(!view_group(&s_local, 0, 1, &vg) && vg.member_count == 0);
    assert(!view_group(&s_local, 9, 0, &vg));
    assert(!view_group_actions(&s_local, 0, 1, &gacts) && gacts.count == 0);

    /* The bays that hold no group: this unit's, in bay order. */
    assert(view_ungrouped_bays(&s_local, 0, &ug));
    assert(ug.count == 2);
    assert(strcmp(ug.bays[0].slot_id, "oams13") == 0 && strcmp(ug.bays[0].label, "Spool 3") == 0);
    assert(strcmp(ug.bays[0].state, "Empty") == 0);
    assert(strcmp(ug.bays[1].slot_id, "oams14") == 0);
    assert(!view_ungrouped_bays(&s_local, 9, &ug));
}

/* ---------------------- fix 7: a builder reads the model it is given only */

static void test_local_model_only(void)
{
    view_tile_t t;
    view_actions_t acts;
    char *json_a, *json_b;

    /* g_mmu holds a different scenario: two other units, no groups, no
     * spools, a different toolhead. */
    status_map_reset(&g_mmu);
    add_unit("X", 4);
    add_unit("Y", 4);
    g_mmu.units[0].slots[0].state = MMU_SLOT_LOADED;

    build_actions_model(&s_local, 2);

    /* the local model's group and ids, not g_mmu's */
    assert(view_tile(&s_local, 0, 1, &t));
    assert(strcmp(t.slot_id, "oams12") == 0);
    assert(strcmp(t.label, "T0") == 0 && strcmp(t.sublabel, "Spool 2") == 0);
    assert(t.spare.present && !t.spare.bare && t.spare.position == 1);

    /* load resolves against the local model's group and slot ids: with
     * g_mmu's (no groups, no oams12) it would be "bay not in a group" or a
     * line no map recognizes. */
    assert(view_tile_actions(&s_local, 0, 1, &acts));
    assert(find_action(&acts, "load")->enabled);
    assert(view_tile_actions(&s_local, 0, 3, &acts));
    assert(!find_action(&acts, "load")->enabled);
    assert(strcmp(find_action(&acts, "load")->reason, "Assign this spool to a tool first") == 0);

    /* the whole JSON of the local model does not move when g_mmu does */
    json_a = view_json(&s_local);
    assert(json_a);
    status_map_reset(&g_mmu);
    add_unit("oams1", 4);
    snprintf(g_mmu.groups[0].name, sizeof(g_mmu.groups[0].name), "T5");
    g_mmu.groups[0].count = 1;
    g_mmu.groups[0].members[0].unit = 0; g_mmu.groups[0].members[0].slot = 3;
    g_mmu.group_count = 1;
    json_b = view_json(&s_local);
    assert(json_b && strcmp(json_a, json_b) == 0);
    free(json_a);
    free(json_b);

    /* and a second local model is built from itself alone as well */
    build_actions_model(&s_other, 0);
    s_other.group_count = 0;
    assert(view_tile(&s_other, 0, 1, &t));
    assert(!t.has_tool && strcmp(t.label, "?") == 0 && strcmp(t.sublabel, "Spool 2") == 0);
    assert(view_tile(&s_local, 0, 1, &t));
    assert(t.has_tool && strcmp(t.label, "T0") == 0);
}

/* -------------- 4b: alerts by level, action targets and the tag's detail */

static void test_alert_levels_and_targets(void)
{
    view_alerts_t g;
    view_toolhead_t th;
    view_unit_t u;
    view_panel_t p;
    view_actions_t acts;
    view_tile_t t;
    int i;

    status_map_reset(&s_local);
    add_unit_to(&s_local, "oams1", 4);
    add_unit_to(&s_local, "oams2", 4);
    s_local.units[0].toolhead = 0;
    s_local.units[1].toolhead = 0;
    s_local.toolhead_count = 1;
    snprintf(s_local.toolheads[0].id, sizeof(s_local.toolheads[0].id), "fps1");
    s_local.toolheads[0].current_group = -1;
    s_local.units[0].slots[1].state = MMU_SLOT_ERROR;

    /* a lane fault that names no unit is the toolhead's */
    s_local.toolheads[0].has_error = true;
    s_local.toolheads[0].error_severity = MMU_SEV_STOP;
    s_local.toolheads[0].error_unit = -1;
    s_local.toolheads[0].error_slot = -1;
    s_local.toolheads[0].error_count = 2;
    snprintf(s_local.toolheads[0].error_text, sizeof(s_local.toolheads[0].error_text), "Feeder stalled");
    snprintf(s_local.toolheads[0].error_act[0], 16, "resume");
    s_local.toolheads[0].error_act_count = 1;
    assert(view_toolhead(&s_local, 0, &th));
    assert(th.alert.present && th.alert.count == 2 && th.alert.severity == (int)MMU_SEV_STOP);
    assert(view_toolhead_alerts(&s_local, 0, &g) && g.count == 1);
    assert(strcmp(g.items[0].text, "Feeder stalled") == 0 && g.items[0].action_count == 1);
    assert(strcmp(g.items[0].actions[0].id, "resume") == 0);
    assert(view_unit(&s_local, 0, &u) && !u.alert.present);
    /* the fault's buttons are not toolhead actions any more */
    assert(view_toolhead_actions(&s_local, 0, &acts) && acts.count == 2);
    /* Unload and Stop are always listed, in this order; the one that does not apply is dimmed */
    assert(strcmp(acts.items[0].id, "unload") == 0 && !acts.items[0].enabled &&
           strcmp(acts.items[0].reason, "No filament is loaded") == 0);
    assert(strcmp(acts.items[1].id, "stop") == 0 && !acts.items[1].enabled &&
           strcmp(acts.items[1].reason, "Nothing is loading or unloading") == 0);

    /* a fault that names a unit and a bay is shown by that tile: no badge on
     * the unit or the toolhead, and its actions are the tag's (a) */
    s_local.toolheads[0].error_unit = 0;
    s_local.toolheads[0].error_slot = 1;
    snprintf(s_local.toolheads[0].error_act[1], 16, "clear_fault");
    s_local.toolheads[0].error_act_count = 2;
    snprintf(s_local.toolheads[0].error_code, sizeof(s_local.toolheads[0].error_code), "feeder_stall");
    assert(view_toolhead(&s_local, 0, &th) && !th.alert.present);
    assert(view_unit(&s_local, 0, &u) && !u.alert.present);
    assert(view_tile(&s_local, 0, 1, &t));
    assert(t.state == VIEW_TILE_ERROR && strcmp(t.tag_detail, "Feeder stalled") == 0);
    assert(strcmp(t.tag_code, "feeder_stall") == 0);   /* the host's code, its own field */
    assert(view_tile_actions(&s_local, 0, 1, &acts));
    assert(find_action(&acts, "resume") && find_action(&acts, "resume")->target == VIEW_TARGET_TAG);
    assert(find_action(&acts, "clear_fault") && find_action(&acts, "clear_fault")->target == VIEW_TARGET_TAG);
    for(i = 0; i < (int)acts.count; i++) {
        const char *id = acts.items[i].id;

        if(strcmp(id, "load") == 0 || strcmp(id, "unload") == 0) assert(acts.items[i].target == VIEW_TARGET_TILE);
        else if(strcmp(id, "resume") != 0 && strcmp(id, "clear_fault") != 0) assert(acts.items[i].target == VIEW_TARGET_RING);
    }

    /* one that names a unit but no bay stays in the unit's badge, with its actions */
    s_local.toolheads[0].error_slot = -1;
    assert(view_unit(&s_local, 0, &u) && u.alert.present && u.alert.count == 2);
    assert(view_unit_alerts(&s_local, 0, &g) && g.count == 1 && g.items[0].action_count == 2);
    assert(strcmp(g.items[0].code, "feeder_stall") == 0);
    assert(strcmp(g.items[0].actions[1].id, "clear_fault") == 0);
    s_local.toolheads[0].error_slot = 1;

    /* the dryer's own fault */
    s_local.toolheads[0].has_error = false;
    s_local.units[1].can_dry = true;
    s_local.units[1].dry_state = MMU_DRY_ERROR;
    assert(view_unit_alerts(&s_local, 1, &g) && g.count == 1 && g.total == 1);
    assert(strcmp(g.items[0].text, "The dryer reported a fault.") == 0);

    /* each alert on exactly one level, and a badge lists only what is not
     * already shown elsewhere: */
    s_local.units[1].dry_state = MMU_DRY_IDLE;
    mmu_alerts_push_at(&s_local, "Panel wide", MMU_SEV_INFO, -1, -1, false);
    mmu_alerts_push_at(&s_local, "Low filament: oams2 Spool 1 has 20 g left", MMU_SEV_INFO, 1, -1, false);
    mmu_alerts_push_at(&s_local, "T0 needs 120 g; its spools have 80 g", MMU_SEV_INFO, -1, 0, false);
    mmu_alerts_push_at(&s_local, "Load failed", MMU_SEV_PAUSE, -1, 0, true);   /* a lane fault's history entry */
    view_panel(&s_local, &p);
    assert(p.alert_count == 1 && strcmp(s_local.alerts[p.alert_idx[0]].text, "Panel wide") == 0);
    /* (b) low filament is the tile's low ring: no unit badge item */
    assert(view_unit(&s_local, 1, &u) && !u.alert.present);
    /* (c) the shortfall shown as the message row has no badge... */
    s_local.toolheads[0].has_error = false;
    s_local.toolheads[0].current_group = 0;
    snprintf(s_local.groups[0].name, sizeof(s_local.groups[0].name), "T0");
    s_local.groups[0].count = 1;
    s_local.groups[0].members[0].unit = 1; s_local.groups[0].members[0].slot = 0;
    s_local.group_count = 1;
    s_local.job_need_g[0] = 120;
    s_local.units[1].slots[0].remaining_g = 80;
    assert(view_toolhead(&s_local, 0, &th));
    assert(strcmp(th.message.text, "T0 needs 120 g; its spools have 80 g") == 0);
    assert(!th.alert.present);
    /* ...but while an error owns the row it is listed, so it is not lost */
    s_local.toolheads[0].has_error = true;
    s_local.toolheads[0].error_unit = 1;
    s_local.toolheads[0].error_slot = 0;
    assert(view_toolhead(&s_local, 0, &th) && th.alert.present && th.alert.count == 1);
    assert(th.alert.tone == VIEW_TONE_INFO);
    /* a fault with no action and the row's own text is not listed either */
    s_local.toolheads[0].has_error = true;
    s_local.toolheads[0].error_unit = -1;
    s_local.toolheads[0].error_slot = -1;
    s_local.toolheads[0].error_act_count = 0;
    s_local.alert_count = 0;                 /* the log holds nothing else about this lane */
    assert(view_toolhead(&s_local, 0, &th) && !th.alert.present);
}

/* ------------------------- 4a: tile tags and dim, pressure scale, labels */

static void test_tags_dim_scale_labels(void)
{
    view_tile_t t;
    view_toolhead_t th;
    view_panel_t p;

    status_map_reset(&s_local);
    add_unit_to(&s_local, "oams1", 4);
    s_local.units[0].toolhead = 0;
    s_local.toolhead_count = 1;
    snprintf(s_local.toolheads[0].id, sizeof(s_local.toolheads[0].id), "fps1");
    s_local.toolheads[0].current_group = -1;
    s_local.toolheads[0].pressure = 0.4f;
    s_local.toolheads[0].set_point = 0.5f;
    s_local.units[0].slots[0].state = MMU_SLOT_READY;
    s_local.units[0].slots[0].info_source = MMU_INFO_MANUAL;
    snprintf(s_local.units[0].slots[0].material, sizeof(s_local.units[0].slots[0].material), "PLA");
    s_local.units[0].slots[1].state = MMU_SLOT_ERROR;
    s_local.units[0].slots[2].state = MMU_SLOT_READY;   /* unknown info */
    s_local.units[0].slots[3].state = MMU_SLOT_READY;
    s_local.units[0].slots[3].info_source = MMU_INFO_MANUAL;
    s_local.units[0].slots[3].pending_confirmation = true;

    assert(view_tile(&s_local, 0, 0, &t));           /* plain ready: no tag, not dim */
    assert(t.state == VIEW_TILE_READY && !t.has_tag && !t.dim && !t.hatched);
    assert(view_tile(&s_local, 0, 1, &t));           /* error: a red tag */
    assert(t.state == VIEW_TILE_ERROR && t.has_tag && t.tag_tone == VIEW_TONE_ERROR && strcmp(t.tag, "Error") == 0 && !t.hatched);
    assert(view_tile(&s_local, 0, 2, &t));           /* unknown: dim, and no "?" in the texts */
    assert(t.state == VIEW_TILE_UNKNOWN && t.dim && !t.hatched && t.name[0] == '\0' && t.material[0] == '\0');
    assert(strcmp(t.pct_text, "?") == 0);
    assert(view_tile(&s_local, 0, 3, &t));           /* pending confirmation: a yellow tag */
    assert(t.has_tag && t.tag_tone == VIEW_TONE_INFO && strcmp(t.tag, "Confirm") == 0);

    s_local.units[0].slots[1].state = MMU_SLOT_EMPTY;
    assert(view_tile(&s_local, 0, 1, &t));           /* empty: dim, a neutral tag */
    assert(t.dim && t.hatched && t.tag_tone == VIEW_TONE_NEUTRAL && strcmp(t.tag, "Empty") == 0);

    s_local.units[0].connected = false;              /* offline dims and hatches every tile */
    assert(view_tile(&s_local, 0, 0, &t));
    assert(t.dim && t.hatched);
    s_local.units[0].connected = true;

    assert(view_toolhead(&s_local, 0, &th));
    assert(th.pressure.present);
    assert(strcmp(th.pressure.scale[0], "0.00") == 0 && strcmp(th.pressure.scale[1], "0.50") == 0 &&
           strcmp(th.pressure.scale[2], "1.00") == 0);

    view_panel(&s_local, &p);
    assert(strcmp(p.labels.alerts, "Alerts") == 0 && strcmp(p.labels.settings, "Settings") == 0);
}

/* The bay a lane is unloading reads "Unloading", not "Loading"; the tone is the
 * one a load has, and the detail sentence is the unload's own. */
static void test_unloading_tile(void)
{
    view_tile_t t;
    view_toolhead_t th;

    status_map_reset(&s_local);
    add_unit_to(&s_local, "oams1", 2);
    s_local.units[0].toolhead = 0;
    s_local.toolhead_count = 1;
    snprintf(s_local.toolheads[0].id, sizeof(s_local.toolheads[0].id), "fps1");
    s_local.units[0].slots[0].state = MMU_SLOT_LOADED;
    s_local.units[0].slots[0].info_source = MMU_INFO_MANUAL;
    s_local.units[0].slots[1].state = MMU_SLOT_READY;
    s_local.units[0].slots[1].info_source = MMU_INFO_MANUAL;

    /* Unloading bay 0: the named bay's own tile, the tag's tone, its detail. */
    s_local.toolheads[0].busy = MMU_BUSY_UNLOAD;
    s_local.toolheads[0].busy_unit = 0;
    s_local.toolheads[0].busy_slot = 0;
    assert(view_tile(&s_local, 0, 0, &t));
    assert(t.state == VIEW_TILE_UNLOADING);
    assert(t.has_tag);
    assert(strcmp(t.tag, "Unloading") == 0);
    assert(t.tag_tone == VIEW_TONE_INFO);
    assert(strcmp(t.tag_detail, "This spool is unloading from the toolhead.") == 0);
    assert(!t.dim && !t.hatched);

    /* The other bay is untouched: the motion names one bay, not the unit. */
    assert(view_tile(&s_local, 0, 1, &t));
    assert(t.state == VIEW_TILE_READY && !t.has_tag);

    /* Loading the same bay reads "Loading" with the load's own detail, and the
     * toolhead's activity is the stepper either way. */
    s_local.toolheads[0].busy = MMU_BUSY_LOAD;
    assert(view_tile(&s_local, 0, 0, &t));
    assert(t.state == VIEW_TILE_LOADING);
    assert(strcmp(t.tag, "Loading") == 0);
    assert(t.tag_tone == VIEW_TONE_INFO);
    assert(strcmp(t.tag_detail, "This spool is loading into the toolhead.") == 0);

    /* The lane's plan reaches the stepper for an unload, with kind
     * "unloading": the unload stage is mapped exactly as a load's. */
    s_local.toolheads[0].busy = MMU_BUSY_UNLOAD;
    snprintf(s_local.toolheads[0].steps[0], sizeof(s_local.toolheads[0].steps[0]), "Retract from extruder");
    snprintf(s_local.toolheads[0].steps[1], sizeof(s_local.toolheads[0].steps[1]), "Park");
    s_local.toolheads[0].step_count = 2;
    s_local.toolheads[0].step_current = 1;
    s_local.toolheads[0].step_failed = -1;
    assert(view_toolhead(&s_local, 0, &th));
    assert(th.activity.kind == VIEW_ACTIVITY_UNLOADING);
    assert(th.activity.step_count == 2);
    assert(strcmp(th.activity.steps[0], "Retract from extruder") == 0);
    assert(strcmp(th.activity.steps[1], "Park") == 0);
    assert(th.activity.index == 1);
    assert(th.activity.failed == -1);

    /* The JSON name of the new state, the one the TypeScript port mirrors. */
    {
        cJSON *root = view_json_build(&s_local);
        const cJSON *ths, *unit, *bays, *first, *state;
        char *text;
        int i;
        bool seen = false;

        assert(root);
        text = view_json(&s_local);
        assert(text);
        assert(strstr(text, "\"unloading\"") != NULL);
        free(text);

        ths = cJSON_GetObjectItemCaseSensitive(root, "toolheads");
        for(i = 0; i < cJSON_GetArraySize(ths) && !seen; i++) {
            const cJSON *units = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(ths, i), "units");
            unit = cJSON_GetArrayItem(units, 0);
            bays = cJSON_GetObjectItemCaseSensitive(unit, "bays");
            first = cJSON_GetArrayItem(bays, 0);
            state = cJSON_GetObjectItemCaseSensitive(first, "state");
            seen = cJSON_IsString(state) && strcmp(state->valuestring, "unloading") == 0;
        }
        assert(seen);
        cJSON_Delete(root);
    }
}

/* ------------------------------------------------ step labels and detail */

/* A step with a label shows it as written, a step without one shows the
 * interface's own label, and the running step carries its detail beside that
 * text; the other steps carry none. `home` and `clean` are standard steps now,
 * so they are translated like the rest. */
static void test_step_labels_and_detail(void)
{
    view_toolhead_t th;
    mmu_toolhead_t *s;

    status_map_reset(&s_local);
    add_unit_to(&s_local, "oams1", 2);
    s_local.units[0].toolhead = 0;
    s_local.toolhead_count = 1;
    s = &s_local.toolheads[0];
    s->busy = MMU_BUSY_LOAD;
    s->busy_unit = 0;
    s->busy_slot = 0;

    /* The plan as the mapper left it: a label of the user's for one step, and
     * the bare names of the others. */
    snprintf(s->steps[0], sizeof(s->steps[0]), "Brushing the nozzle");
    snprintf(s->steps[1], sizeof(s->steps[1]), "heat");
    snprintf(s->steps[2], sizeof(s->steps[2]), "home");
    snprintf(s->steps[3], sizeof(s->steps[3]), "clean");
    s->step_count = 4;
    s->step_current = 1;
    s->step_failed = -1;

    /* Without a detail the plan is drawn exactly as it always was. */
    assert(view_toolhead(&s_local, 0, &th));
    assert(th.activity.step_count == 4);
    assert(strcmp(th.activity.steps[0], "Brushing the nozzle") == 0);   /* a label, as written */
    assert(strcmp(th.activity.steps[1], "Heat the nozzle") == 0);       /* a standard name, translated */
    assert(strcmp(th.activity.steps[2], "Home the axes") == 0);
    assert(strcmp(th.activity.steps[3], "Clean the nozzle") == 0);
    assert(th.activity.detail_text[0] == '\0');

    /* The running step's detail, and only the running step's. */
    snprintf(s->step_detail, sizeof(s->step_detail), "250 " "\xc2\xb0" "C");
    assert(view_toolhead(&s_local, 0, &th));
    assert(strcmp(th.activity.steps[1], "Heat the nozzle \xc2\xb7 250 " "\xc2\xb0" "C") == 0);
    assert(strcmp(th.activity.steps[0], "Brushing the nozzle") == 0);
    assert(strcmp(th.activity.steps[2], "Home the axes") == 0);
    assert(strcmp(th.activity.steps[3], "Clean the nozzle") == 0);

    /* A labeled running step is composed the same way: the user's words, then
     * the detail, and still never translated. */
    s->step_current = 0;
    assert(view_toolhead(&s_local, 0, &th));
    assert(strcmp(th.activity.steps[0],
        "Brushing the nozzle \xc2\xb7 250 " "\xc2\xb0" "C") == 0);
    assert(strcmp(th.activity.steps[1], "Heat the nozzle") == 0);

    /* A detail with no running step has nowhere to go, and none is shown. */
    s->step_current = -1;
    assert(view_toolhead(&s_local, 0, &th));
    assert(strcmp(th.activity.steps[0], "Brushing the nozzle") == 0);
    assert(strcmp(th.activity.steps[1], "Heat the nozzle") == 0);
}

/* ------------------------------------------------- the slot at rest */

/* The stepper's slot rests on the toolhead's own path (PRINCIPLES.md 2): with
 * a bay feeding the extruder it names the tool, the bay and the material, and
 * it carries the filament's own color; with nothing from a bay it rests as
 * "Ready: ..." or "Insert a spool" in no color; and while a plan runs there is no resting
 * value at all, so the stepper and the rest never both speak. */
static void test_activity_rest(void)
{
    view_toolhead_t th;
    mmu_toolhead_t *s;
    mmu_slot_t *bay;

    status_map_reset(&s_local);
    add_unit_to(&s_local, "oams1", 2);
    s_local.units[0].toolhead = 0;
    s_local.toolhead_count = 1;
    s = &s_local.toolheads[0];
    bay = &s_local.units[0].slots[1];

    /* Nothing loaded and no bay ready: the next step, with no color to draw. */
    assert(view_toolhead(&s_local, 0, &th));
    assert(th.activity.rest.present);
    assert(!th.activity.rest.loaded);
    assert(!th.activity.rest.color_known);
    assert(strcmp(th.activity.rest.label, "Insert a spool") == 0);

    /* Nothing loaded, bays ready: what could be loaded, the tools in group
     * order (T1 is declared before T0, so it leads) and only those with a
     * ready bay on this toolhead's units. */
    s_local.group_count = 3;
    snprintf(s_local.groups[0].name, sizeof(s_local.groups[0].name), "T1");
    s_local.groups[0].count = 1;
    s_local.groups[0].members[0].unit = 0;
    s_local.groups[0].members[0].slot = 1;
    snprintf(s_local.groups[1].name, sizeof(s_local.groups[1].name), "T0");
    s_local.groups[1].count = 1;
    s_local.groups[1].members[0].unit = 0;
    s_local.groups[1].members[0].slot = 0;
    snprintf(s_local.groups[2].name, sizeof(s_local.groups[2].name), "T3");
    s_local.groups[2].count = 1;
    s_local.groups[2].members[0].unit = 0;
    s_local.groups[2].members[0].slot = 0;      /* a second tool on the same bay */
    s_local.units[0].slots[0].state = MMU_SLOT_EMPTY;
    s_local.units[0].slots[1].state = MMU_SLOT_EMPTY;
    assert(view_toolhead(&s_local, 0, &th));
    assert(strcmp(th.activity.rest.label, "Insert a spool") == 0);   /* groups, no spools */
    s_local.units[0].slots[1].state = MMU_SLOT_READY;
    assert(view_toolhead(&s_local, 0, &th));
    assert(strcmp(th.activity.rest.label, "Ready: T1") == 0);
    s_local.units[0].slots[0].state = MMU_SLOT_READY;
    assert(view_toolhead(&s_local, 0, &th));
    assert(!th.activity.rest.loaded);
    assert(strcmp(th.activity.rest.label, "Ready: T1 \xc2\xb7 T0 \xc2\xb7 T3") == 0);
    /* A faulted bay is not ready; a unit of another toolhead is
     * not this one's. */
    s_local.units[0].slots[0].state = MMU_SLOT_ERROR;
    assert(view_toolhead(&s_local, 0, &th));
    assert(strcmp(th.activity.rest.label, "Ready: T1") == 0);
    s_local.units[0].toolhead = -1;
    assert(view_toolhead(&s_local, 0, &th));
    assert(strcmp(th.activity.rest.label, "Insert a spool") == 0);
    s_local.units[0].toolhead = 0;
    s_local.units[0].slots[0].state = MMU_SLOT_EMPTY;
    s_local.units[0].slots[1].state = MMU_SLOT_EMPTY;
    s_local.group_count = 0;

    /* A bay feeding it: the tool, the unit:bay name and the material. */
    snprintf(s_local.groups[0].name, sizeof(s_local.groups[0].name), "T1");
    s_local.group_count = 1;
    s_local.groups[0].count = 1;
    s_local.groups[0].members[0].unit = 0;     /* unit 0, bay 1 */
    s_local.groups[0].members[0].slot = 1;
    bay->state = MMU_SLOT_LOADED;
    snprintf(bay->material, sizeof(bay->material), "ABS");
    bay->color = 0x1E90FF;
    bay->color_known = true;
    s->loaded = true;
    s->loaded_unit = 0;
    s->loaded_slot = 1;
    s->loaded_ext = -1;

    assert(view_toolhead(&s_local, 0, &th));
    assert(th.activity.rest.present);
    assert(th.activity.rest.loaded);
    assert(th.activity.rest.color_known);
    assert(th.activity.rest.color == 0x1E90FF);
    assert(strcmp(th.activity.rest.label, "T1 \xc2\xb7 oams1:2 \xc2\xb7 ABS") == 0);

    /* Nobody told us the material: the path stops at the bay. */
    bay->material[0] = '\0';
    assert(view_toolhead(&s_local, 0, &th));
    assert(th.activity.rest.present);
    assert(th.activity.rest.loaded);
    assert(strcmp(th.activity.rest.label, "T1 \xc2\xb7 oams1:2") == 0);

    /* No known color: no color, so a renderer draws the neutral track. The
     * label is still the whole truth about the path. */
    bay->material[0] = 'A'; bay->material[1] = '\0';
    bay->color_known = false;
    assert(view_toolhead(&s_local, 0, &th));
    assert(th.activity.rest.present);
    assert(!th.activity.rest.color_known);
    assert(strcmp(th.activity.rest.label, "T1 \xc2\xb7 oams1:2 \xc2\xb7 A") == 0);

    /* A bay in no group: "?" for the tool, as the hotend icon's overlay has
     * it (UNIFIED_UI 5, one name for each thing). */
    s_local.group_count = 0;
    assert(view_toolhead(&s_local, 0, &th));
    assert(th.activity.rest.present);
    assert(strcmp(th.activity.rest.label, "? \xc2\xb7 oams1:2 \xc2\xb7 A") == 0);

    /* A plan runs: the stepper speaks and the rest is absent. */
    s_local.group_count = 1;
    s->step_count = 2;
    snprintf(s->steps[0], sizeof(s->steps[0]), "cut");
    snprintf(s->steps[1], sizeof(s->steps[1]), "feed");
    s->step_current = 0;
    s->step_failed = -1;
    assert(view_toolhead(&s_local, 0, &th));
    assert(!th.activity.rest.present);
    assert(th.activity.step_count == 2);
}

/* ------------------------------------------- the alert slots' "clear" flag */

/* A slot with no badge is empty because nothing alerts (clear: the check) or
 * because what alerts is shown beside it (not clear: the slot stays empty). */
static void test_alert_clear(void)
{
    view_toolhead_t th;
    view_unit_t u;
    mmu_toolhead_t *s;

    status_map_reset(&s_local);
    add_unit_to(&s_local, "oams1", 2);
    s_local.units[0].toolhead = 0;
    s_local.toolhead_count = 1;
    s = &s_local.toolheads[0];

    assert(view_toolhead(&s_local, 0, &th) && view_unit(&s_local, 0, &u));
    assert(!th.alert.present && th.alert.clear);
    assert(!u.alert.present && u.alert.clear);

    /* A history entry counts only while it is unread: a read one no longer
     * needs attention (the unit's own, then the lane's). */
    s_local.alert_count = 1;
    s_local.alerts[0].unit = 0;
    s_local.alerts[0].toolhead = -1;
    s_local.alerts[0].lane_fault = false;
    s_local.alerts[0].severity = MMU_SEV_INFO;
    s_local.alerts[0].unread = true;
    assert(view_unit(&s_local, 0, &u) && !u.alert.clear);
    s_local.alerts[0].unread = false;
    assert(view_unit(&s_local, 0, &u) && u.alert.clear);
    s_local.alerts[0].unit = -1;
    s_local.alerts[0].toolhead = 0;
    s_local.alerts[0].unread = true;
    assert(view_toolhead(&s_local, 0, &th) && !th.alert.clear);
    s_local.alerts[0].unread = false;
    /* a lane's own entry is in its badge, read or not, so the slot is still
     * a badge's, never the check */
    assert(view_toolhead(&s_local, 0, &th) && th.alert.present && !th.alert.clear);
    s_local.alert_count = 0;

    /* a fault the message row says, naming no unit: no badge, but not clear */
    s->has_error = true;
    s->error_unit = -1;
    s->error_slot = -1;
    snprintf(s->error_text, sizeof(s->error_text), "Hotend cold");
    assert(view_toolhead(&s_local, 0, &th) && view_unit(&s_local, 0, &u));
    assert(!th.alert.present && !th.alert.clear);
    assert(u.alert.clear);                     /* it does not name this unit */

    /* the same fault on this unit's bay: the lane and the unit both say it */
    s->error_unit = 0;
    s->error_slot = 1;
    assert(view_toolhead(&s_local, 0, &th) && view_unit(&s_local, 0, &u));
    assert(!th.alert.clear);
    assert(!u.alert.present && !u.alert.clear);
    s->has_error = false;

    /* a faulted bay, no lane fault: the unit and (through its row) the lane */
    s_local.units[0].slots[0].state = MMU_SLOT_ERROR;
    assert(view_toolhead(&s_local, 0, &th) && view_unit(&s_local, 0, &u));
    assert(!u.alert.clear && !th.alert.clear);
    s_local.units[0].slots[0].state = MMU_SLOT_EMPTY;

    /* a runout names its lane and its unit */
    s->runout_active = true;
    s->runout_from_unit = 0;
    s->runout_from_slot = 0;
    assert(view_toolhead(&s_local, 0, &th) && view_unit(&s_local, 0, &u));
    assert(!th.alert.clear && !u.alert.clear);
}

/* UNIFIED_UI 4g: a history entry leaves a toolhead's badge once it has aged
 * out (read for 2 minutes, or never read for 15), but stays in the history; a
 * fault with an action stays until it is resolved. The model's clock is set
 * by the test, never read. */
static void test_alert_badge_expiry(void)
{
    view_toolhead_t th;
    view_unit_t u;
    mmu_toolhead_t *s;
    mmu_alert_t *a;

    status_map_reset(&s_local);
    add_unit_to(&s_local, "oams1", 2);
    s_local.units[0].toolhead = 0;
    s_local.toolhead_count = 1;
    s = &s_local.toolheads[0];

    memset(&s_local.alerts[0], 0, sizeof s_local.alerts[0]);
    a = &s_local.alerts[0];
    s_local.alert_count = 1;
    a->unit = -1;
    a->toolhead = 0;
    a->severity = MMU_SEV_INFO;
    snprintf(a->text, sizeof a->text, "Job needs more filament");

    /* never read: 15 minutes after it was raised */
    a->unread = true;
    a->raised_ms = 1000;
    s_local.now_ms = 1000 + MMU_ALERT_BADGE_UNREAD_MS - 1;
    assert(view_toolhead(&s_local, 0, &th) && th.alert.present && !th.alert.clear);
    s_local.now_ms = 1000 + MMU_ALERT_BADGE_UNREAD_MS;
    assert(view_toolhead(&s_local, 0, &th) && !th.alert.present && th.alert.clear);
    assert(s_local.alert_count == 1);          /* still in the history */

    /* read: 2 minutes after it was read, whatever its age */
    a->unread = false;
    a->read_ms = 5000;
    s_local.now_ms = 5000 + MMU_ALERT_BADGE_READ_MS - 1;
    assert(view_toolhead(&s_local, 0, &th) && th.alert.present && !th.alert.clear);
    s_local.now_ms = 5000 + MMU_ALERT_BADGE_READ_MS;
    assert(view_toolhead(&s_local, 0, &th) && !th.alert.present && th.alert.clear);

    /* the clock is a uint32 and wraps: 512 ms after a raise just before 2^32 */
    a->unread = true;
    a->raised_ms = 0xFFFFFF00u;
    s_local.now_ms = 0x100u;
    assert(view_toolhead(&s_local, 0, &th) && th.alert.present);

    /* the unit's own check follows the same rule */
    a->unit = 0;
    a->toolhead = -1;
    a->raised_ms = 0;
    s_local.now_ms = MMU_ALERT_BADGE_UNREAD_MS - 1;
    assert(view_unit(&s_local, 0, &u) && !u.alert.clear);
    s_local.now_ms = MMU_ALERT_BADGE_UNREAD_MS;
    assert(view_unit(&s_local, 0, &u) && u.alert.clear);

    /* a fault with a recovery action stays however long it has stood */
    s->has_error = true;
    s->error_unit = -1;
    s->error_slot = -1;
    s->error_severity = MMU_SEV_PAUSE;
    s->error_act_count = 1;
    snprintf(s->error_act[0], sizeof s->error_act[0], "resume");
    snprintf(s->error_text, sizeof s->error_text, "Filament jam");
    s_local.now_ms = 0x7FFFFFFFu;
    assert(view_toolhead(&s_local, 0, &th) && th.alert.present && !th.alert.clear);
}

/* A mark-read stamps the read time, and a push stamps the raised time. */
static void test_alert_clock_stamps(void)
{
    mmu_model_t m;

    memset(&m, 0, sizeof m);
    m.now_ms = 4242;
    mmu_alerts_push_to(&m, "hello", MMU_SEV_INFO, -1);
    assert(m.alerts[0].raised_ms == 4242 && m.alerts[0].unread);
    assert(!mmu_alert_badge_expired(&m, &m.alerts[0]));
    m.now_ms = 4242 + MMU_ALERT_BADGE_UNREAD_MS;
    assert(mmu_alert_badge_expired(&m, &m.alerts[0]));
}

/* UNIFIED_UI 4g: a dark filament gets a light outline (view.c outline_for()):
 * black does, a mid color does not; an unknown color has none to outline. */
static void test_outline(void)
{
    static const struct { uint32_t rgb; bool outline; } cases[] = {
        { 0x000000u, true }, { 0x111111u, true }, { 0x1c1c1cu, true }, { 0x333333u, true },
        { 0x404040u, true }, { 0x484848u, false }, { 0x808080u, false }, { 0x1E90FFu, false },
        { 0xD32F2Fu, false }, { 0xFFFFFFu, false }, { 0x000080u, true }, { 0x0000FFu, false },
    };
    view_toolhead_t th;
    mmu_toolhead_t *s;
    mmu_slot_t *bay;
    size_t i;

    status_map_reset(&s_local);
    add_unit_to(&s_local, "oams1", 2);
    s_local.units[0].toolhead = 0;
    s_local.toolhead_count = 1;
    s = &s_local.toolheads[0];
    bay = &s_local.units[0].slots[1];
    bay->state = MMU_SLOT_LOADED;
    bay->color_known = true;
    s->loaded = true;
    s->loaded_unit = 0;
    s->loaded_slot = 1;
    s->loaded_ext = -1;

    for(i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        bay->color = cases[i].rgb;
        assert(view_toolhead(&s_local, 0, &th));
        assert(th.tool.color_known && th.tool.outline == cases[i].outline);
        assert(th.activity.rest.color_known && th.activity.rest.outline == cases[i].outline);
    }

    bay->color_known = false;
    assert(view_toolhead(&s_local, 0, &th));
    assert(!th.tool.outline && !th.activity.rest.outline);
}

/* -------------------------------------------------------------- no alloc */

/* Builders take a caller struct and never allocate: printed here (not
 * asserted against a magic number) so the report can quote real sizes. */
static void print_struct_sizes(void)
{
    printf("test_view: sizeof(view_tile_t)=%zu sizeof(view_unit_t)=%zu "
           "sizeof(view_toolhead_t)=%zu sizeof(view_alert_t)=%zu "
           "sizeof(view_panel_t)=%zu sizeof(view_action_t)=%zu "
           "sizeof(view_actions_t)=%zu\n",
           sizeof(view_tile_t), sizeof(view_unit_t), sizeof(view_toolhead_t),
           sizeof(view_alert_t), sizeof(view_panel_t), sizeof(view_action_t),
           sizeof(view_actions_t));
    /* the widget structs are built on the device's LVGL task stack per widget;
     * their actions are not in them (view_*_actions() build those on demand). */
    assert(sizeof(view_tile_t) < 512);
}

/* A whole panel, on the stack, through view_json_build() too: if any builder
 * reached for the heap this would be the path that would show it (run under
 * a leak checker in CI's ASan/UBSan build; this test just exercises it). */
static void test_no_alloc_smoke(void)
{
    view_tile_t t;
    view_unit_t u;
    view_toolhead_t th;
    view_panel_t p;
    cJSON *root;
    char *text;

    status_map_reset(&g_mmu);
    add_unit("A", 2);
    g_mmu.units[0].slots[0].state = MMU_SLOT_READY;
    g_mmu.units[0].slots[1].state = MMU_SLOT_EMPTY;

    assert(view_tile(&g_mmu, 0, 0, &t));
    assert(view_unit(&g_mmu, 0, &u));
    view_panel(&g_mmu, &p);
    (void)th;

    root = view_json_build(&g_mmu);
    assert(root);
    text = view_json(&g_mmu);
    assert(text);
    free(text);
    cJSON_Delete(root);
}

/* ------------------------------------------------------- no truncation */

/* Every text field of every builder, for one model: the panel, each toolhead,
 * each unit, each bay, and the on-demand action and alert groups. Nothing is
 * drawn and nothing is compared - the point is to make every vs_* call the
 * builders make, so the truncation counter sees them all. */
static void build_everything(const mmu_model_t *m)
{
    view_panel_t p;
    view_toolhead_t th;
    view_unit_t u;
    view_tile_t t;
    view_alerts_t alerts;
    view_actions_t acts;
    int i, j;

    view_panel(m, &p);
    for(i = 0; i < (int)p.setting_count; i++) {
        assert(view_setting_key(i)[0] != '\0');
    }
    for(i = 0; i < (int)p.alert_count; i++) {
        view_alert_t a;
        assert(view_alert(m, p.alert_idx[i], &a));
    }
    for(i = 0; i < (int)m->toolhead_count; i++) {
        assert(view_toolhead(m, i, &th));
        assert(view_toolhead_actions(m, i, &acts));
        assert(view_toolhead_alerts(m, i, &alerts));
    }
    for(i = 0; i < (int)m->unit_count; i++) {
        assert(view_unit(m, i, &u));
        assert(view_unit_actions(m, i, &acts));
        assert(view_unit_alerts(m, i, &alerts));
        for(j = 0; j < (int)m->units[i].slot_count; j++) {
            assert(view_tile(m, i, j, &t));
            assert(view_tile_actions(m, i, j, &acts));
        }
    }
}

static cJSON *load_vams_fixture(const char *name)
{
    char path[512];
    FILE *f;
    long len;
    char *text;
    cJSON *root;

    snprintf(path, sizeof path, "%s/%s.json", VIEW_FIXTURE_DIR, name);
    f = fopen(path, "rb");
    if(!f) {
        fprintf(stderr, "test_view: cannot open %s\n", path);
        exit(1);
    }
    if(fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "test_view: cannot size %s\n", path);
        exit(1);
    }
    text = malloc((size_t)len + 1);
    assert(text);
    assert(fread(text, 1, (size_t)len, f) == (size_t)len);
    text[len] = '\0';
    fclose(f);
    root = cJSON_Parse(text);
    free(text);
    if(!root) {
        fprintf(stderr, "test_view: %s is not valid JSON\n", path);
        exit(1);
    }
    return root;
}

static const char *const k_vams_fixtures[] = {
    "ams1_unverified", "idle", "loaded", "loading", "positioning", "two_lanes",
};

/* The brief's UTF-8 rule, as a test: no English string is cut anywhere. Every
 * field of every builder is sized for the longest composition it can make, so
 * a text cut at all is a bug, not a layout decision - and a translation would
 * be cut far harder. The count is printed (and asserted to be zero) so the
 * report can quote what it was before the buffers grew. */
static void test_no_truncation(const char *lang)
{
    mmu_model_t m;
    int i, events = 0;

    assert(view_set_language(lang));
    view_str_truncations_reset();

    /* The dummy scenarios the simulator and the gallery show. */
    for(i = 0; i < mmu_model_scenario_count(); i++) {
        mmu_model_set_scenario(mmu_model_scenario_name(i));
        build_everything(&g_mmu);
    }

    /* The recorded recorded fixtures, after every event, through core_apply() the
     * way the wasm host feeds the module. */
    for(i = 0; i < (int)(sizeof k_vams_fixtures / sizeof k_vams_fixtures[0]); i++) {
        cJSON *root = load_vams_fixture(k_vams_fixtures[i]);
        const cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "events");
        const cJSON *ev;
        core_t *core;

        assert(cJSON_IsArray(list));
        status_map_reset(&m);
        core = core_create(&m);
        assert(core);
        cJSON_ArrayForEach(ev, list) {
            const cJSON *kind = cJSON_GetObjectItemCaseSensitive(ev, "kind");
            const cJSON *unit = cJSON_GetObjectItemCaseSensitive(ev, "unit");
            const cJSON *payload = cJSON_GetObjectItemCaseSensitive(ev, "payload");

            if(!cJSON_IsString(kind) || !payload) continue;
            core_apply(core, kind->valuestring,
                       cJSON_IsString(unit) ? unit->valuestring : NULL, payload);
            mmu_model_merge_live(&m, &m);
            build_everything(&m);
            events++;
        }
        core_destroy(core);
        cJSON_Delete(root);
    }

    printf("test_view: %s: %d truncated string(s) over %d scenario(s) and %d fixture event(s)\n",
           lang, view_str_truncations(), mmu_model_scenario_count(), events);
    assert(view_str_truncations() == 0);
    assert(view_set_language("en"));
}

/* Every composed template, and the field its text lands in. A field that only
 * borrows a fixed string or the host's own words is not here any more: it is
 * a `const char *` into a const table or the model, so no buffer can shorten
 * it. What is left is what a longer language can still push past a field, so
 * this list is where the sizes are held honest - and where the report quotes
 * its count from. The one entry with no field is vs_spare: the spare badge
 * reaches both renderers as numbers (view_spare_t), and its composed text is
 * a caller's own buffer, so its budget is stated here. */
#define SPARE_CALLER_LEN 20   /* "{mark}{n}" in the pseudo-locale */

/* The longest arguments the model can hand a template: every name field
 * filled to its last byte, and the numbers at the width
 * tools/measure_strings.mjs measures with. The accented and CJK glyphs are
 * three bytes each - what a real translation brings - and are the reason the
 * cut has to be a character cut and not a byte cut. */
#define W_UNIT   "o\xc3\xa4ms\xc3\xa7\xc3\xa8"            /* the 8-byte name field, all 7 */
#define W_TOOL   "fps\xe2\x91\xa0\xe2\x91\xa1\xe2\x91\xa2\xe2\x91\xa3"  /* the 16-byte id, 15 */
#define W_EXT    "extruder_with_a_name"                    /* the 24-byte extruder name, 22 */
#define W_VENDOR "Polym\xc3\xa4ker-XX"                    /* the 16-byte vendor, 14 */
#define W_MAT    "Pol\xc3\xafLite-123"                    /* the 12-byte material, 12 */
#define W_GROUP  "T\xc3\xb6\xc3\xb6-T\xc3\xb8"            /* the 8-byte group name, 7 */
#define W_SLOT_MAT "Pol\xc3\xafLite-1234567890"           /* the 24-byte slot material, 21 */

/** Render every composed template with the longest arguments the view can
 *  make, into a buffer of exactly the field's size, and count the ones that
 *  did not fit. The count is view_str_truncations(), the same counter
 *  test_no_truncation() uses, so a template that overflows here is a template
 *  that overflows the view.
 *
 *  Returns the count so the caller can assert on it; the print is what the
 *  report quotes. */
static int test_composed_fit(const char *lang)
{
    char buf[VIEW_MESSAGE_TEXT_LEN];
    char spool[VIEW_MESSAGE_TEXT_LEN];   /* "{unit} Spool {n}": the alerts' {spool} */
    char tspool[VIEW_MESSAGE_TEXT_LEN];  /* "{tool} ({unit} Spool {n})": the runout's */
    int over = 0;

    assert(view_set_language(lang));

    /* Render into buf and count a cut. @p what names the field in the print. */
#define FITS(what, call)                                                        \
    do {                                                                         \
        view_str_truncations_reset();                                            \
        call;                                                                    \
        if(view_str_truncations() != 0) {                                       \
            over++;                                                              \
            printf("test_view: %s: [%s] does not fit %s (%zu bytes)\n",         \
                   lang, buf, (what), strlen(buf));                              \
        }                                                                        \
    } while(0)

    /* The ring and the grams line. */
    FITS("tile pct_text", vs_pct(100, buf, VIEW_PCT_LEN));
    FITS("tile grams_text", vs_grams(32767, buf, VIEW_GRAMS_LEN));

    /* The unit's environment and dryer lines. */
    FITS("unit env.text", vs_env(true, 100, true, -100, buf, VIEW_ENV_TEXT_LEN));
    FITS("unit dryer.text", vs_dryer_active(999, 99, buf, VIEW_DRYER_TEXT_LEN));

    /* The tile's sublabel and label, the extruder title, the pressure
     * numbers, and the group's own name copied straight through. */
    FITS("tile sublabel", vs_spool_label(9999, buf, VIEW_SUBLABEL_LEN));
    FITS("tile label", vs_copy(W_UNIT, buf, VIEW_LABEL_LEN));
    FITS("toolhead title", vs_extruder_title(W_EXT, buf, VIEW_TITLE_LEN));
    FITS("pressure text", vs_pressure(999.999f, buf, VIEW_PRESSURE_TEXT_LEN));
    FITS("pressure scale", vs_scale(99.99f, buf, VIEW_SCALE_LEN));

    /* The one spare badge's text, into a caller's own buffer. */
    FITS("spare badge", vs_spare(9999, buf, SPARE_CALLER_LEN));

    /* A Spoolman spool's picker label, whole. */
    FITS("option label", vs_spool_option(W_VENDOR, W_MAT, 32767, buf, VIEW_OPTION_LABEL_LEN));

    /* The stepper slot's resting path: a tool name, a bay label (composed
     * itself) and a material, into the activity's own field. */
    vs_spool_label(7, tspool, sizeof(tspool));
    {
        const vs_arg_t rest[3] = {
            VS_ARG_T("tool", W_GROUP), VS_ARG_T("spool", tspool), VS_ARG_T("material", W_SLOT_MAT),
        };

        FITS("activity rest label",
             vs_format(VS_REST_PATH, buf, VIEW_REST_PATH_LEN, rest, 3));
        FITS("activity rest label",
             vs_format(VS_REST_PATH_NO_MATERIAL, buf, VIEW_REST_PATH_LEN, rest, 2));
        {
            /* the longest list build_rest_empty() composes: ready_tools()'s
             * 96-byte buffer, full */
            char tools[96];
            const vs_arg_t ready[1] = { VS_ARG_T("tools", tools) };

            memset(tools, 'W', sizeof(tools) - 1);
            tools[sizeof(tools) - 1] = '\0';
            FITS("activity rest ready", vs_format(VS_REST_READY, buf, VIEW_REST_PATH_LEN, ready, 1));
        }
    }

    /* The toolhead's message line. Both of its {spool} arguments are composed
     * templates themselves, so these compose twice: a runout that switches to
     * a spool on another tool is the deepest, "{tool} ran out; switching to
     * {tool} ({unit} Spool {n})". */
    vs_unit_spool(W_UNIT, 7, spool, sizeof(spool));     /* a bay is 1..8: one digit */
    vs_tool_spool(W_TOOL, W_UNIT, 7, tspool, sizeof(tspool));
    FITS("toolhead message", vs_runout(W_TOOL, tspool, buf, VIEW_MESSAGE_TEXT_LEN));
    FITS("toolhead message", vs_runout(W_UNIT, spool, buf, VIEW_MESSAGE_TEXT_LEN));
    FITS("toolhead message", vs_runout(W_TOOL, NULL, buf, VIEW_MESSAGE_TEXT_LEN));
    FITS("toolhead message", vs_runout_no_spare(W_TOOL, buf, VIEW_MESSAGE_TEXT_LEN));
    FITS("toolhead message", vs_spool_error(spool, buf, VIEW_MESSAGE_TEXT_LEN));

    /* The two alerts the status mapper raises, into the model's own buffer.
     * Both name a spool the way the mapper builds it, "{unit} Spool {n}". */
    FITS("alert text", vs_low_filament(spool, 32767, buf, VIEW_ALERT_TEXT_LEN));
    FITS("alert text", vs_shortfall(9, 32767, 32767, buf, VIEW_ALERT_TEXT_LEN));

#undef FITS

    printf("test_view: %s: %d composed string(s) longer than their field\n", lang, over);
    assert(view_set_language("en"));
    return over;
}

/* ------------------------------------------------- language and fallback */

/** set_language: the languages the image carries, a code it does not, and the
 *  settings row the panel offers them in. */
static void test_set_language(void)
{
    view_panel_t p;
    const view_setting_t *row = NULL;
    int i, n = view_lang_count();

    assert(n >= 2);
    assert(VIEW_MAX_LANGS == 12);   /* English, the ten translations of UNIFIED_UI.md 12 and
                                     * the pseudo-locale that holds them to their budget */
    assert(n <= VIEW_MAX_LANGS);
    assert(strcmp(view_lang_code(0), "en") == 0);
    for(i = 0; i < n; i++) {
        assert(view_lang_code(i)[0] != '\0' && view_lang_name(i)[0] != '\0');
    }
    assert(view_lang_code(-1)[0] == '\0' && view_lang_name(n)[0] == '\0');
    assert(view_lang_current() >= 0 && view_lang_current() < n);

    /* a code this image does not carry changes nothing */
    assert(view_set_language("qps") == true);
    assert(view_lang_current() != 0);
    assert(view_set_language("xx") == false && view_set_language(NULL) == false);
    assert(view_lang_current() != 0);
    assert(view_set_language("en") == true && view_lang_current() == 0);

    /* the language row: every language by its own name, the current one
     * selected, and no on/off switch where a choice belongs */
    status_map_reset(&g_mmu);
    view_panel(&g_mmu, &p);
    for(i = 0; i < (int)p.setting_count; i++) {
        if(strcmp(p.settings[i].key, "language") == 0) row = &p.settings[i];
    }
    assert(row && row->is_choice && !row->value);
    /* the pseudo-locale is left out of the list: it is for tests and layout
     * review, and view_set_language() still takes it */
    assert(row->option_count == (uint8_t)(n - 1));
    assert(row->selected == view_lang_current());
    for(i = 0; i < (int)row->option_count; i++) {
        assert(row->options[i].value == (int32_t)i);
        assert(strcmp(row->options[i].label, view_lang_name(i)) == 0);
        assert(strcmp(view_lang_code(row->options[i].value), "qps") != 0);
    }
    assert(view_set_language("qps"));
    view_panel(&g_mmu, &p);
    assert(p.settings[VIEW_LANGUAGE_SETTING_ROW].selected == -1);   /* the current one is hidden */
    assert(view_set_language("en"));
    /* the other five rows are still plain switches */
    assert(p.setting_count == VIEW_MAX_SETTINGS);
    assert(!p.settings[0].is_choice && p.settings[0].action_on.line[0] != '\0');
}

/** The fallback: an id the current language has no text for is read in
 *  English. qps leaves out the two marks it cannot change ("?" and the bare
 *  infinity), so this runs against a real hole in a real table. */
static void test_fallback_to_english(void)
{
    int i;

    assert(view_set_language("qps"));
    for(i = 0; i < (int)VS_COUNT; i++) {
        assert(view_str((view_str_id_t)i)[0] != '\0');    /* no id is ever blank */
    }
    assert(strcmp(view_str(VS_UNKNOWN_MARK), "?") == 0);
    assert(strcmp(view_str(VS_INFINITY_MARK), "\xe2\x88\x9e") == 0);
    assert(strcmp(view_str(VS_TAG_RUNOUT), "Runout") != 0);   /* ... but this one is qps */

    assert(view_set_language("en"));
    assert(strcmp(view_str(VS_TAG_RUNOUT), "Runout") == 0);
    assert(strcmp(view_str((view_str_id_t)-1), "") == 0);      /* and out of range is "" */
    assert(strcmp(view_str(VS_COUNT), "") == 0);
}

/** Truncation never splits a character. The glyph is three bytes - what a CJK
 *  translation brings - and the buffer ends in the middle of it. */
static void test_utf8_safe_cut(void)
{
    char buf[8];
    const char *const cjk = "ab\xe6\x97\xa5" "cd";  /* "ab" + one CJK glyph + "cd" */

    view_str_truncations_reset();
    /* 4 of the 7 bytes would fit, but not the glyph: it is dropped whole */
    vs_copy(cjk, buf, 5);
    assert(strcmp(buf, "ab") == 0);
    /* one byte more and the glyph fits whole, and nothing after it does */
    vs_copy(cjk, buf, 6);
    assert(strcmp(buf, "ab\xe6\x97\xa5") == 0);
    /* the whole string, given room */
    vs_copy(cjk, buf, 8);
    assert(strcmp(buf, cjk) == 0);
    assert(view_str_truncations() == 2);

    /* a buffer of no size is left alone rather than written past */
    vs_copy(cjk, buf, 0);
    assert(strcmp(buf, cjk) == 0);
    assert(view_str_truncations() == 2);
}

/** core_set_language(): the same choice, one call up from the view. */
static void test_core_set_language(void)
{
    mmu_model_t m;
    core_t *core;
    cJSON *ui;

    status_map_reset(&m);
    core = core_create(&m);
    assert(core);
    assert(core_set_language(NULL, "qps") == false);
    assert(core_set_language(core, NULL) == false);
    assert(core_set_language(core, "xx") == false);
    assert(view_lang_current() == 0);

    assert(core_set_language(core, "qps") == true);
    assert(strcmp(view_str(VS_TAG_RUNOUT), "Runout") != 0);
    ui = cJSON_Parse("{\"unit\":\"oams1\",\"bays\":[{}]}");
    assert(ui && core_apply(core, "openams_ui", "oams1", ui));   /* a view in qps */
    cJSON_Delete(ui);
    assert(core_set_language(core, "en") == true);
    core_destroy(core);
    assert(view_lang_current() == 0);
}

/* ------------------------------------------------------- the formatter */

/** vs_format(): the one place a whole-sentence template is put together. It
 *  fills named placeholders, leaves one standing rather than dropping it, and
 *  cuts on a character like every other vs_* function. web/logic/strings.ts is
 *  the same function; the two are held identical by the differential tests. */
static void test_format(void)
{
    char buf[64];
    char small[30];
    const vs_arg_t runout[] = {
        VS_ARG_T("tool", "T0"),
        VS_ARG_T("spool", "oams2 Spool 1"),
    };
    const vs_arg_t partial[] = { VS_ARG_T("tool", "T0") };
    const vs_arg_t nothing[] = { VS_ARG_T("unrelated", "x") };
    const vs_arg_t prefix[] = { VS_ARG_N("tool", 0), VS_ARG_T("needful", "9999") };
    const vs_arg_t numbers[] = { VS_ARG_N("n", 8), VS_ARG_F("rh", 45, 2) };
    const vs_arg_t cjk[] = { VS_ARG_T("tool", "T0"),
                             VS_ARG_T("spool", "oams\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e") };

    /* the plain case: every placeholder named, the sentence reads as one */
    vs_format(VS_RUNOUT, buf, sizeof buf, runout, 2);
    assert(strcmp(buf, "T0 ran out; switching to oams2 Spool 1") == 0);

    /* an argument nobody asked for is ignored, and the ones asked for still fill */
    vs_format(VS_RUNOUT, buf, sizeof buf, partial, 1);
    assert(strcmp(buf, "T0 ran out; switching to {spool}") == 0);

    /* none of them named: the template stands whole, so the gap is visible
     * rather than the sentence quietly losing a part */
    vs_format(VS_RUNOUT, buf, sizeof buf, nothing, 1);
    assert(strcmp(buf, "{tool} ran out; switching to {spool}") == 0);
    vs_format(VS_RUNOUT, buf, sizeof buf, NULL, 0);
    assert(strcmp(buf, "{tool} ran out; switching to {spool}") == 0);

    /* a name is matched whole, not by its first letters: "needful" does not
     * answer {need} */
    vs_format(VS_SHORTFALL, buf, sizeof buf, prefix, 2);
    assert(strcmp(buf, "T0 needs {need} g; its spools have {have} g") == 0);

    /* numbers, whole and fixed-point */
    vs_format(VS_SPOOL_LABEL, buf, sizeof buf, numbers, 2);
    assert(strcmp(buf, "Spool 8") == 0);
    vs_format(VS_ENV_PCT, buf, sizeof buf, numbers, 2);
    assert(strcmp(buf, "45.00% RH") == 0);

    /* an out-of-range id has no text to fill, as everywhere else */
    vs_format((view_str_id_t)-1, buf, sizeof buf, runout, 2);
    assert(strcmp(buf, "") == 0);

    /* a fixed string is not a template: it comes through untouched, braces
     * and all, and nothing is counted as a cut */
    view_str_truncations_reset();
    vs_format(VS_TAG_ERROR, buf, sizeof buf, NULL, 0);
    assert(strcmp(buf, view_str(VS_TAG_ERROR)) == 0);
    assert(view_str_truncations() == 0);

    /* UTF-8: 30 bytes take the 25-byte literal and four more of the
     * argument's ASCII, and the three-byte glyph that follows is dropped
     * whole rather than half-written - exactly as vs_copy() does
     * (test_utf8_safe_cut). */
    view_str_truncations_reset();
    vs_format(VS_RUNOUT, small, sizeof small, cjk, 2);
    assert(strcmp(small, "T0 ran out; switching to oams") == 0);
    assert(view_str_truncations() == 1);
    view_str_truncations_reset();
    vs_format(VS_RUNOUT, buf, sizeof buf, cjk, 2);
    assert(strcmp(buf, "T0 ran out; switching to oams\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e") == 0);
    assert(view_str_truncations() == 0);

    /* a buffer of no size is left alone rather than written past */
    vs_format(VS_RUNOUT, buf, 0, cjk, 2);
    assert(strcmp(buf, "T0 ran out; switching to oams\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e") == 0);
}

/** A host unit name is cut on a character in the model (the boundary case and
 *  its C test are in test_status_map.c); what the view adds is that it must
 *  not cut it a second time, and must not put a half glyph into the JSON the
 *  renderers read. */
static void test_host_text_is_cut_on_a_character(void)
{
    mmu_model_t m;
    view_tile_t tile;
    view_unit_t unit;
    cJSON *ui;

    status_map_reset(&m);

    /* "oams" and ten glyphs is 34 bytes into the model's 32-byte name field,
     * so the name ends after nine glyphs; the tile and the unit both see those
     * 31 whole bytes and must not cut them again. */
    ui = cJSON_Parse("{\"unit\":\"oams\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\",\"family\":\"ams1\","
                     "\"bays\":[{\"s\":\"p\"}],"
                     "\"lane\":{\"op\":\"UNLOADED\"}}");
    assert(ui);
    assert(status_map_apply_openams_ui(&m, ui));
    cJSON_Delete(ui);

    assert(view_tile(&m, 0, 0, &tile));
    assert(strcmp(tile.slot_id, "oams\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5" "1") == 0);
    assert(view_unit(&m, 0, &unit));
    assert(strcmp(unit.subtitle, "oams\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5") == 0);

    /* Nothing was cut on the way into the JSON either: the tile's id is whole
     * where the model's is. */
    view_str_truncations_reset();
    {
        char *json = view_json(&m);

        assert(json);
        assert(strstr(json, "\"slot_id\":\"oams\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5" "1\"") != NULL);
        free(json);
    }
    assert(view_str_truncations() == 0);
}

/* -------------------------------------------------------------------- main */

int main(void)
{
    test_ink();
    test_color_known();
    test_edit_action();
    test_save_and_load_action();
    test_low_and_pct_text();
    test_label_and_spare();
    test_pressure_label();
    test_disabled_action_reason();
    test_shortfall_recomputed();
    test_unit_title_and_subtitle();
    test_family_product_title();
    test_tile_sublabel();
    test_toolhead_title_and_message();
    test_tags_dim_scale_labels();
    test_unloading_tile();
    test_step_labels_and_detail();
    test_activity_rest();
    test_alert_clear();
    test_alert_badge_expiry();
    test_alert_clock_stamps();
    test_outline();
    test_alert_levels_and_targets();
    test_system_alert_reaches_no_toolhead();
    test_tile_error_message();
    test_adapter_unknown_is_not_missing();
    test_tile_actions();
    test_confirm_action();
    test_change_group_action();
    test_groups_list();
    test_local_model_only();
    test_set_language();
    test_fallback_to_english();
    test_utf8_safe_cut();
    test_core_set_language();
    test_format();
    test_host_text_is_cut_on_a_character();
    assert(test_composed_fit("en") == 0);
    assert(test_composed_fit("qps") == 0);   /* a translation is meant to be longer */
    test_no_truncation("en");
    test_no_truncation("qps");
    test_no_alloc_smoke();
    print_struct_sizes();

    printf("test_view: ok\n");
    return 0;
}
