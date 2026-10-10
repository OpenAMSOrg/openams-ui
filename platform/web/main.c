/**
 * @file main.c
 * Emscripten/WebAssembly platform: same LVGL SDL driver as platform/sim
 * (Emscripten provides SDL2 via -sUSE_SDL=2), but driven by the browser's
 * frame loop instead of a blocking `for(;;)`, and with two functions exposed
 * to the page's pickers (web_set_scenario / web_set_unit) so they can steer
 * the running instance without a reload.
 *
 * Not part of the CMake `openams-ui-sim` target: built by its own
 * Emscripten CMake path (see the top-level CMakeLists.txt's
 * EMSCRIPTEN branch) into build-web/site/.
 */
#include "lvgl.h"
#include "SDL2/SDL.h"

#include <emscripten.h>
#include <emscripten/html5.h>

#include "app/actions.h"
#include "app/app.h"
#include "app/platform.h"
#include "model/mmu_model.h"
#include "ui/ui_nav.h"
#include "ui/ui_theme.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* The browser build shows the demo model; it has no live link to wait on. */
platform_link_t platform_link_status(char *detail, size_t n)
{
    if(detail && n) detail[0] = '\0';
    return PLATFORM_LINK_READY;
}

/* No physical touchscreen to calibrate here: the browser has none either. */
void platform_touch_calibrate(void)
{
    printf("touch calibration (web: no-op)\n");
    fflush(stdout);
}

/* Every action line reaches the console AND the page's "What you tapped"
 * log, via a JS hook the page defines (index.html). A page that has not
 * defined it yet (still loading) just gets the console line. */
void platform_action(const char *line)
{
    printf("ACTION: %s\n", line);
    fflush(stdout);

    EM_ASM(
        {
            if(typeof window.mmuActionLog === 'function') {
                window.mmuActionLog(UTF8ToString($0));
            }
        },
        line);
}

/* lv_sdl_window_create() sets lv_tick_set_cb(SDL_GetTicks) itself the first
 * time it runs; overriding it with the wall-clock browser timer has to
 * happen after that call, same ordering as platform/sim/main.c. */
static uint32_t web_tick_cb(void)
{
    return (uint32_t)emscripten_get_now();
}

static void loop_iter(void)
{
    lv_timer_handler();
}

/* ------------------------------------------------ exposed to the page JS */

/* The Unit and Scenario pickers call these directly (no reload). Kept to
 * exactly the app-level operations app_init() itself uses, so a picker
 * change and a fresh --scenario/--unit launch behave identically. */

EMSCRIPTEN_KEEPALIVE
void web_set_scenario(const char *name)
{
    app_set_scenario(name);
}

EMSCRIPTEN_KEEPALIVE
void web_set_unit(const char *name)
{
    mmu_set_this_unit_by_name(name);
    ui_nav_goto_async(UI_SCREEN_HOME);
}

/* --------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    const char *scenario = NULL;
    const char *unit_name = NULL;
    int i;
    lv_display_t *disp;

    /* index.html builds Module.arguments from the page's own URL query
     * parameters (?scenario=...&unit=...) before the wasm module loads, so
     * argv here is the same --scenario/--unit pair the simulator accepts. */
    for(i = 1; i < argc; i++) {
        if(strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            scenario = argv[++i];
        }
        else if(strcmp(argv[i], "--unit") == 0 && i + 1 < argc) {
            unit_name = argv[++i];
        }
    }

    lv_init();

    disp = lv_sdl_window_create(UI_SCREEN_W, UI_SCREEN_H);
    lv_tick_set_cb(web_tick_cb);
    lv_sdl_mouse_create();

    app_init(scenario, unit_name);

    /* fps=0 -> driven by requestAnimationFrame; simulate_infinite_loop=1 so
     * main() never returns and its locals (disp) stay live for the
     * lifetime of the page. */
    emscripten_set_main_loop(loop_iter, 0, 1);

    return 0;
}
