/**
 * @file app_live.h
 * The live counterpart of app.h: the same theme/nav start-up, but the model
 * comes from a live Moonraker link (net_link.h) instead of a dummy scenario,
 * so there is no scenario load and no model timer. Screens rebuild when the
 * link reports a change, at a rate a person can read.
 */
#ifndef APP_LIVE_H
#define APP_LIVE_H

/** Empty live model, theme init, nav init, go home. */
void app_live_init(void);

/** The live model changed: a rebuild of the current screen is wanted. */
void app_live_model_changed(void);

/** Call every LVGL loop iteration: rebuild the current screen once a rebuild
 * is both wanted and safe to run. */
void app_live_poll(void);

#endif /* APP_LIVE_H */
