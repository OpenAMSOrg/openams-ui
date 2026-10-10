/**
 * @file view_json.h
 * Walks the view builders into the §4 JSON, for the web renderer and for the
 * goldens (docs/design/UNIFIED_UI.md §4, §9).
 *
 * Pure C: no LVGL, no platform headers.
 */

#ifndef VIEW_JSON_H
#define VIEW_JSON_H

#include "lib/cjson/cJSON.h"
#include "model/mmu_model.h"

/**
 * Build the whole panel tree (docs/design/UNIFIED_UI.md 4's `panel` shape):
 * every toolhead with its units and their bays, every unassigned unit, every
 * alert and the settings list. The caller owns the returned tree
 * (cJSON_Delete()).
 */
cJSON *view_json_build(const mmu_model_t *m);

/**
 * view_json_build() then cJSON_PrintUnformatted() (compact, for the web):
 * the caller frees the returned string (free()).
 */
char *view_json(const mmu_model_t *m);

#endif /* VIEW_JSON_H */
