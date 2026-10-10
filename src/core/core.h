/**
 * @file core.h
 * The one merge-owning path from a raw Moonraker object to the model
 * (docs/design/UNIFIED_UI.md 3, 8).
 *
 * Moonraker sends diffs: only the top-level keys of an object that changed.
 * `status_map_apply_*` needs the *whole* object (a diff without `bays` would
 * otherwise erase every bay the model already knew about), so something has
 * to keep an owned, merged copy per (kind, name) and hand the appliers that
 * copy instead of the raw diff. This is that something: both the ESP32 (over
 * its shadow model) and the simulator hold one `core_t` and feed it every
 * object moonraker_client (or, later, a web host's own store) delivers.
 *
 * Pure C: no LVGL, no platform headers. The owned copies are heap cJSON, not
 * static storage, so a core adds nothing to the static-DRAM gate.
 */

#ifndef CORE_H
#define CORE_H

#include "lib/cjson/cJSON.h"
#include "model/mmu_model.h"

#include <stdbool.h>

typedef struct core core_t;

/** A core over @p model, which it does not own and which must outlive it.
 *  Returns NULL when @p model is NULL or there is no memory for the core. */
core_t *core_create(mmu_model_t *model);

/** Free @p core and every owned copy. Does not touch @p model. */
void core_destroy(core_t *core);

/**
 * Drop every owned copy and reset the model (status_map_reset()), keeping
 * the alert history when @p keep_alerts. Call this when the link reaches
 * READY again after a disconnect (a Klipper restart or a Moonraker
 * reconnect), before the fresh subscribe result is applied: the stale copies
 * (and a stale settings/lane state) must not survive into the new session.
 */
void core_reset(core_t *core, bool keep_alerts);

/**
 * Merge @p obj's top-level keys into the copy @p core owns for (@p kind,
 * @p name): each key already in the copy is replaced, each new key is added
 * (the rule moonraker_client's own merge used to apply, now owned here so a
 * future web host can feed the same raw diffs from its store). `spool_list`,
 * `vendor_list` and `metadata` are whole results rather than diffs, so they replace the
 * owned copy outright instead of merging into it.
 *
 * The merged (or replaced) copy is then handed to the matching
 * `status_map_apply_*()`:
 *   - "openams_ui" (@p name = the unit)  -> status_map_apply_openams_ui()
 *   - "oams_manager"                     -> status_map_apply_settings() on
 *                                            its `settings` key and
 *                                            status_map_apply_lanes() on its
 *                                            `lanes_by_fps` key, each only
 *                                            when present
 *   - "component_status"                 -> status_map_apply_component_status()
 *   - "print_stats"                      -> status_map_apply_print_stats()
 *   - "toolhead"                         -> no applier; its `extruder` is
 *                                            recorded on the core for later
 *                                            (the active toolhead has no
 *                                            consumer yet)
 *   - "spool_list"                       -> status_map_apply_spool_list()
 *   - "vendor_list"                      -> status_map_apply_vendor_list()
 *   - "metadata"                         -> status_map_apply_metadata()
 *   - "action_result"                    -> the host's answer to an action
 *                                            the display sent ({kind, ok,
 *                                            message}); it is not a status, so
 *                                            it owns no copy and goes straight
 *                                            to the model
 * `mmu_model_refresh_this()` runs afterwards either way (most appliers
 * already call it themselves; this covers "toolhead", which has none).
 *
 * @p name is ignored (may be NULL) for every kind but "openams_ui", which
 * keeps one owned copy per unit.
 *
 * Returns true when the owned copy actually changed content (a proxy for
 * "the model changed": the appliers are deterministic functions of that
 * copy, so an unchanged copy cannot have mapped to a different model). An
 * unrecognized @p kind, or @p obj of the wrong JSON type for @p kind
 * (an object for every kind but "spool_list" and "vendor_list", which are arrays), returns
 * false without touching anything.
 */
bool core_apply(core_t *core, const char *kind, const char *name, const cJSON *obj);

/**
 * Show the view's text in @p code ("en", "qps"): every string the builders
 * emit comes from that language's table, and an id it does not translate
 * falls back to English (view_set_language(), which this forwards to). The
 * choice is a setting of the one core rather than of the model, so it is not
 * reset by core_reset() and costs the core nothing.
 *
 * False (and the current language kept) when no such language is built in.
 * The caller rebuilds the view afterwards: nothing is cached.
 */
bool core_set_language(core_t *core, const char *code);

/**
 * The display just sent a load or an unload (@p kind, from
 * mmu_action_kind_of() on the action line) and the host has not reported it
 * yet, so the load screen says it is starting rather than "Nothing is
 * loading". MMU_ACTION_OTHER is not a wait and does nothing. Returns true
 * when the model changed.
 */
bool core_action_sent(core_t *core, mmu_action_kind_t kind);

/**
 * The answer to an RPC the display sent (@p tag is actions_rpc_tag()'s, the one
 * handed to mc_send_rpc()): a spool or vendor list, a file's metadata, or the
 * host's word on a spool edit (@p error is its message, NULL when it took the
 * edit; the outcome lands in the model's edit_refusal and edit_seq). Returns
 * true when the model changed.
 */
bool core_rpc_result(core_t *core, int tag, const cJSON *result, const char *error);

#endif /* CORE_H */
