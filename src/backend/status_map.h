/**
 * @file status_map.h
 * Map OpenAMS status objects onto the display model.
 *
 * Two sources feed one model (C2 6.5):
 *   - the compact per-unit projection `openams_ui <unit>` (C2 6) carries the
 *     unit, its bays, the lane, the environment, the dryer, the active fault
 *     and the lane's groups;
 *   - the `openams_spoolman` component status (C4) carries the spool facts the
 *     projection deliberately dropped: material, color, vendor and remaining.
 *     They are merged into the same slot.
 *
 * Pure C: no LVGL, no platform headers. The simulator and (later) the firmware
 * run the same code.
 */

#ifndef STATUS_MAP_H
#define STATUS_MAP_H

#include "lib/cjson/cJSON.h"
#include "model/mmu_model.h"

#include <stdbool.h>

/**
 * Turn @p m into an empty live model: no units, no groups, no scenario data,
 * every "none" sentinel at its documented value. Unlike mmu_model_init() this
 * starts from nothing, so a replay does not inherit a dummy scenario.
 */
void status_map_reset(mmu_model_t *m);

/**
 * Apply one `openams_ui <unit>` projection object (C2 6). Finds the unit by
 * its `unit` name, creating it when this is the first time it is seen, and
 * maps every key the model can hold. Keys the model has no home for are
 * ignored, so a newer projection stays backwards compatible.
 *
 * Returns false when @p obj is not a usable projection (not an object, no unit
 * name, or no `bays` array).
 */
bool status_map_apply_openams_ui(mmu_model_t *m, const cJSON *obj);

/**
 * Apply one `openams_spoolman` status object (C4): `spoolman_online` plus the
 * per-`<unit>-<bay>` spool facts. Units named here are created on demand, so a
 * unit known only through its Spoolman links still shows up.
 *
 * Returns false when @p obj is not a status object (no `bays` object).
 */
bool status_map_apply_component_status(mmu_model_t *m, const cJSON *obj);

/**
 * Apply one `oams_manager.settings` object (C2 4.9): copy each of the five
 * booleans that is present, then mark the settings known. A partial object
 * therefore changes only the keys it carries.
 *
 * Returns false when @p obj is not an object.
 */
bool status_map_apply_settings(mmu_model_t *m, const cJSON *obj);

/**
 * Apply one `oams_manager.lanes_by_fps` object: for each lane id, set that
 * toolhead's `pressure` and `set_point` (creating the toolhead when it is not
 * known yet, C2 6.5), from the -1-if-absent convention every other toolhead
 * field uses. Nothing else from `lanes_by_fps` is read; the `openams_ui`
 * projection stays authoritative for everything else the toolhead holds
 * (pressure and set_point are deliberately left out of that projection).
 *
 * Returns false when @p lanes_by_fps is not an object.
 */
bool status_map_apply_lanes(mmu_model_t *m, const cJSON *lanes_by_fps);

/**
 * Apply one recorded fixture event, dispatching on its `kind`. The two kinds
 * above are mapped; every other kind (c2_status, action_log, spoolman_call,
 * gcode_script) is a recording artifact the display does not read and is
 * ignored. Returns the applier's result for a mapped kind, false for an event
 * that is not an object or has no `kind`.
 */
bool status_map_apply_fixture_event(mmu_model_t *m, const cJSON *event);

/**
 * Parse a Spoolman spool list (the `result` from `server.spoolman.proxy`
 * GET /v1/spool) into @p m->spools[]. Skips archived entries; keeps at most
 * MMU_MAX_SPOOLS unarchived ones, in the order the server sent them.
 *
 * Returns false when @p arr is not a JSON array.
 */
bool status_map_apply_spool_list(mmu_model_t *m, const cJSON *arr);

/**
 * Parse a Spoolman vendor list (the `result` from `server.spoolman.proxy`
 * GET /v1/vendor) into @p m->vendors[]: the names that fit whole in
 * MMU_VENDOR_LEN, without repeats, at most MMU_MAX_VENDORS of them, sorted
 * case-insensitively. Returns false when @p arr is not a JSON array.
 */
bool status_map_apply_vendor_list(mmu_model_t *m, const cJSON *arr);

/**
 * Parse a `print_stats` object (from the subscribe result or a diff) and
 * store the `filename` field in @p m->print_filename.
 *
 * Returns false when @p obj is not a JSON object.
 */
bool status_map_apply_print_stats(mmu_model_t *m, const cJSON *obj);

/**
 * Parse a `server.files.metadata` result object and fill @p m->job_need_g[]
 * from the `filament_weights` array (values rounded up to whole grams).
 * Recomputes @p m->job_short and pushes a pause-severity alert (into
 * @p m->alert_pending) for each tool with a shortfall.
 *
 * Returns false when @p obj is not a JSON object or has no filament_weights.
 */
bool status_map_apply_metadata(mmu_model_t *m, const cJSON *obj);

#endif /* STATUS_MAP_H */
