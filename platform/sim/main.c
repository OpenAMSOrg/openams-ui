/**
 * @file main.c
 * SDL simulator: a 480x320 window (zoomed 4x by default, 2x on Windows, --zoom N
 * always overrides) for interactive use, plus two
 * automated modes the other screens are checked against:
 *
 *   --scenario NAME       start on one scenario (default "idle").
 *   --unit NAME           bind the display to one unit ("A", "B", "HT-A"),
 *                         as it would be mounted on that unit in production.
 *   --lang CODE           draw the UI in that language ("de", "es", "fr", "it",
 *                         "pl", "pt-BR"; every code view_lang_code() lists is
 *                         accepted). Default English. A code this build does
 *                         not carry (-DMMU_LANGUAGES) says so on stderr and
 *                         falls back to English, the way the device does; a
 *                         code no build has (a typo) is an error.
 *   --smoke              walk every scenario x every unit of that scenario x
 *                         every screen x every modal, print `SMOKE OK ...` and
 *                         exit 0, or exit 1 on the first screen that fails to
 *                         build or if LVGL's pool peak exceeds 90% of
 *                         LV_MEM_SIZE.
 *   --screenshots DIR     the same walk, writing one 24-bit BMP per
 *                         scenario/unit/screen and per modal into DIR.
 *   --gallery DIR         render every component-gallery page to
 *                         DIR/gallery_<name>.bmp, audit each one, print
 *                         `GALLERY OK issues=<n>` and exit 0 when clean.
 *   --bench-builds N      build every screen N times on the model state the
 *                         smoke walk starts from, and print one
 *                         `bench <screen> build_us=<avg> draw_us=<avg>` line
 *                         per screen, where the draw is one forced full
 *                         redraw. Where the build and the draw of a screen go
 *                         is what the responsiveness work turns on.
 *   --replay FILE         replay a recorded fixture (a JSON file in tests/fixtures/):
 *                         start from an empty model and apply each event once
 *                         its `t` has elapsed in real time, scaled by
 *                         --replay-speed. Screens then render the live model.
 *   --replay-speed X      real-time multiplier for --replay (default 1.0).
 *   --dump-model          apply every event of the --replay fixture at once,
 *                         print a compact text dump of the key model fields
 *                         and exit 0 without opening a window.
 *   --dump-view           the same replay, through core_apply() (so
 *                         oams_manager/lanes_by_fps reaches the model, unlike
 *                         --dump-model's older path), printing the view
 *                         builders' own JSON (view_json_build(), pretty) and
 *                         exiting 0 (tests/replay_view_goldens.sh).
 *   --moonraker URL       mirror a live AMS over Moonraker's WebSocket
 *                         JSON-RPC API (ws://host:port/path) instead of a
 *                         built-in scenario or a fixture.
 *   --bind-unit NAME      with --moonraker: bind to the `openams_ui NAME`
 *                         object instead of the first one the server lists.
 *   --dump-model-after S  with --moonraker: after S seconds, print the same
 *                         dump as --dump-model and exit 0 (headless, for
 *                         tests; no window is opened).
 *   --dump-view-after S   with --moonraker: the same, printing --dump-view's
 *                         JSON instead.
 *   --action LINE         with --moonraker and --dump-model-after: once the
 *                         link is READY, inject one UI line exactly as a
 *                         screen would log it (`load oams11`), so its host
 *                         G-code goes out; repeatable, at most 8.
 *
 * Both automated modes drive lv_tick themselves (see manual_tick_cb) so the
 * walk is deterministic and fast instead of depending on wall-clock time.
 */
#ifdef _WIN32
/* Keep a normal main() (and a console window) instead of the SDL_main
 * rename + real WinMain entry point that linking SDL2main would require. */
#define SDL_MAIN_HANDLED
#include <windows.h>
#endif

#include "lvgl.h"
#include "SDL2/SDL.h"

#include "app/actions.h"
#include "app/app.h"
#include "app/app_live.h"
#include "app/platform.h"
#include "audit.h"
#include "backend/actions_map.h"
#include "backend/moonraker_client.h"
#include "backend/status_map.h"
#include "core/core.h"
#include "gallery.h"
#include "lib/cjson/cJSON.h"
#include "model/mmu_model.h"
#include "sim.h"
#include "ui/ui_language.h"
#include "ui/ui_nav.h"
#include "ui/ui_screens.h"
#include "view/strings_langs.gen.h"
#include "view/strings.h"
#include "view/view_json.h"
#include "ws_client.h"

#include "ui/ui_theme.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
/* A tester has no console history to scroll back through, so every ACTION
 * line also lands next to the exe, in a file they can attach when reporting
 * back what they saw. */
static FILE *s_action_log;

static void open_action_log(void)
{
    char exe_path[MAX_PATH];
    char *last_sep;
    DWORD n = GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));

    if(n == 0 || n >= sizeof(exe_path)) return;

    last_sep = strrchr(exe_path, '\\');
    if(!last_sep) last_sep = strrchr(exe_path, '/');

    if(last_sep) {
        char log_path[MAX_PATH + 32];
        size_t dir_len = (size_t)(last_sep - exe_path) + 1;

        memcpy(log_path, exe_path, dir_len);
        snprintf(log_path + dir_len, sizeof(log_path) - dir_len, "openams-ui-actions.log");
        s_action_log = fopen(log_path, "a");
    }
}
#endif

/* No physical touchscreen to calibrate here: web and Windows builds use
 * this same platform/sim, so one no-op covers all three. */
void platform_touch_calibrate(void)
{
    printf("touch calibration (simulator: no-op)\n");
    fflush(stdout);
}

/* ------------------------------------------------------------ host actions */

/* The --moonraker URL. Declared up here because platform_action() has to know
 * whether a live link is around before it turns a UI line into G-code. */
static const char *s_moonraker_url;

/* --action: UI lines to inject through platform_action() once the link is
 * ready, so a test can drive host actions without a mouse. */
#define SIM_MAX_ACTIONS 8
static const char *s_actions[SIM_MAX_ACTIONS];
static int s_action_count;
static bool s_actions_sent;

/* What each outstanding G-code request was, so the answer can name the action
 * it is about (the same small ring the ESP32 link keeps). */
#define SIM_SENT_KINDS 8
static int s_sent_id[SIM_SENT_KINDS];
static mmu_action_kind_t s_sent_kind[SIM_SENT_KINDS];
static int s_sent_next;

static void sim_sent_remember(int id, mmu_action_kind_t kind)
{
    if(id < 0) return;
    s_sent_id[s_sent_next] = id;
    s_sent_kind[s_sent_next] = kind;
    s_sent_next = (s_sent_next + 1) % SIM_SENT_KINDS;
}

static mmu_action_kind_t sim_sent_kind_of(int id)
{
    int i;

    for(i = 0; i < SIM_SENT_KINDS; i++)
        if(s_sent_id[i] == id) return s_sent_kind[i];
    return MMU_ACTION_OTHER;
}

/* The live Moonraker link's core, defined with the link below:
 * platform_action() marks a load or an unload pending on it. */
static core_t *s_core;

void platform_action(const char *line)
{
    char out[128];
    char rpc_method[64];
    mmu_action_kind_t kind;
    cJSON *rpc_params = NULL;
    const char *why = "";

    printf("ACTION: %s\n", line);
    fflush(stdout);
#ifdef _WIN32
    if(s_action_log) {
        fprintf(s_action_log, "ACTION: %s\n", line);
        fflush(s_action_log);
    }
#endif

    /* Only when a live link is configured: a line the screens logged that
     * names something the host owns is its business, not the demo model's.
     * The local lines (nav, sheets, toasts) map to 0 and stop here. */
    if(!s_moonraker_url) return;

    /* A load or an unload is worth waiting on: the load screen says it is
     * starting until the host reports the operation, instead of claiming
     * nothing is loading. */
    kind = mmu_action_kind_of(line);

    switch(actions_map(line, &g_mmu, out, sizeof out)) {
    case 1: {
        /* A load or an unload is answered once it has run, so it gets that long. */
        int id = (kind == MMU_ACTION_OTHER) ? mc_send_gcode(out)
                                            : mc_send_gcode_within(out, MC_GCODE_RUN_TIMEOUT_MS);

        printf("GCODE: %s\n", out);
        if(id < 0) {
            printf("GCODE-REFUSED: link not ready\n");
        }
        else {
            sim_sent_remember(id, kind);
            core_action_sent(s_core, kind);
        }
        fflush(stdout);
        return;
    }
    case -1:
        printf("ACTION-UNMAPPED: %s\n", out);
        fflush(stdout);
        return;
    default:
        break;
    }

    /* G-code did not match; try RPC. */
    switch(actions_rpc_why(line, &g_mmu, rpc_method, sizeof rpc_method, &rpc_params, &why)) {
    case 1:
        printf("RPC: %s\n", rpc_method);
        if(mc_send_rpc(rpc_method, rpc_params, actions_rpc_tag(rpc_method, rpc_params)) < 0)
            printf("RPC-REFUSED: link not ready\n");
        break;
    case -1:
        printf("ACTION-UNMAPPED: %s\n", why);
        break;
    default:
        break;
    }
    fflush(stdout);
}

/* -------------------------------------------------------- manual tick/pump */

/* --smoke / --screenshots advance a fake clock instead of SDL_GetTicks, so
 * timers and animations run the same way on every machine and every run. */
static uint32_t s_manual_ms;

/* --audit: run the layout audit at every stop of the walk. */
static bool s_audit;
static int s_audit_issues;

static uint32_t manual_tick_cb(void)
{
    return s_manual_ms;
}

static void pump(int n)
{
    int i;

    for(i = 0; i < n; i++) {
        s_manual_ms += 33;
        lv_timer_handler();
    }
}

/* exposed to simulator-only tooling (the gallery): same pump, same clock */
void sim_pump(int n)
{
    pump(n);
}

/* ------------------------------------------------------------- BMP encoder */

static void put_u16(FILE *f, uint16_t v)
{
    fputc((int)(v & 0xFFU), f);
    fputc((int)((v >> 8) & 0xFFU), f);
}

static void put_u32(FILE *f, uint32_t v)
{
    fputc((int)(v & 0xFFU), f);
    fputc((int)((v >> 8) & 0xFFU), f);
    fputc((int)((v >> 16) & 0xFFU), f);
    fputc((int)((v >> 24) & 0xFFU), f);
}

/* 24-bit BGR, bottom-up, rows padded to 4 bytes: the classic uncompressed
 * BITMAPFILEHEADER + BITMAPINFOHEADER pair. LVGL's RGB888 draw buffers are
 * already B,G,R per pixel, so a row copies out unchanged. */
static bool write_bmp(const char *path, const uint8_t *bgr, int w, int h, int src_stride)
{
    static const uint8_t zero[4];
    FILE *f;
    int row_bytes = w * 3;
    int padded = (row_bytes + 3) & ~3;
    uint32_t data_size = (uint32_t)padded * (uint32_t)h;
    int y;

    f = fopen(path, "wb");
    if(!f) return false;

    fputc('B', f);
    fputc('M', f);
    put_u32(f, 14u + 40u + data_size);
    put_u32(f, 0);
    put_u32(f, 14u + 40u);

    put_u32(f, 40);
    put_u32(f, (uint32_t)w);
    put_u32(f, (uint32_t)h); /* positive height = bottom-up rows */
    put_u16(f, 1);
    put_u16(f, 24);
    put_u32(f, 0); /* BI_RGB, no compression */
    put_u32(f, data_size);
    put_u32(f, 2835); /* ~72 DPI */
    put_u32(f, 2835);
    put_u32(f, 0);
    put_u32(f, 0);

    for(y = h - 1; y >= 0; y--) {
        const uint8_t *row = bgr + (size_t)y * (size_t)src_stride;
        fwrite(row, 1, (size_t)row_bytes, f);
        if(padded > row_bytes) fwrite(zero, 1, (size_t)(padded - row_bytes), f);
    }

    fclose(f);
    return true;
}

/* --------------------------------------------------------------- snapshots */

/* lv_snapshot_take() allocates its draw buffer through LVGL's own 64KB pool
 * (LV_MEM_SIZE) — fine for the small buffers the rest of the UI needs, but a
 * 320x240 screen at 3-4 bytes/px (230-307KB) can never fit in it. Snapshots
 * are simulator-only tooling (never shipped to the ESP32), so we hand
 * lv_snapshot_take_to_draw_buf() our own statically-allocated buffers
 * instead, bypassing that pool entirely. */
static uint8_t s_screen_pixels[UI_SCREEN_W * UI_SCREEN_H * 3]; /* RGB888 */
static uint8_t s_top_pixels[UI_SCREEN_W * UI_SCREEN_H * 4];    /* ARGB8888 */
static lv_draw_buf_t s_screen_buf;
static lv_draw_buf_t s_top_buf;

/* Modals live on lv_layer_top(), which a screen snapshot does not include;
 * blend it over the screen image ourselves using its alpha channel. */
static void blend_top_layer(lv_draw_buf_t *screen)
{
    uint32_t stride = lv_draw_buf_width_to_stride(UI_SCREEN_W, LV_COLOR_FORMAT_ARGB8888);
    uint32_t x, y;

    lv_draw_buf_init(&s_top_buf, UI_SCREEN_W, UI_SCREEN_H, LV_COLOR_FORMAT_ARGB8888, stride,
                     s_top_pixels, sizeof s_top_pixels);
    if(lv_snapshot_take_to_draw_buf(lv_layer_top(), LV_COLOR_FORMAT_ARGB8888, &s_top_buf) !=
       LV_RESULT_OK) {
        return;
    }

    for(y = 0; y < screen->header.h && y < s_top_buf.header.h; y++) {
        uint8_t *srow = screen->data + (size_t)y * screen->header.stride;
        const uint8_t *trow = s_top_buf.data + (size_t)y * s_top_buf.header.stride;

        for(x = 0; x < screen->header.w && x < s_top_buf.header.w; x++) {
            uint8_t *sp = srow + (size_t)x * 3;
            const uint8_t *tp = trow + (size_t)x * 4;
            uint8_t a = tp[3];

            if(a == 0) continue;
            if(a == 255) {
                sp[0] = tp[0];
                sp[1] = tp[1];
                sp[2] = tp[2];
                continue;
            }
            sp[0] = (uint8_t)(((int)tp[0] * a + (int)sp[0] * (255 - a)) / 255);
            sp[1] = (uint8_t)(((int)tp[1] * a + (int)sp[1] * (255 - a)) / 255);
            sp[2] = (uint8_t)(((int)tp[2] * a + (int)sp[2] * (255 - a)) / 255);
        }
    }
}

static bool capture_shot(const char *dir, const char *prefix, const char *screen,
                         int modal_index)
{
    uint32_t stride = lv_draw_buf_width_to_stride(UI_SCREEN_W, LV_COLOR_FORMAT_RGB888);
    char path[512];

    lv_draw_buf_init(&s_screen_buf, UI_SCREEN_W, UI_SCREEN_H, LV_COLOR_FORMAT_RGB888, stride,
                     s_screen_pixels, sizeof s_screen_pixels);
    if(lv_snapshot_take_to_draw_buf(lv_screen_active(), LV_COLOR_FORMAT_RGB888, &s_screen_buf) !=
       LV_RESULT_OK) {
        return false;
    }

    if(modal_index >= 0) blend_top_layer(&s_screen_buf);

    if(modal_index >= 0) {
        snprintf(path, sizeof path, "%s/%s_%s_modal%d.bmp", dir, prefix, screen, modal_index);
    }
    else {
        snprintf(path, sizeof path, "%s/%s_%s.bmp", dir, prefix, screen);
    }

    return write_bmp(path, s_screen_buf.data, (int)s_screen_buf.header.w,
                     (int)s_screen_buf.header.h, (int)s_screen_buf.header.stride);
}

/* exposed to simulator-only tooling (the gallery): same snapshot, same blend,
 * same encoder as capture_shot, but to a caller-chosen path. */
bool sim_capture_bmp(const char *path, bool with_top_layer)
{
    uint32_t stride = lv_draw_buf_width_to_stride(UI_SCREEN_W, LV_COLOR_FORMAT_RGB888);

    lv_draw_buf_init(&s_screen_buf, UI_SCREEN_W, UI_SCREEN_H, LV_COLOR_FORMAT_RGB888, stride,
                     s_screen_pixels, sizeof s_screen_pixels);
    if(lv_snapshot_take_to_draw_buf(lv_screen_active(), LV_COLOR_FORMAT_RGB888, &s_screen_buf) !=
       LV_RESULT_OK) {
        return false;
    }

    if(with_top_layer) blend_top_layer(&s_screen_buf);

    return write_bmp(path, s_screen_buf.data, (int)s_screen_buf.header.w,
                     (int)s_screen_buf.header.h, (int)s_screen_buf.header.stride);
}

/* ---------------------------------------------------- the shared screen walk */

/* Visits every scenario x every unit of that scenario (as the display's
 * binding) x every screen x every modal. Used by --smoke (no capture, just
 * checks nothing fails and reports the memory peak) and by --screenshots
 * (same walk, writing a BMP at each stop). */
static bool run_walk(const char *out_dir, bool report_smoke)
{
    int scenario_count = app_scenario_count();
    int bindings_visited = 0;
    int screens_visited = 0;
    int modals_visited = 0;
    int si, u, id, mi;

    for(si = 0; si < scenario_count; si++) {
        mmu_model_set_scenario(app_scenario_name(si));

        for(u = 0; u < (int)g_mmu.unit_count; u++) {
            char prefix[48];

            mmu_set_this_unit(u);
            bindings_visited++;
            snprintf(prefix, sizeof prefix, "%s-%s", app_scenario_name(si),
                     g_mmu.units[u].name);

            for(id = 0; id < UI_SCREEN_COUNT; id++) {
                const ui_screen_ops_t *ops = ui_screen_ops((ui_screen_id_t)id);
                int modal_count;

                if(!ui_nav_goto((ui_screen_id_t)id)) {
                    fprintf(stderr, "WALK FAIL: scenario=%s unit=%s screen=%s failed to build\n",
                            app_scenario_name(si), g_mmu.units[u].name,
                            ui_nav_screen_name((ui_screen_id_t)id));
                    return false;
                }
                pump(60);
                screens_visited++;
                if(s_audit) {
                    char where[96];
                    snprintf(where, sizeof(where), "%s/%s", prefix,
                             ui_nav_screen_name((ui_screen_id_t)id));
                    s_audit_issues += audit_run(where);
                }

                if(out_dir) {
                    capture_shot(out_dir, prefix,
                                 ui_nav_screen_name((ui_screen_id_t)id), -1);
                }

                modal_count = (ops && ops->modal_count) ? ops->modal_count() : 0;
                for(mi = 0; mi < modal_count; mi++) {
                    ops->open_modal(mi);
                    pump(60);
                    modals_visited++;
                    if(s_audit) {
                        char where[96];
                        snprintf(where, sizeof(where), "%s/%s/modal%d", prefix,
                                 ui_nav_screen_name((ui_screen_id_t)id), mi);
                        s_audit_issues += audit_run(where);
                    }

                    if(out_dir) {
                        capture_shot(out_dir, prefix,
                                     ui_nav_screen_name((ui_screen_id_t)id), mi);
                    }

                    ui_nav_modal_close_all();
                }
            }
        }
    }

    if(report_smoke) {
        lv_mem_monitor_t mon;
        double used_frac;

        lv_mem_monitor(&mon);
        used_frac = mon.total_size ? (double)mon.max_used / (double)mon.total_size : 0.0;

        if(used_frac > 0.90) {
            fprintf(stderr,
                    "WALK FAIL: mem peak %lu/%lu (%.0f%%) exceeds 90%% of LV_MEM_SIZE\n",
                    (unsigned long)mon.max_used, (unsigned long)mon.total_size,
                    used_frac * 100.0);
            return false;
        }

        printf("SMOKE OK scenarios=%d bindings=%d screens=%d modals=%d peak=%lu/%lu\n",
               scenario_count, bindings_visited, screens_visited, modals_visited,
               (unsigned long)mon.max_used, (unsigned long)mon.total_size);
        fflush(stdout);
    }

    return true;
}

/* ----------------------------------------------------------- build bench */

/* --bench-builds N: how many builds of every screen to average, 0 = off.
 * The bench is a measuring mode like --smoke: it drives the manual clock, so
 * the model never ticks on under it and every build sees the same state. */
static int s_bench_builds;

/* Microseconds since an arbitrary origin. SDL's performance counter, not
 * SDL_GetTicks(): a build is tens of milliseconds, and whole-millisecond
 * ticks could not separate two screens that differ by a millisecond. */
static uint64_t bench_now_us(void)
{
    uint64_t c = SDL_GetPerformanceCounter();
    uint64_t f = SDL_GetPerformanceFrequency();

    return c * 1000000ULL / f;
}

/* Build every screen N times on the model state app_init() left - the same
 * start the smoke walk's first stop sees - and time the build and one forced
 * full redraw of it. The redraw is forced with lv_obj_invalidate() +
 * lv_refr_now(), because a screen whose area is already clean would otherwise
 * cost nothing and look free. The hidden debug screen is left out: it is not
 * reachable in the product. */
static bool run_bench(int n)
{
    uint64_t build_total[UI_SCREEN_COUNT];
    uint64_t draw_total[UI_SCREEN_COUNT];
    int id, i;

    for(id = 0; id < UI_SCREEN_COUNT; id++) {
        build_total[id] = 0;
        draw_total[id] = 0;
    }

    for(i = 0; i < n; i++) {
        for(id = 0; id < UI_SCREEN_COUNT; id++) {
            uint64_t t0, t1, t2;

            if(id == UI_SCREEN_DEBUG) continue;

            t0 = bench_now_us();
            if(!ui_nav_goto((ui_screen_id_t)id)) {
                fprintf(stderr, "BENCH FAIL: screen=%s failed to build\n",
                        ui_nav_screen_name((ui_screen_id_t)id));
                return false;
            }
            t1 = bench_now_us();
            lv_obj_invalidate(lv_screen_active());
            t2 = bench_now_us();
            lv_refr_now(NULL);
            build_total[id] += t1 - t0;
            draw_total[id] += bench_now_us() - t2;
        }
    }

    for(id = 0; id < UI_SCREEN_COUNT; id++) {
        if(id == UI_SCREEN_DEBUG) continue;
        printf("bench %s build_us=%llu draw_us=%llu\n", ui_nav_screen_name((ui_screen_id_t)id),
               (unsigned long long)(build_total[id] / (uint64_t)n),
               (unsigned long long)(draw_total[id] / (uint64_t)n));
    }
    fflush(stdout);
    return true;
}

/* mingw's <sys/stat.h> mkdir() takes only a path (it maps to the Win32 call,
 * which has no POSIX mode bits); the mode argument is silently accepted by
 * every other platform this simulator targets. */
static void sim_mkdir(const char *path)
{
#ifdef _WIN32
    mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

/* ------------------------------------------------------------ fixture replay */

/* Every fixture recorded so far is a handful of events; a ceiling this high
 * still catches a replay of the wrong file (a whole log) before it runs. */
#define REPLAY_MAX_EVENTS 512

static cJSON *s_replay_root;
static bool s_replay_applied[REPLAY_MAX_EVENTS];
static uint32_t s_replay_start_ms;
static double s_replay_speed = 1.0;
static const char *s_replay_unit;

/* Read the fixture into memory and keep its tree alive for the whole run
 * (events refer into it). False means the caller has nothing to replay. */
static bool replay_load(const char *path)
{
    FILE *f;
    long len;
    char *text;
    cJSON *events;

    f = fopen(path, "rb");
    if(!f) {
        fprintf(stderr, "replay: cannot open %s\n", path);
        return false;
    }
    if(fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "replay: cannot size %s\n", path);
        fclose(f);
        return false;
    }
    text = malloc((size_t)len + 1);
    if(!text) {
        fprintf(stderr, "replay: out of memory reading %s\n", path);
        fclose(f);
        return false;
    }
    if(fread(text, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "replay: short read on %s\n", path);
        free(text);
        fclose(f);
        return false;
    }
    text[len] = '\0';
    fclose(f);

    s_replay_root = cJSON_Parse(text);
    free(text);
    if(!s_replay_root) {
        fprintf(stderr, "replay: %s is not valid JSON\n", path);
        return false;
    }

    events = cJSON_GetObjectItemCaseSensitive(s_replay_root, "events");
    if(!cJSON_IsArray(events)) {
        fprintf(stderr, "replay: %s has no events array\n", path);
        cJSON_Delete(s_replay_root);
        s_replay_root = NULL;
        return false;
    }
    if(cJSON_GetArraySize(events) > REPLAY_MAX_EVENTS) {
        fprintf(stderr, "replay: %s has more than %d events\n", path, REPLAY_MAX_EVENTS);
        cJSON_Delete(s_replay_root);
        s_replay_root = NULL;
        return false;
    }

    memset(s_replay_applied, 0, sizeof s_replay_applied);
    return true;
}

/* Apply every event whose t has elapsed. all=true applies the whole file at
 * once (--dump-model) and never reads the clock, so it is safe before
 * lv_init(). Returns true when at least one event was applied. */
static bool replay_apply(bool all)
{
    const cJSON *events = cJSON_GetObjectItemCaseSensitive(s_replay_root, "events");
    const cJSON *ev;
    double elapsed_s = 0.0;
    bool applied = false;
    int i = 0;

    if(!all) {
        elapsed_s = (double)(lv_tick_get() - s_replay_start_ms) / 1000.0 * s_replay_speed;
    }

    cJSON_ArrayForEach(ev, events) {
        const cJSON *t = cJSON_GetObjectItemCaseSensitive(ev, "t");
        double at = cJSON_IsNumber(t) ? t->valuedouble : 0.0;

        if(i < REPLAY_MAX_EVENTS && !s_replay_applied[i] && (all || at <= elapsed_s)) {
            status_map_apply_fixture_event(&g_mmu, ev);
            mmu_model_merge_live(&g_mmu, &g_mmu);
            s_replay_applied[i] = true;
            applied = true;
        }
        i++;
    }

    return applied;
}

static void replay_timer_cb(lv_timer_t *t)
{
    (void)t;

    if(replay_apply(false)) ui_nav_refresh_async();

    /* --unit names a unit the events have to create first, so the binding
     * waits here until the name resolves. */
    if(s_replay_unit && mmu_set_this_unit_by_name(s_replay_unit)) s_replay_unit = NULL;
}

/* ------------------------------------------------------------ model dump */

static const char *onoff(bool v)
{
    return v ? "1" : "0";
}

static const char *slot_state_name(mmu_slot_state_t s)
{
    switch(s) {
    case MMU_SLOT_EMPTY: return "empty";
    case MMU_SLOT_READY: return "ready";
    case MMU_SLOT_LOADED: return "loaded";
    case MMU_SLOT_ERROR: return "error";
    }
    return "?";
}

static const char *info_source_name(mmu_info_source_t s)
{
    switch(s) {
    case MMU_INFO_UNKNOWN: return "unknown";
    case MMU_INFO_TAG: return "tag";
    case MMU_INFO_MANUAL: return "manual";
    }
    return "?";
}

static const char *cal_state_name(mmu_cal_state_t c)
{
    switch(c) {
    case MMU_CAL_UNKNOWN: return "unknown";
    case MMU_CAL_CONFIGURED: return "configured";
    case MMU_CAL_CALIBRATING: return "calibrating";
    case MMU_CAL_UNCALIBRATED: return "uncalibrated";
    }
    return "?";
}

static const char *dry_state_name(mmu_dry_state_t d)
{
    switch(d) {
    case MMU_DRY_IDLE: return "idle";
    case MMU_DRY_HEATING: return "heating";
    case MMU_DRY_DEHUMIDIFYING: return "dehumidifying";
    case MMU_DRY_COOLING: return "cooling";
    case MMU_DRY_ERROR: return "error";
    }
    return "?";
}

static const char *busy_name(mmu_busy_t b)
{
    switch(b) {
    case MMU_BUSY_NONE: return "none";
    case MMU_BUSY_LOAD: return "load";
    case MMU_BUSY_UNLOAD: return "unload";
    }
    return "?";
}

static const char *severity_name(mmu_severity_t s)
{
    switch(s) {
    case MMU_SEV_INFO: return "info";
    case MMU_SEV_PAUSE: return "pause";
    case MMU_SEV_STOP: return "stop";
    }
    return "?";
}

static const char *kind_name(mmu_unit_kind_t k)
{
    switch(k) {
    case MMU_UNIT_4SLOT: return "4slot";
    case MMU_UNIT_HT: return "ht";
    case MMU_UNIT_LANE: return "lane";
    }
    return "?";
}

/* One stable, greppable line per fact: the point is a diffable snapshot of
 * the mapped model, not a pretty report. */
static void dump_model(void)
{
    int u, s, i, m;

    printf("MODEL units=%u groups=%u busy=%s busy_unit=%d busy_slot=%d "
           "loaded=%s loaded_unit=%d loaded_slot=%d loaded_ext=%d current_group=%d "
           "motion_settled=%s spoolman_online=%s has_error=%s\n",
           (unsigned)g_mmu.unit_count, (unsigned)g_mmu.group_count,
           busy_name(g_mmu.busy), (int)g_mmu.busy_unit, (int)g_mmu.busy_slot,
           onoff(g_mmu.loaded), (int)g_mmu.loaded_unit, (int)g_mmu.loaded_slot,
           (int)g_mmu.loaded_ext, (int)g_mmu.current_group,
           onoff(g_mmu.motion_settled), onoff(g_mmu.spoolman_online),
           onoff(g_mmu.has_error));

    printf("SETTINGS known=%s rti=%s rts=%s aro=%s acs=%s pa=%s\n",
           onoff(g_mmu.settings.known), onoff(g_mmu.settings.read_tag_on_insertion),
           onoff(g_mmu.settings.read_tags_at_startup),
           onoff(g_mmu.settings.auto_reload_on_runout),
           onoff(g_mmu.settings.auto_create_spoolman_spools),
           onoff(g_mmu.settings.apply_pa_on_load));

    for(u = 0; u < (int)g_mmu.unit_count; u++) {
        const mmu_unit_t *unit = &g_mmu.units[u];

        printf("UNIT %d name=%s kind=%s variant=%s slots=%u side=%d connected=%s "
               "can_dry=%s dry=%s target=%d remaining_min=%d adapter=%s "
               "rh=%d rh_valid=%s temp=%d temp_valid=%s rfid=%s\n",
               u, unit->name, kind_name(unit->kind), mmu_variant_name(unit->variant),
               (unsigned)unit->slot_count, (int)unit->side, onoff(unit->connected),
               onoff(unit->can_dry), dry_state_name(unit->dry_state),
               (int)unit->dry_target_c, (int)unit->dry_remaining_min,
               onoff(unit->power_adapter), (int)unit->humidity_pct,
               onoff(unit->has_humidity_pct), (int)unit->temp_c, onoff(unit->has_temp),
               onoff(unit->rfid));

        for(s = 0; s < (int)unit->slot_count; s++) {
            const mmu_slot_t *slot = &unit->slots[s];

            printf("SLOT %d:%d state=%s cal=%s positioning=%s spool=%d source=%s "
                   "material=%s brand=%s color=%06X remaining_g=%d remaining_pct=%d pending=%s\n",
                   u, s, slot_state_name(slot->state), cal_state_name(slot->cal_state),
                   onoff(slot->positioning), (int)slot->spool_id,
                   info_source_name(slot->info_source), slot->material, slot->brand,
                   (unsigned)(slot->color & 0xFFFFFFu), (int)slot->remaining_g,
                   (int)slot->remaining_pct, onoff(slot->pending_confirmation));
        }
    }

    printf("STEPS count=%u current=%d failed=%d\n",
           (unsigned)g_mmu.step_count, (int)g_mmu.step_current, (int)g_mmu.step_failed);
    for(i = 0; i < (int)g_mmu.step_count; i++) {
        printf("STEP %d name=%s\n", i, g_mmu.steps[i]);
    }

    if(g_mmu.has_error) {
        printf("ERROR severity=%s code=%s unit=%d slot=%d text=%s\n",
               severity_name(g_mmu.error_severity), g_mmu.error_code,
               (int)g_mmu.error_unit, (int)g_mmu.error_slot, g_mmu.error_text);
    }
    else {
        printf("ERROR none\n");
    }

    /* The host's answer to the last load or unload the display sent: what a
     * live test checks after --action (the load screen draws the same). */
    printf("ACTION refusal_kind=%d pending_kind=%d refusal=%s\n",
           (int)g_mmu.action_refusal_kind, (int)g_mmu.action_pending_kind,
           g_mmu.action_refusal[0] ? g_mmu.action_refusal : "-");

    printf("RUNOUT active=%s from=%d:%d to=%d:%d\n", onoff(g_mmu.runout_active),
           (int)g_mmu.runout_from_unit, (int)g_mmu.runout_from_slot,
           (int)g_mmu.runout_to_unit, (int)g_mmu.runout_to_slot);

    {
        int j;
        char need_str[64] = "";
        char short_str[64] = "";
        int npos = 0, spos = 0;

        for(j = 0; j < MMU_MAX_TOOLS; j++) {
            npos += snprintf(need_str + npos, (int)sizeof need_str - npos,
                             "%s%d", j ? "," : "", (int)g_mmu.job_need_g[j]);
        }
        for(j = 0; j < MMU_MAX_TOOLS; j++) {
            if(g_mmu.job_short & (1u << j)) {
                spos += snprintf(short_str + spos, (int)sizeof short_str - spos,
                                 "%s%d", spos ? "," : "", j);
            }
        }
        printf("JOB need=%s short=%s\n", need_str, spos ? short_str : "-");
    }

    for(i = 0; i < (int)g_mmu.group_count; i++) {
        const mmu_group_t *grp = &g_mmu.groups[i];

        printf("GROUP %d name=%s count=%u members=", i, grp->name, (unsigned)grp->count);
        if(grp->count == 0) {
            printf("-\n");
            continue;
        }
        for(m = 0; m < (int)grp->count; m++) {
            printf("%s%d:%d", m ? "," : "", (int)grp->members[m].unit,
                   (int)grp->members[m].slot);
        }
        printf("\n");
    }

    fflush(stdout);
}

/* --dump-view: the view builders' own JSON (view_json_build()), pretty-printed
 * with cJSON's stable (insertion) key order, for tests/replay_view_goldens.sh.
 * view_json() itself stays compact (docs/design/UNIFIED_UI.md 4, "for the
 * web"); this just prints the same tree the other way. */
static void dump_view(void)
{
    cJSON *root = view_json_build(&g_mmu);
    char *text = cJSON_Print(root);

    if(text) printf("%s\n", text);
    free(text);
    cJSON_Delete(root);
    fflush(stdout);
}

/* --dump-view's own replay of a fixture: unlike --dump-model
 * (status_map_apply_fixture_event, which only understands the "openams_ui"
 * and "component_status" kinds a VIEW.recorded fixture also carries), this
 * feeds every event through core_apply() so "c2_status" - recorded's manager
 * status, C2 6.5's lanes_by_fps (pressure, set_point) among its keys - reaches
 * the model too, the same way a live oams_manager notify would. */
static void replay_apply_via_core(core_t *core)
{
    const cJSON *events = cJSON_GetObjectItemCaseSensitive(s_replay_root, "events");
    const cJSON *ev;

    cJSON_ArrayForEach(ev, events) {
        const cJSON *kind = cJSON_GetObjectItemCaseSensitive(ev, "kind");
        const cJSON *unit = cJSON_GetObjectItemCaseSensitive(ev, "unit");
        const cJSON *payload = cJSON_GetObjectItemCaseSensitive(ev, "payload");
        const char *name;

        if(!cJSON_IsString(kind) || !payload) continue;
        name = cJSON_IsString(unit) ? unit->valuestring : NULL;

        if(strcmp(kind->valuestring, "c2_status") == 0)
            core_apply(core, "oams_manager", NULL, payload);
        else
            core_apply(core, kind->valuestring, name, payload);
    }
}

/* The merge-owning path onto g_mmu (core.h): on the simulator m == &g_mmu, so
 * this is the same "self-merge" the fixture replay path already used. */
static core_t *s_core;

/* -------------------------------------------------------- live Moonraker link */

static const char *s_bind_unit;
static double s_dump_after_s = -1.0;
static double s_dump_view_after_s = -1.0;
static bool s_mc_socket_up;    /* ws handshake up and mc_on_connected() called */
static bool s_mc_first_try;    /* no connect attempt yet: skip the backoff wait */
static const char *s_live_unit; /* --unit, resolved once the model has it */

static const char *link_name(mc_link_t state)
{
    switch(state) {
    case MC_LINK_DOWN: return "down";
    case MC_LINK_CONNECTING: return "connecting";
    case MC_LINK_KLIPPY_WAIT: return "klippy-wait";
    case MC_LINK_READY: return "ready";
    }
    return "?";
}

/* With no link the units cannot be trusted to be reachable any more. */
static void moonraker_link_cb(void *ctx, mc_link_t state)
{
    int i;

    (void)ctx;
    printf("MOONRAKER link=%s\n", link_name(state));
    fflush(stdout);
    if(state == MC_LINK_DOWN) {
        for(i = 0; i < (int)g_mmu.unit_count; i++) g_mmu.units[i].connected = false;
    }
    else if(state == MC_LINK_READY) {
        /* A fresh subscribe result is about to be applied (moonraker_client
         * reaches READY before dispatching it): drop whatever the core owns
         * first, so a reconnect after a Klipper restart cannot leave stale
         * copies or stale settings/lane state behind. Harmless (a no-op) on
         * the very first connect, when there is nothing to drop yet. */
        core_reset(s_core, true);
    }
}

static bool moonraker_send_cb(void *ctx, const char *text, size_t len)
{
    (void)ctx;
    return ws_send_text(text, len);
}

static void moonraker_object_cb(void *ctx, const char *kind, const char *name, const cJSON *obj)
{
    (void)ctx;
    core_apply(s_core, kind, name, obj);
    /* On the simulator m == &g_mmu, so the copy step is skipped; only the
     * pending drain runs, moving any new fault alerts into the history. */
    mmu_model_merge_live(&g_mmu, &g_mmu);
    app_live_model_changed();
}

static void moonraker_component_cb(void *ctx, const cJSON *status)
{
    (void)ctx;
    core_apply(s_core, "component_status", NULL, status);
    mmu_model_merge_live(&g_mmu, &g_mmu);
    app_live_model_changed();
}

/* The host's answer to a G-code line the display sent. A refusal stops here
 * otherwise: it reaches the model through core_apply("action_result"), so the
 * load screen says the host's words and any other refusal is said once as a
 * toast (see app_live_poll). */
static void moonraker_gcode_cb(void *ctx, int request_id, bool ok, const char *message)
{
    cJSON *obj;
    mmu_action_kind_t kind;

    (void)ctx;
    printf("MOONRAKER gcode id=%d ok=%d %s\n", request_id, ok ? 1 : 0,
           message ? message : "");
    fflush(stdout);

    kind = sim_sent_kind_of(request_id);
    if(kind == MMU_ACTION_OTHER && ok) return;   /* nothing to tell the display */

    obj = cJSON_CreateObject();
    if(!obj) return;
    cJSON_AddNumberToObject(obj, "kind", (double)kind);
    cJSON_AddBoolToObject(obj, "ok", ok);
    cJSON_AddStringToObject(obj, "message", message ? message : "");
    if(core_apply(s_core, "action_result", NULL, obj)) {
        mmu_model_merge_live(&g_mmu, &g_mmu);
        app_live_model_changed();
    }
    cJSON_Delete(obj);
}

/* The answer to an RPC the display sent (see core_rpc_result()) */
static void moonraker_rpc_cb(void *ctx, int tag, const cJSON *result, const char *error)
{
    (void)ctx;
    if(error) {
        printf("MOONRAKER rpc tag=%d error=%s\n", tag, error);
        fflush(stdout);
    }
    if(core_rpc_result(s_core, tag, result, error)) {
        mmu_model_merge_live(&g_mmu, &g_mmu);
        app_live_model_changed();
    }
}

static void moonraker_text_cb(void *ctx, const char *text, size_t len)
{
    static uint32_t last_reject_ms;
    uint32_t now = ws_now_ms();

    (void)ctx;
    if(!mc_on_text(text, len, now) && (last_reject_ms == 0 || now - last_reject_ms >= 5000)) {
        last_reject_ms = now ? now : 1;
        printf("MOONRAKER rejected a %zu-byte message\n", len);
        fflush(stdout);
    }
}

static void moonraker_init(void)
{
    mc_callbacks_t cb;

    if(!s_core) s_core = core_create(&g_mmu);

    memset(&cb, 0, sizeof cb);
    cb.send_text = moonraker_send_cb;
    cb.on_object = moonraker_object_cb;
    cb.on_component_status = moonraker_component_cb;
    cb.on_link = moonraker_link_cb;
    cb.on_gcode_result = moonraker_gcode_cb;
    cb.on_rpc_result = moonraker_rpc_cb;
    mc_init(&cb, s_bind_unit);

    s_mc_socket_up = false;
    s_mc_first_try = true;
}

/* One turn of the link: open a socket when the backoff allows, pump the
 * transport into the client, and let the client's timers run.
 *
 * The client only arms its reconnect clock once it is *told* the socket went
 * away (`mc_on_disconnected`), so the very first attempt cannot wait for
 * `mc_reconnect_due()`: it would never fire. `s_mc_first_try` covers that one
 * case; every later attempt goes through the backoff like a real reconnect. */
static void moonraker_step(void)
{
    uint32_t now = ws_now_ms();

    if(ws_is_open()) {
        ws_poll(moonraker_text_cb, NULL);
        if(!ws_is_open()) {
            s_mc_socket_up = false;
            mc_on_disconnected(now);
        }
    }
    else if(s_mc_socket_up) {
        s_mc_socket_up = false;
        mc_on_disconnected(now);
    }
    else if(s_mc_first_try || mc_reconnect_due(now)) {
        s_mc_first_try = false;
        if(ws_connect(s_moonraker_url)) {
            s_mc_socket_up = true;
            mc_on_connected(now);
        }
        else {
            mc_on_disconnected(now); /* re-arm the backoff on a refused connect */
        }
    }
    mc_poll(now);

    /* --unit names a unit the live model has to create first, so the
     * display's binding waits here until the name resolves. */
    if(s_live_unit && mmu_set_this_unit_by_name(s_live_unit)) s_live_unit = NULL;
}

/* --action: hand each stored line to platform_action() once -- and only once
 * -- the link can carry it. Everything before READY would be refused. */
static void send_actions(void)
{
    int i;

    if(s_actions_sent || mc_link() != MC_LINK_READY) return;
    s_actions_sent = true;
    for(i = 0; i < s_action_count; i++) platform_action(s_actions[i]);
}

/* --dump-model-after / --dump-view-after: no window, no LVGL. Drive the
 * socket by hand for @p seconds and print the same dump --dump-model (or
 * --dump-view, when @p view_mode) does. */
static int moonraker_headless(double seconds, bool view_mode)
{
    uint32_t start = ws_now_ms();

    status_map_reset(&g_mmu);
    s_live_unit = NULL;
    moonraker_init();

    while((double)(ws_now_ms() - start) < seconds * 1000.0) {
        moonraker_step();
        /* The injected lines still need the loop to keep pumping after them,
         * so their requests leave before the dump. */
        send_actions();
        SDL_Delay(2);
    }

    if(view_mode) dump_view();
    else          dump_model();
    mc_deinit();
    ws_close();
    return 0;
}

/* -------------------------------------------------------------------- CLI */

int main(int argc, char **argv)
{
    const char *scenario = NULL;
    const char *unit_name = NULL;
    const char *lang = NULL;
    const char *screenshots_dir = NULL;
    const char *gallery_dir = NULL;
    const char *replay_file = NULL;
    bool smoke = false;
    bool dump = false;
    bool dump_view_flag = false;
    bool zoom_given = false;
#ifdef _WIN32
    /* A tester's screen is likely 1080p; the Linux default (1920x1280) can
     * run off it, so default smaller here. --zoom still overrides. */
    float zoom = 2.0f;
#else
    float zoom = 4.0f; /* 1920x1280 on the owner's 4K screen */
#endif
    int i;
    lv_display_t *disp;

#ifdef _WIN32
    SDL_SetMainReady();
    open_action_log();
#endif

    for(i = 1; i < argc; i++) {
        if(strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            scenario = argv[++i];
        }
        else if(strcmp(argv[i], "--unit") == 0 && i + 1 < argc) {
            unit_name = argv[++i];
        }
        else if(strcmp(argv[i], "--lang") == 0 && i + 1 < argc) {
            lang = argv[++i];
        }
        else if(strcmp(argv[i], "--smoke") == 0) {
            smoke = true;
        }
        else if(strcmp(argv[i], "--audit") == 0) {
            s_audit = true;
            smoke = true;
        }
        else if(strcmp(argv[i], "--zoom") == 0 && i + 1 < argc) {
            zoom = (float)atof(argv[++i]);
            if(zoom < 1.0f) zoom = 1.0f;
            zoom_given = true;
        }
        else if(strcmp(argv[i], "--screenshots") == 0 && i + 1 < argc) {
            screenshots_dir = argv[++i];
        }
        else if(strcmp(argv[i], "--gallery") == 0 && i + 1 < argc) {
            gallery_dir = argv[++i];
        }
        else if(strcmp(argv[i], "--bench-builds") == 0 && i + 1 < argc) {
            s_bench_builds = atoi(argv[++i]);
            if(s_bench_builds < 1) {
                fprintf(stderr, "--bench-builds must be at least 1\n");
                return 1;
            }
        }
        else if(strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {
            replay_file = argv[++i];
        }
        else if(strcmp(argv[i], "--replay-speed") == 0 && i + 1 < argc) {
            s_replay_speed = atof(argv[++i]);
            if(s_replay_speed <= 0.0) {
                fprintf(stderr, "--replay-speed must be greater than 0\n");
                return 1;
            }
        }
        else if(strcmp(argv[i], "--dump-model") == 0) {
            dump = true;
        }
        else if(strcmp(argv[i], "--dump-view") == 0) {
            dump_view_flag = true;
        }
        else if(strcmp(argv[i], "--moonraker") == 0 && i + 1 < argc) {
            s_moonraker_url = argv[++i];
        }
        else if(strcmp(argv[i], "--bind-unit") == 0 && i + 1 < argc) {
            s_bind_unit = argv[++i];
        }
        else if(strcmp(argv[i], "--dump-model-after") == 0 && i + 1 < argc) {
            s_dump_after_s = atof(argv[++i]);
            if(s_dump_after_s < 0.0) {
                fprintf(stderr, "--dump-model-after must not be negative\n");
                return 1;
            }
        }
        else if(strcmp(argv[i], "--dump-view-after") == 0 && i + 1 < argc) {
            s_dump_view_after_s = atof(argv[++i]);
            if(s_dump_view_after_s < 0.0) {
                fprintf(stderr, "--dump-view-after must not be negative\n");
                return 1;
            }
        }
        else if(strcmp(argv[i], "--action") == 0 && i + 1 < argc) {
            if(s_action_count >= SIM_MAX_ACTIONS) {
                fprintf(stderr, "--action takes at most %d lines\n", SIM_MAX_ACTIONS);
                return 1;
            }
            s_actions[s_action_count++] = argv[++i];
        }
        else {
            fprintf(stderr, "usage: %s [--scenario NAME] [--unit NAME] [--lang CODE] [--zoom N] [--smoke] [--audit] [--screenshots DIR] [--gallery DIR] [--bench-builds N] [--replay FILE] [--replay-speed X] [--dump-model] [--dump-view] [--moonraker URL] [--bind-unit NAME] [--dump-model-after SECONDS] [--dump-view-after SECONDS] [--action LINE]\n",
                    argv[0]);
            return 1;
        }
    }

    /* The bench measures the UI, not the desktop window: at the default zoom
     * the SDL window is 4x the panel, and presenting that scaled surface costs
     * more than drawing every screen here put together, which would hide the
     * differences the bench exists to show. An explicit --zoom still wins. */
    if(s_bench_builds && !zoom_given) zoom = 1.0f;

    /* The language is picked before anything is drawn, so every screen, the
     * view JSON and the audit all see the same tables. A code this image does
     * not carry falls back to English, the way a device falls back to the one
     * it has (src/app/app.c), and says so: --lang is a debugging flag on a
     * build someone chose (-DMMU_LANGUAGES), and the message says the code was
     * left out rather than misspelled. */
    if(lang && !ui_set_language(lang)) {
        static const char *const known[] = { VS_KNOWN_LANG_CODES };
        bool exists = false;

        for(i = 0; i < (int)(sizeof(known) / sizeof(known[0])); i++)
            if(strcmp(known[i], lang) == 0) exists = true;
        /* A code no build has is a typo, and --smoke --audit is a check: refuse
         * it rather than report a pass for a language nobody looked at. */
        if(!exists) {
            fprintf(stderr, "unknown language: %s\n", lang);
            return 1;
        }
        fprintf(stderr, "language \"%s\" is not in this build; using English (this build carries",
                lang);
        for(i = 0; i < view_lang_count(); i++) fprintf(stderr, " %s", view_lang_code(i));
        fprintf(stderr, ")\n");
    }

    if(dump && !replay_file) {
        fprintf(stderr, "usage: %s --replay FILE --dump-model\n", argv[0]);
        return 1;
    }

    if(dump_view_flag && !replay_file) {
        fprintf(stderr, "usage: %s --replay FILE --dump-view\n", argv[0]);
        return 1;
    }

    if(dump && dump_view_flag) {
        fprintf(stderr, "--dump-model and --dump-view are two different dumps; pick one\n");
        return 1;
    }

    if(s_moonraker_url && replay_file) {
        fprintf(stderr, "--moonraker and --replay are two different model sources; pick one\n");
        return 1;
    }

    if(s_dump_after_s >= 0.0 && !s_moonraker_url) {
        fprintf(stderr, "usage: %s --moonraker URL --dump-model-after SECONDS\n", argv[0]);
        return 1;
    }

    if(s_dump_view_after_s >= 0.0 && !s_moonraker_url) {
        fprintf(stderr, "usage: %s --moonraker URL --dump-view-after SECONDS\n", argv[0]);
        return 1;
    }

    if(s_dump_after_s >= 0.0 && s_dump_view_after_s >= 0.0) {
        fprintf(stderr,
                "--dump-model-after and --dump-view-after are two different dumps; pick one\n");
        return 1;
    }

    /* --action injects into the headless pump, so anywhere else it would be
     * parsed and then silently never run. */
    if(s_action_count && ((s_dump_after_s < 0.0 && s_dump_view_after_s < 0.0) || !s_moonraker_url)) {
        fprintf(stderr,
                "usage: %s --moonraker URL --dump-model-after SECONDS --action LINE\n", argv[0]);
        return 1;
    }

    /* --dump-model is a batch print, not a UI: apply the whole fixture and
     * report before lv_init() ever creates a window. */
    if(dump) {
        if(!replay_load(replay_file)) return 1;
        status_map_reset(&g_mmu);
        replay_apply(true);
        dump_model();
        cJSON_Delete(s_replay_root);
        return 0;
    }

    /* --dump-view: the same batch replay, through core_apply() instead of
     * status_map_apply_fixture_event() (see replay_apply_via_core()), then
     * the view builders' own JSON. */
    if(dump_view_flag) {
        core_t *core;

        if(!replay_load(replay_file)) return 1;
        status_map_reset(&g_mmu);
        core = core_create(&g_mmu);
        replay_apply_via_core(core);
        dump_view();
        core_destroy(core);
        cJSON_Delete(s_replay_root);
        return 0;
    }

    /* --dump-model-after / --dump-view-after are the same batch print
     * against a live link: no window, no LVGL, just the socket and the
     * clock. */
    if(s_dump_after_s >= 0.0) {
        return moonraker_headless(s_dump_after_s, false);
    }
    if(s_dump_view_after_s >= 0.0) {
        return moonraker_headless(s_dump_view_after_s, true);
    }

    lv_init();

    /* lv_sdl_window_create() calls lv_tick_set_cb(SDL_GetTicks) itself, the
     * first time it runs, so setting the manual clock's callback before it
     * would only be overwritten: set it after the window exists instead. */
    disp = lv_sdl_window_create(UI_SCREEN_W, UI_SCREEN_H);
    lv_tick_set_cb((smoke || screenshots_dir || gallery_dir || s_bench_builds) ? manual_tick_cb : SDL_GetTicks);
    lv_sdl_window_set_zoom(disp, zoom);
#ifdef _WIN32
    lv_sdl_window_set_title(disp, "MMU display simulator");
#endif
    lv_sdl_mouse_create();

    if(replay_file) {
        /* app_init() is deliberately not used here: it loads a dummy scenario
         * and installs the 500ms model tick that would finish the replayed
         * load and run the replayed dryer down, which is exactly the live
         * state --replay exists to freeze. Do the two things the UI needs
         * (theme, nav) and drive the fixture from our own timer instead. */
        if(!replay_load(replay_file)) return 1;
        s_replay_unit = unit_name;
        status_map_reset(&g_mmu);

        ui_theme_init();
        ui_nav_init();

        s_replay_start_ms = lv_tick_get();
        replay_apply(false); /* every t == 0 event is the initial state */
        if(s_replay_unit && mmu_set_this_unit_by_name(s_replay_unit)) s_replay_unit = NULL;

        ui_nav_goto(UI_SCREEN_HOME);
        lv_timer_create(replay_timer_cb, 250, NULL);
    }
    else if(s_moonraker_url) {
        /* app_init() would load a dummy scenario and install the model tick,
         * and the live link is the one source of truth for this model:
         * app_live_init() is the same start-up without either. */
        s_live_unit = unit_name;
        app_live_init();

        moonraker_init();
    }
    else {
        app_init(scenario, unit_name);
    }

    if(gallery_dir) {
        int issues;
        sim_mkdir(gallery_dir);
        issues = gallery_run(gallery_dir);
        if(issues == 0) {
            printf("GALLERY OK issues=%d\n", issues);
            return 0;
        }
        printf("GALLERY FAIL issues=%d\n", issues);
        return 1;
    }

    if(screenshots_dir) {
        bool ok;
        sim_mkdir(screenshots_dir);
        ok = run_walk(screenshots_dir, false);
        return ok ? 0 : 1;
    }

    if(smoke) {
        bool ok = run_walk(NULL, true);
        if(s_audit) {
            printf("AUDIT TOTAL issues=%d\n", s_audit_issues);
            return (ok && s_audit_issues == 0) ? 0 : 1;
        }
        return ok ? 0 : 1;
    }

    if(s_bench_builds) {
        return run_bench(s_bench_builds) ? 0 : 1;
    }

    /* interactive: real time, real mouse. LVGL's SDL driver already polls
     * the event queue (mouse motion/clicks, window close) inside
     * lv_timer_handler(); polling it again here would steal those events
     * before the driver sees them, so buttons would stop responding. */
    for(;;) {
        lv_timer_handler();
        if(s_moonraker_url) {
            moonraker_step();
            app_live_poll();
        }
        SDL_Delay(5);
    }
}
