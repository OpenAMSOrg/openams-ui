/**
 * @file app.h
 * Start-up and scenario switching, shared by every platform. Owns the one
 * lv_timer that drives the dummy model's animated state.
 */
#ifndef APP_H
#define APP_H

/** Model init, theme init, nav init, start the model timer, go home.
 * @p scenario may be NULL (falls back to "idle").
 * @p unit_name may be NULL (leaves the default binding, unit 0); unknown
 * names also leave the binding unchanged. */
void app_init(const char *scenario, const char *unit_name);

/** Bind the display to @p unit_idx, log the choice and return home. */
void app_set_this_unit(int unit_idx);

/** Switch scenario at runtime (unknown names fall back to "idle") and return
 * to the home screen so the new state is visible immediately. */
void app_set_scenario(const char *name);

/** Scenario names accepted by app_set_scenario() / --scenario. */
int app_scenario_count(void);
const char *app_scenario_name(int index);

#endif /* APP_H */
