/**
 * @file display_cfg.c
 * Simulator implementation of the display settings platform API
 * (src/app/platform.h): no real backlight/gamma/orientation to drive, so
 * this is just a static struct with the documented defaults, logging each
 * change the way every other platform_action() does.
 */
#include "app/platform.h"
#include "app/actions.h"

#include <stdio.h>
#include <string.h>

/* The one setting with nothing to drive: the strings and the font slots are
 * chosen by the app, so there is no hardware to apply it to. The simulator
 * keeps it for the session - it also serves the browser build, which has no
 * storage at all - and only the ESP32 persists it. */
static char s_lang[16];

static platform_display_cfg_t s_cfg = {
    .brightness_pct = 100,
    .contrast = 0,
    .dim_after_s = 300,
    .off_after_s = 900,
    .flip = false,
};

void platform_display_get(platform_display_cfg_t *out)
{
    *out = s_cfg;
}

void platform_display_set(const platform_display_cfg_t *cfg)
{
    if(cfg->brightness_pct != s_cfg.brightness_pct) {
        actions_log("display brightness %u", (unsigned)cfg->brightness_pct);
    }
    if(cfg->contrast != s_cfg.contrast) {
        actions_log("display contrast %s", cfg->contrast ? "high" : "standard");
    }
    if(cfg->dim_after_s != s_cfg.dim_after_s) {
        actions_log("display dim-after %u", (unsigned)cfg->dim_after_s);
    }
    if(cfg->off_after_s != s_cfg.off_after_s) {
        actions_log("display off-after %u", (unsigned)cfg->off_after_s);
    }
    if(cfg->flip != s_cfg.flip) {
        actions_log("display flip %s", cfg->flip ? "on" : "off");
    }

    s_cfg = *cfg;
}

void platform_language_get(char *out, size_t n)
{
    if(!out || n == 0) return;
    snprintf(out, n, "%s", s_lang);
}

void platform_language_set(const char *code)
{
    if(!code) return;
    snprintf(s_lang, sizeof s_lang, "%s", code);
    actions_log("language %s", s_lang);
}
