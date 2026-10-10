/**
 * @file test_screen_edit.c
 * The device's spool editor, driven headlessly: real LVGL events on the real
 * widgets of the real screen, then a real Save.
 *
 * The screen is a renderer of the view, but it composes the `spool edit` line
 * itself (src/ui/screen_edit.c). Nothing else in the tree checks that line, so
 * nothing else would notice the screen picking up the wrong field: the
 * differential tests exercise the view's form, not this screen's steppers.
 *
 * Setup is the simulator's --smoke one (lv_init(), an SDL window on the dummy
 * driver, ui_theme_init(), ui_nav_init(), a scenario, one ui_nav_goto), and
 * the platform's action hook is the capture point: actions_log() formats the
 * line and hands it to platform_action(), which is where the device would send
 * it to Moonraker. Each captured line is then resolved with actions_rpc_why(),
 * so a test can assert what the HOST would receive, not just what the screen
 * composed.
 */

#include "app/platform.h"
#include "lvgl.h"

#include "app/actions.h"
#include "backend/actions_map.h"
#include "backend/status_map.h"
#include "lib/cjson/cJSON.h"
#include "model/mmu_model.h"
#include "ui/ui_nav.h"
#include "ui/ui_print_common.h"
#include "ui/ui_screens.h"
#include "ui/ui_theme.h"
#include "view/strings.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------- the capture point */

#define CAPTURE_MAX 16
static char s_capture[CAPTURE_MAX][192];
static int s_capture_count;

void platform_action(const char *line)
{
    if(s_capture_count < CAPTURE_MAX)
        snprintf(s_capture[s_capture_count++], sizeof s_capture[0], "%s", line ? line : "");
}

/** The one platform hook the screens' settings page also wants; this test never
 *  opens that page, so touching the calibration is a no-op. */
platform_link_t platform_link_status(char *detail, size_t n)
{
    if(detail && n) detail[0] = '\0';
    return PLATFORM_LINK_READY;
}

void platform_touch_calibrate(void)
{
}

static void capture_reset(void)
{
    s_capture_count = 0;
}

/** The `spool edit ...` line the editor last saved, or NULL when it saved none. */
static const char *captured_edit(void)
{
    int i;

    for(i = s_capture_count - 1; i >= 0; i--) {
        if(strncmp(s_capture[i], "spool edit ", 11) == 0) return s_capture[i];
    }
    return NULL;
}

/* ------------------------------------------------------------- the harness */

static int s_failures;

static void check(bool ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if(!ok) s_failures++;
}

static void report_line(const char *line)
{
    printf("     line: %s\n", line ? line : "(none)");
}

/** The value of `key=value` in a `spool edit` line, into @p out; false when the
 *  line carries no such key. */
static bool field_of(const char *line, const char *key, char *out, size_t n)
{
    size_t klen = strlen(key);
    const char *p = line ? line : "";

    while((p = strstr(p, key)) != NULL) {
        if(p > line && p[-1] == ' ' && p[klen] == '=') {
            const char *v = p + klen + 1;
            const char *e = strchr(v, ' ');
            size_t len = e ? (size_t)(e - v) : strlen(v);

            if(len >= n) len = n - 1;
            memcpy(out, v, len);
            out[len] = '\0';
            return true;
        }
        p += klen;
    }
    return false;
}

/* --------------------------------------------------------- the host's answer */

/** What `server.openams_spoolman.edit` would carry for @p line under @p key, as
 *  text ("" when the RPC carries no such key, or does not resolve at all). */
static void host_rpc_string(const char *line, const char *key, char *out, size_t n)
{
    char method[64];
    cJSON *params = NULL;
    const char *why = "";
    cJSON *v;

    out[0] = '\0';
    if(!line) return;
    if(actions_rpc_why(line, &g_mmu, method, sizeof method, &params, &why) != 1) return;
    v = params ? cJSON_GetObjectItemCaseSensitive(params, key) : NULL;
    if(cJSON_IsString(v) && v->valuestring)
        snprintf(out, n, "%s", v->valuestring);
    else if(cJSON_IsNumber(v))
        snprintf(out, n, "%d", (int)v->valuedouble);
    cJSON_Delete(params);
}

/** What the host would be told about @p key, the way the RPC names it (the line
 *  says `color`, the RPC `color_hex`): the RPC's own value, else the value the
 *  line carries - a field the edit did not change is not sent, and what stands
 *  on the host is what the form started from. */
static void host_field(const char *line, const char *key, char *out, size_t n)
{
    char rpc_key[32];

    snprintf(rpc_key, sizeof rpc_key, "%s%s", key, strcmp(key, "color") == 0 ? "_hex" : "");
    host_rpc_string(line, rpc_key, out, n);
    if(out[0]) return;
    if(strcmp(key, "material") == 0) {
        /* the RPC sends the name, the line the index into the model's table */
        int i = 0;
        char idx[16] = "";

        if(field_of(line, key, idx, sizeof idx)) i = atoi(idx);
        if(i < MMU_MATERIAL_COUNT) snprintf(out, n, "%s", mmu_material_name(i));
        return;
    }
    field_of(line, key, out, n);
}

/* ------------------------------------------------------------- the widgets */

/** The first object under @p root (depth first) whose label text is @p text. */
static lv_obj_t *find_label(lv_obj_t *root, const char *text)
{
    uint32_t i, n;

    if(!root) return NULL;
    if(lv_obj_check_type(root, &lv_label_class)) {
        const char *t = lv_label_get_text(root);

        if(t && strcmp(t, text) == 0) return root;
    }
    n = lv_obj_get_child_count(root);
    for(i = 0; i < n; i++) {
        lv_obj_t *hit = find_label(lv_obj_get_child(root, i), text);

        if(hit) return hit;
    }
    return NULL;
}

/** The stepper button of the field whose caption is @p caption: the card holds
 *  the caption label and the stepper, and the stepper's second child is its
 *  plus button (ui_kit.c's stepper(): minus, plus, value). */
static lv_obj_t *find_stepper_button(lv_obj_t *root, const char *caption, bool plus)
{
    lv_obj_t *cap = find_label(root, caption);
    lv_obj_t *card, *stepper = NULL;
    uint32_t i, n;

    if(!cap) return NULL;
    card = lv_obj_get_parent(cap);
    if(!card) return NULL;
    /* the stepper is the card's child that is not the caption */
    n = lv_obj_get_child_count(card);
    for(i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(card, i);

        if(c == cap) continue;
        stepper = c;
        break;
    }
    if(!stepper) return NULL;
    return lv_obj_get_child(stepper, plus ? 1u : 0u);
}

/** The clickable object of a ui_kit button: the label's parent. */
static lv_obj_t *find_button(lv_obj_t *root, const char *text)
{
    lv_obj_t *l = find_label(root, text);

    return l ? lv_obj_get_parent(l) : NULL;
}

/** The color sheet's grid under @p root: the one object holding the whole
 *  palette, and the first swatch in it. The sheet lays its children out on the
 *  top layer, which never gets a size in this test (nothing renders), so the
 *  grid is recognized by what it holds rather than by where it sits. */
static lv_obj_t *first_swatch(lv_obj_t *root)
{
    uint32_t i, n;

    if(!root) return NULL;
    n = lv_obj_get_child_count(root);
    if(n >= (uint32_t)print_palette_count()) return lv_obj_get_child(root, 0);
    for(i = 0; i < n; i++) {
        lv_obj_t *hit = first_swatch(lv_obj_get_child(root, i));

        if(hit) return hit;
    }
    return NULL;
}

static void click(lv_obj_t *o)
{
    if(o) lv_obj_send_event(o, LV_EVENT_CLICKED, NULL);
}

/** Run the LVGL timers, so a navigation or a toast a handler queued has run. */
static void settle(int n)
{
    int i;

    for(i = 0; i < n; i++) lv_timer_handler();
}

/* ------------------------------------------------------------------- setup */

/* A2 of the demo model: a manually entered spool ("PETG-CF" / "Generic"), so
 * the editor opens it editable (a tag-read spool is the demo's read-only one). */
#define BAY 1

/** Open the editor on bay @p slot of the bound unit, as a user walking in from a
 *  tile would, against a host that can take the edit (a live model with a
 *  Spoolman to edit through: otherwise the screen's Save has nothing to send). */
static bool open_edit(int slot)
{
    mmu_model_set_scenario("idle");
    mmu_model_set_live(true);
    g_mmu.spoolman_edit = true;
    g_mmu.spoolman_online = true;
    mmu_home_set_selection(mmu_this_unit(), slot, -1);
    ui_theme_init();
    ui_nav_init();
    return ui_nav_goto(UI_SCREEN_EDIT);
}

/** Feed Spoolman's answer to `vendor list` to the model, the way the core does
 *  when the host replies to the editor asking for its vendors. */
static void vendors_arrive(const char *const *names, int n)
{
    cJSON *arr = cJSON_CreateArray();
    int i;

    for(i = 0; i < n; i++) {
        cJSON *v = cJSON_CreateObject();

        cJSON_AddStringToObject(v, "name", names[i]);
        cJSON_AddItemToArray(arr, v);
    }
    status_map_apply_vendor_list(&g_mmu, arr);
    cJSON_Delete(arr);
}

/** Tap a stepper @p times, then Save; returns the `spool edit` line (NULL when
 *  the Save was unavailable, which is itself worth asserting). */
static const char *step_and_save(const char *caption, bool plus, int times)
{
    lv_obj_t *scr = lv_screen_active();
    int i;

    for(i = 0; i < times; i++) {
        click(find_stepper_button(scr, caption, plus));
        settle(2);
    }
    capture_reset();
    click(find_button(scr, view_str(VS_ACTION_SAVE_EDIT)));
    settle(2);
    return captured_edit();
}

static void expect_field(const char *line, const char *key, const char *want)
{
    char got[96] = "";
    char what[160];

    field_of(line, key, got, sizeof got);
    snprintf(what, sizeof what, "line holds %s=%s", key, want);
    check(line && strcmp(got, want) == 0, what);
}

/** What the line's `vendor=` names, read through the model's own list exactly
 *  as the mapper reads it: the index alone says nothing, the name does. An
 *  index one past the list is the bay's own vendor. */
static void line_vendor(const char *line, char *out, size_t n)
{
    char idx[16] = "";
    const mmu_slot_t *sp = &g_mmu.units[mmu_this_unit()].slots[BAY];
    int i;

    out[0] = '\0';
    if(!line || !field_of(line, "vendor", idx, sizeof idx)) return;
    i = atoi(idx);
    snprintf(out, n, "%s", (i < mmu_edit_vendor_count(&g_mmu)) ? mmu_edit_vendor_name(&g_mmu, i)
                                                               : sp->brand);
}

static void expect_vendor(const char *line, const char *want)
{
    char got[96] = "";
    char what[160];

    line_vendor(line, got, sizeof got);
    snprintf(what, sizeof what, "line's vendor names %s", want);
    check(line && strcmp(got, want) == 0, what);
}

/** The vendor the host ends up with: the one the edit names, or - when the edit
 *  says nothing about the vendor because it did not change - the one the line
 *  already named. */
static void expect_host_vendor(const char *line, const char *want)
{
    char got[96] = "";
    char what[160];

    host_rpc_string(line, "vendor", got, sizeof got);
    if(!got[0]) line_vendor(line, got, sizeof got);
    snprintf(what, sizeof what, "the host's vendor is %s", want);
    check(line && strcmp(got, want) == 0, what);
}

static void expect_host_field(const char *line, const char *key, const char *want)
{
    char got[96] = "";
    char what[160];

    host_field(line, key, got, sizeof got);
    snprintf(what, sizeof what, "the host is told %s=%s", key, want);
    check(line && strcmp(got, want) == 0, what);
}

/* ------------------------------------------------------------------- tests */

/** The bay's own facts, as the editor is expected to leave them. */
static void bay_facts(char *material, size_t mn, char *vendor, size_t vn)
{
    const mmu_slot_t *sp = &g_mmu.units[mmu_this_unit()].slots[BAY];

    snprintf(material, mn, "%s", sp->material);
    snprintf(vendor, vn, "%s", sp->brand);
}

/** A material tap must move the material and nothing else. The vendor is the
 *  bay's own, and the line has to keep naming it - also when Spoolman's vendor
 *  list lands while the editor is open, which is what happens on a real
 *  printer: the editor opens on the list it has, and the host's answer
 *  replaces it a moment later. */
static void test_material_keeps_the_vendor(void)
{
    static const char *const spoolman[] = { "Bambu Lab", "Prusament" };
    char material[24], vendor[24];
    const char *line;

    /* 1. nothing changes under the editor */
    check(open_edit(BAY), "the editor opens on A2");
    bay_facts(material, sizeof material, vendor, sizeof vendor);
    line = step_and_save(view_str(VS_EDIT_MATERIAL), true, 1);
    report_line(line);
    check(line != NULL, "a material tap makes Save available, and it saves");
    expect_field(line, "material", "9");   /* PETG-CF (8) + one tap = PVA (9) */
    expect_vendor(line, vendor);
    expect_host_field(line, "material", "PVA");
    expect_host_vendor(line, vendor);

    /* 2. Spoolman's list arrives while the editor is open */
    check(open_edit(BAY), "the editor opens again on A2");
    bay_facts(material, sizeof material, vendor, sizeof vendor);
    vendors_arrive(spoolman, 2);
    line = step_and_save(view_str(VS_EDIT_MATERIAL), true, 1);
    report_line(line);
    check(line != NULL, "a material tap after the vendor list arrives saves");
    expect_field(line, "material", "9");
    expect_vendor(line, vendor);
    expect_host_field(line, "material", "PVA");
    expect_host_vendor(line, vendor);
}

/** The vendor stepper moves the vendor and nothing else. */
static void test_vendor_step_keeps_the_material(void)
{
    static const char *const spoolman[] = { "Bambu Lab", "Prusament" };
    const char *line;

    check(open_edit(BAY), "the editor opens on A2");
    vendors_arrive(spoolman, 2);
    line = step_and_save(view_str(VS_EDIT_VENDOR), true, 1);
    report_line(line);
    check(line != NULL, "a vendor tap makes Save available, and it saves");
    expect_vendor(line, "Bambu Lab");
    expect_host_vendor(line, "Bambu Lab");
    expect_host_field(line, "material", "PETG-CF");
}

/** The color path touches the color alone: a picked color must not drag the
 *  material or the vendor with it. */
static void test_color_keeps_the_other_fields(void)
{
    static const char *const spoolman[] = { "Bambu Lab", "Prusament" };
    const char *line;

    check(open_edit(BAY), "the editor opens on A2");
    vendors_arrive(spoolman, 2);
    /* open the palette sheet and take its first swatch */
    click(find_button(lv_screen_active(), view_str(VS_EDIT_COLOR)));
    settle(10);
    check(lv_obj_get_child_count(lv_layer_top()) > 0, "the palette sheet opens");
    check(first_swatch(lv_layer_top()) != NULL, "the sheet offers the whole palette");
    capture_reset();
    click(first_swatch(lv_layer_top()));
    settle(2);
    check(captured_edit() == NULL, "picking a color does not save by itself");

    click(find_button(lv_screen_active(), view_str(VS_ACTION_SAVE_EDIT)));
    settle(2);
    line = captured_edit();
    report_line(line);
    check(line != NULL, "the color change saves");
    expect_host_field(line, "color_hex", "FFFFFF");
    expect_host_field(line, "material", "PETG-CF");
    expect_host_vendor(line, "Generic");
}

/** Reset clears the bay's own facts and nothing else: the neighbour bay keeps
 *  its own, and the editor's fields go back to the empty bay's. */
static void test_reset_clears_only_this_bay(void)
{
    const mmu_slot_t *a1 = &g_mmu.units[mmu_this_unit()].slots[0];
    char a1_material[24], a1_brand[24];

    check(open_edit(BAY), "the editor opens on A2");
    snprintf(a1_material, sizeof a1_material, "%s", a1->material);
    snprintf(a1_brand, sizeof a1_brand, "%s", a1->brand);

    /* open the reset confirm (modal 1) and accept it */
    {
        const ui_screen_ops_t *ops = ui_screen_ops(UI_SCREEN_EDIT);

        click(find_button(lv_screen_active(), view_str(VS_BTN_RESET)));
        settle(2);
        click(find_button(lv_layer_top(), view_str(VS_BTN_RESET)));
        settle(2);
        (void)ops;
    }

    check(g_mmu.units[mmu_this_unit()].slots[BAY].material[0] == '\0',
          "reset clears this bay's material");
    check(g_mmu.units[mmu_this_unit()].slots[BAY].brand[0] == '\0',
          "reset clears this bay's vendor");
    check(g_mmu.units[mmu_this_unit()].slots[BAY].color_known == false,
          "reset clears this bay's color");
    check(strcmp(a1->material, a1_material) == 0 && strcmp(a1->brand, a1_brand) == 0,
          "reset leaves the neighbouring bay alone");
}

/** The first label under @p root whose text contains @p needle. */
static lv_obj_t *find_label_containing(lv_obj_t *root, const char *needle)
{
    uint32_t i, n;

    if(!root) return NULL;
    if(lv_obj_check_type(root, &lv_label_class)) {
        const char *t = lv_label_get_text(root);

        if(t && strstr(t, needle)) return root;
    }
    n = lv_obj_get_child_count(root);
    for(i = 0; i < n; i++) {
        lv_obj_t *hit = find_label_containing(lv_obj_get_child(root, i), needle);

        if(hit) return hit;
    }
    return NULL;
}

/* The load screen, live: a refused load says the host's words, whole and
 * wrapped, instead of "Nothing is loading" (found on a cold printer, which
 * answers a load with a sentence longer than one line); a load just sent says
 * it is starting. */
static void test_load_screen_tells_the_truth(void)
{
    const char *why = "Heat the extruder before an OpenAMS toolchange. "
                      "Cutting and reloading need extrusion temperature";
    lv_obj_t *l;

    mmu_model_set_scenario("idle");
    mmu_model_set_live(true);
    ui_theme_init();
    ui_nav_init();

    mmu_model_action_sent(&g_mmu, MMU_ACTION_LOAD);
    check(ui_nav_goto(UI_SCREEN_LOAD), "the load screen opens");
    settle(5);
    check(find_label_containing(lv_screen_active(), view_str(VS_LOAD_STARTING)) != NULL,
          "a load just sent says it is starting");
    check(find_label_containing(lv_screen_active(), view_str(VS_LOAD_NOTHING)) == NULL,
          "  and not that nothing is loading");

    mmu_model_action_result(&g_mmu, MMU_ACTION_LOAD, false, why);
    ui_nav_refresh();
    settle(5);
    l = find_label_containing(lv_screen_active(), why);
    check(l != NULL, "a refused load shows the host's whole refusal");
    check(l && lv_label_get_long_mode(l) == LV_LABEL_LONG_MODE_WRAP, "  wrapped, not cut to one line");
    check(find_label_containing(lv_screen_active(), view_str(VS_LOAD_NOTHING)) == NULL,
          "  and not that nothing is loading");
}

/* The load screen after a taken unload (owner: "it did it, but stayed on
 * starting, then it timed out, even as the event took place"): once the host
 * reports the unload, what the display sent is no longer pending, so when the
 * unload ends the screen does not go back to "Starting" and later say the
 * host never reported it. The same holds when the host's "ok" is all there is. */
static void test_unload_ends_where_it_ended(void)
{
    mmu_toolhead_t *th;
    int unit, thi;

    mmu_model_set_scenario("idle");
    mmu_model_set_live(true);
    ui_theme_init();
    ui_nav_init();
    unit = mmu_this_unit();
    thi = g_mmu.units[unit].toolhead;
    check(thi >= 0, "this unit has a toolhead");
    th = &g_mmu.toolheads[thi];

    /* sent, then reported: the host is unloading */
    mmu_model_action_sent(&g_mmu, MMU_ACTION_UNLOAD);
    check(ui_nav_goto(UI_SCREEN_LOAD), "the load screen opens");
    th->busy = MMU_BUSY_UNLOAD;
    th->busy_unit = unit;
    th->busy_slot = 0;
    mmu_model_refresh_this(&g_mmu);
    check(g_mmu.action_pending_kind == MMU_ACTION_OTHER,
          "a reported unload is no longer pending");

    /* finished, then answered */
    th->busy = MMU_BUSY_NONE;
    th->busy_unit = -1;
    th->busy_slot = -1;
    mmu_model_refresh_this(&g_mmu);
    mmu_model_action_result(&g_mmu, MMU_ACTION_UNLOAD, true, "");
    ui_nav_refresh();
    settle(5);
    check(find_label_containing(lv_screen_active(), view_str(VS_LOAD_STARTING)) == NULL,
          "a finished unload does not go back to starting");
    check(find_label_containing(lv_screen_active(), view_str(VS_LOAD_NO_REPORT)) == NULL,
          "  nor say the host never reported it");

    /* an answer with no report seen (an operation shorter than one update) */
    mmu_model_action_sent(&g_mmu, MMU_ACTION_UNLOAD);
    check(g_mmu.action_pending_kind == MMU_ACTION_UNLOAD, "a new unload is pending");
    mmu_model_action_result(&g_mmu, MMU_ACTION_UNLOAD, true, "");
    check(g_mmu.action_pending_kind == MMU_ACTION_OTHER,
          "the host's ok ends the wait: the script has run");
}

/* Moonraker answers a load's G-code only once it has run. A load the host
 * reported running and then finished, answered late with "timeout" (the
 * display gave up waiting) or with an error from mid-run, did not fail to
 * start: the load screen ends on the load, not on "Could not start loading".
 * A refusal before anything ran is still said. */
static void test_load_answered_after_it_ran(void)
{
    mmu_toolhead_t *th;
    int unit, thi;
    char refused[96];

    mmu_model_set_scenario("idle");
    mmu_model_set_live(true);
    ui_theme_init();
    ui_nav_init();
    unit = mmu_this_unit();
    thi = g_mmu.units[unit].toolhead;
    th = &g_mmu.toolheads[thi];
    snprintf(refused, sizeof refused, "%s", view_str(VS_LOAD_REFUSED));

    /* sent, reported running, finished: then the late answer */
    mmu_model_action_sent(&g_mmu, MMU_ACTION_LOAD);
    check(ui_nav_goto(UI_SCREEN_LOAD), "the load screen opens");
    th->busy = MMU_BUSY_LOAD;
    th->busy_unit = unit;
    th->busy_slot = 1;
    mmu_model_refresh_this(&g_mmu);
    th->busy = MMU_BUSY_NONE;
    th->busy_unit = -1;
    th->busy_slot = -1;
    mmu_model_refresh_this(&g_mmu);
    mmu_model_action_result(&g_mmu, MMU_ACTION_LOAD, false, "timeout");
    check(g_mmu.action_refusal[0] == '\0', "a late timeout for a load that ran is not a refusal");
    ui_nav_refresh();
    settle(5);
    check(find_label_containing(lv_screen_active(), refused) == NULL,
          "  and the load screen does not say it could not start");

    /* an error from mid-run is the run's outcome, which the status shows */
    mmu_model_action_sent(&g_mmu, MMU_ACTION_LOAD);
    th->busy = MMU_BUSY_LOAD;
    th->busy_unit = unit;
    th->busy_slot = 1;
    mmu_model_refresh_this(&g_mmu);
    mmu_model_action_result(&g_mmu, MMU_ACTION_LOAD, false, "Hub sensor did not trigger");
    check(g_mmu.action_refusal[0] == '\0', "an error after the load started is not a refusal to start");
    th->busy = MMU_BUSY_NONE;
    th->busy_unit = -1;
    th->busy_slot = -1;
    mmu_model_refresh_this(&g_mmu);

    /* refused before anything ran: said, in the host's words */
    mmu_model_action_sent(&g_mmu, MMU_ACTION_LOAD);
    mmu_model_action_result(&g_mmu, MMU_ACTION_LOAD, false, "Heat the extruder first");
    check(strcmp(g_mmu.action_refusal, "Heat the extruder first") == 0,
          "a refusal before the load started is still said");
    ui_nav_refresh();
    settle(5);
    check(find_label_containing(lv_screen_active(), refused) != NULL,
          "  on the load screen");
}

/* ----------------------------------------------- save and load (unknown bay) */

/** The `load ...` lines captured since the last reset. */
static int loads_sent(void)
{
    int i, n = 0;

    for(i = 0; i < s_capture_count; i++)
        if(strncmp(s_capture[i], "load ", 5) == 0) n++;
    return n;
}

/** Open the editor in its save-and-load mode on an unidentified bay, the way
 *  the Load row of its sheet does. */
static bool open_save_and_load(void)
{
    mmu_slot_t *sp;

    mmu_model_set_scenario("idle");
    mmu_model_set_live(true);
    g_mmu.spoolman_edit = true;
    g_mmu.spoolman_online = true;
    sp = &g_mmu.units[mmu_this_unit()].slots[BAY];
    sp->state = MMU_SLOT_READY;
    sp->info_source = MMU_INFO_UNKNOWN;
    sp->spool_id = -1;
    sp->material[0] = '\0';
    mmu_home_set_selection(mmu_this_unit(), BAY, -1);
    ui_theme_init();
    ui_nav_init();
    capture_reset();
    screen_edit_save_and_load();
    return ui_nav_goto(UI_SCREEN_EDIT);
}

/* The SDL window drives LVGL's clock from the wall clock, and a test that waits
 * for the editor's deadline has no business sleeping for it: the clock is the
 * test's own from the first save-and-load test on, and moves only in wait_ms(). */
static uint32_t s_clock;

static uint32_t clock_now(void)
{
    return s_clock;
}

/** Let the editor's answer timer run for @p ms of LVGL time. */
static void wait_ms(uint32_t ms)
{
    s_clock += ms;
    settle(2);
}

/* The Load row of an unidentified bay: the button says what it does, nothing
 * goes out until a material is chosen, the load follows the host's answer and
 * not the tap, and a refusal or silence sends no load (PRINCIPLES.md 13). */
static void test_save_and_load(void)
{
    lv_obj_t *scr;
    const char *line;
    char got[96];

    s_clock = lv_tick_get();
    lv_tick_set_cb(clock_now);
    check(open_save_and_load(), "the editor opens in save-and-load mode");
    scr = lv_screen_active();
    check(find_label(scr, view_str(VS_SAVE_AND_LOAD)) != NULL, "the button reads \"Save and load\"");
    check(find_label(scr, view_str(VS_ACTION_SAVE_EDIT)) == NULL, "  and not \"Save edit\"");
    check(find_label(scr, view_str(VS_MATERIAL_CHOOSE)) != NULL, "the material starts on \"Choose a material\"");

    /* no material: the button is dead, nothing is sent */
    click(find_button(scr, view_str(VS_SAVE_AND_LOAD)));
    settle(2);
    check(captured_edit() == NULL && loads_sent() == 0, "nothing is sent before a material is chosen");

    /* a step down from the unset choice lands on a real material, never on the gap */
    click(find_stepper_button(scr, view_str(VS_EDIT_MATERIAL), false));
    settle(2);
    check(find_label(scr, view_str(VS_MATERIAL_CHOOSE)) == NULL, "stepping back from the choice picks a material");
    click(find_stepper_button(scr, view_str(VS_EDIT_MATERIAL), true));
    settle(2);
    check(find_label(scr, view_str(VS_MATERIAL_CHOOSE)) != NULL, "and stepping forward returns to the choice");

    /* choose one and save: the edit goes out, the load does not (yet) */
    click(find_stepper_button(scr, view_str(VS_EDIT_MATERIAL), true));
    settle(2);
    capture_reset();
    click(find_button(scr, view_str(VS_SAVE_AND_LOAD)));
    settle(2);
    line = captured_edit();
    report_line(line);
    check(line != NULL, "a chosen material sends the edit");
    if(line) {
        check(strstr(line, " then load") != NULL, "  as a save that ends in a load");
        expect_field(line, "material", "0");
        host_rpc_string(line, "material", got, sizeof got);
        check(strcmp(got, mmu_material_name(0)) == 0, "  and the host is told that material");
    }
    check(loads_sent() == 0, "no load goes out with the tap");

    /* the host's ok releases exactly one load, for this bay */
    wait_ms(500);
    check(loads_sent() == 0, "no load goes out while the host has not answered");
    mmu_model_edit_result(&g_mmu, NULL);
    wait_ms(500);
    check(loads_sent() == 1, "the host's ok sends one load");
    check(s_capture_count > 0 && strcmp(s_capture[s_capture_count - 1], "load A2") == 0,
          "  for this bay, after the edit");
    wait_ms(2000);
    check(loads_sent() == 1, "and only one");
}

static void test_save_and_load_refused(void)
{
    lv_obj_t *scr;

    check(open_save_and_load(), "the editor opens in save-and-load mode again");
    scr = lv_screen_active();
    click(find_stepper_button(scr, view_str(VS_EDIT_MATERIAL), true));
    settle(2);
    capture_reset();
    click(find_button(scr, view_str(VS_SAVE_AND_LOAD)));
    settle(2);
    check(captured_edit() != NULL, "the edit goes out");

    mmu_model_edit_result(&g_mmu, "Spoolman is offline");
    wait_ms(500);
    check(loads_sent() == 0, "a refused edit sends no load");
    check(lv_screen_active() == scr, "  and the editor stays");
    wait_ms(15000);
    check(loads_sent() == 0, "  not even later");

    /* the values were kept, and the button works again */
    capture_reset();
    click(find_button(scr, view_str(VS_SAVE_AND_LOAD)));
    settle(2);
    check(captured_edit() != NULL, "after a refusal the same save can be tried again");
    mmu_model_edit_result(&g_mmu, NULL);
    wait_ms(500);
    check(loads_sent() == 1, "and its ok sends the load");
}

static void test_save_and_load_no_answer(void)
{
    lv_obj_t *scr;

    check(open_save_and_load(), "the editor opens in save-and-load mode for a silent host");
    scr = lv_screen_active();
    click(find_stepper_button(scr, view_str(VS_EDIT_MATERIAL), true));
    settle(2);
    capture_reset();
    click(find_button(scr, view_str(VS_SAVE_AND_LOAD)));
    settle(2);
    wait_ms(5000);
    check(loads_sent() == 0, "no load while the host is quiet");
    wait_ms(8000);
    check(loads_sent() == 0, "no load when the wait ends");
    check(lv_screen_active() == scr, "  and the editor stays");
    /* a late answer, after the wait gave up, is nobody's: no load from it */
    mmu_model_edit_result(&g_mmu, NULL);
    wait_ms(1000);
    check(loads_sent() == 0, "a late answer to a wait that ended sends no load");
}

/** The plain editor is the editor it always was: arming one entry does not leak
 *  into the next. */
static void test_edit_after_save_and_load_is_plain(void)
{
    check(open_save_and_load(), "save-and-load opens");
    check(open_edit(BAY), "the plain editor opens next");
    check(find_label(lv_screen_active(), view_str(VS_ACTION_SAVE_EDIT)) != NULL,
          "it says \"Save edit\"");
    check(find_label(lv_screen_active(), view_str(VS_SAVE_AND_LOAD)) == NULL, "  not \"Save and load\"");
}

/* True when a tap on @p row opens the change-group sheet. A tappable ui_kit row
 * is the one that carries a callback, and LVGL 9 makes every object clickable
 * by default (lv_obj_is_clickable() is true for a row with no callback at all),
 * so the tap itself is what tells the two apart. The sheet is taken back down
 * again, so the next row is asked on a clean top layer. */
static bool taps_open_the_picker(lv_obj_t *row)
{
    uint32_t before = lv_obj_get_child_count(lv_layer_top());
    bool opened;

    if(!row) return false;
    click(row);
    settle(2);
    opened = lv_obj_get_child_count(lv_layer_top()) > before;
    lv_obj_clean(lv_layer_top());
    return opened;
}

/* The Groups screen draws the view's rows and nothing of the model's: a
 * group's name as its own section, this unit's bays named like their tiles,
 * another unit's named with its unit, and this unit's bays in no group under
 * their own heading. The rows a user can rearrange from are this unit's own,
 * and only those. */
static void test_groups_screen_draws_the_view(void)
{
    static const char *const g_names[] = { "T0", "T7" };
    char want[64];
    lv_obj_t *scr, *label;
    int other = 1;                 /* unit B, not the one this display is bound to */
    int g0, g1;
    int i;

    mmu_model_set_scenario("idle");
    mmu_model_set_live(false);     /* the model's group helpers refuse while it is live */
    g_mmu.group_count = 0;
    g0 = mmu_group_create("T0");
    mmu_group_assign(g0, mmu_this_unit(), 0);    /* A1: this unit's, and the loaded one */
    mmu_group_assign(g0, other, 1);              /* B2: the group's backup on another unit */
    g1 = mmu_group_create("T7");
    mmu_group_assign(g1, mmu_this_unit(), 1);    /* A2: this unit's, and T7's only member */
    check(g0 >= 0 && g1 == g0 + 1, "the model takes the two groups");

    ui_theme_init();
    ui_nav_init();
    capture_reset();
    scr = screen_refill_ops.build();
    check(scr != NULL, "the Groups screen builds");

    for(i = 0; i < (int)(sizeof g_names / sizeof g_names[0]); i++)
        check(find_label(scr, g_names[i]) != NULL, "each group names its own section");

    vs_spool_label(0, want, sizeof want);        /* A1 */
    label = find_label(scr, want);
    check(label != NULL, "this unit's grouped bay is labeled like its tile");
    check(taps_open_the_picker(label ? lv_obj_get_parent(label) : NULL),
          "  and its row opens the change-group sheet");

    vs_unit_spool(mmu_unit_name(other), 1, want, sizeof want);   /* B2 */
    label = find_label(scr, want);
    check(label != NULL, "another unit's member is labeled with its unit");
    check(!taps_open_the_picker(label ? lv_obj_get_parent(label) : NULL),
          "  and is nothing to rearrange from here");

    check(find_label(scr, view_str(VS_GROUP_NOT_IN)) != NULL,
          "the bays in no group have their own section");
    vs_spool_label(2, want, sizeof want);        /* A3: in no group */
    label = find_label(scr, want);
    check(label != NULL, "  and it lists the ungrouped bay");
    check(taps_open_the_picker(label ? lv_obj_get_parent(label) : NULL),
          "  whose row opens the change-group sheet too");
}

int main(void)
{
    lv_init();
    {
        lv_display_t *disp = lv_sdl_window_create(480, 320);

        (void)disp;
        lv_sdl_mouse_create();
    }

    mmu_model_init();
    test_material_keeps_the_vendor();
    test_vendor_step_keeps_the_material();
    test_color_keeps_the_other_fields();
    test_reset_clears_only_this_bay();
    test_load_screen_tells_the_truth();
    test_unload_ends_where_it_ended();
    test_load_answered_after_it_ran();
    test_save_and_load();
    test_save_and_load_refused();
    test_save_and_load_no_answer();
    test_edit_after_save_and_load_is_plain();
    test_groups_screen_draws_the_view();

    printf("%s: %d failure(s)\n", s_failures ? "FAILED" : "PASSED", s_failures);
    return s_failures ? 1 : 0;
}