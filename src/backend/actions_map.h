/**
 * @file actions_map.h
 * Map a display action line to the host G-code it sends.
 *
 * ARCHITECTURE: "Display tap -> actions.c logs the action line -> backend maps
 * it to a G-code string -> printer.gcode.script". Every screen builds its line
 * through actions_log() (src/app/actions.c); this turns the ones the host
 * knows (C3) into a command string, and leaves the rest alone.
 *
 * Pure C: no LVGL, no platform headers. The simulator and (later) the firmware
 * run the same code. Nothing here talks to the link; wiring it up is a later
 * task.
 */

#ifndef ACTIONS_MAP_H
#define ACTIONS_MAP_H

#include "lib/cjson/cJSON.h"
#include "model/mmu_model.h"

#include <stddef.h>

/**
 * Translate one display action @p line against the live model @p m.
 *
 * Returns 1 and writes the G-code into @p out (several commands separated by
 * '\n') when @p line is a host action; 0 when it is a local UI action (nothing
 * to send); -1 when it is a host action the model cannot address yet (then
 * @p out holds a short reason for the log, e.g. "bay not in a group").
 *
 * The model is read only. Never writes past @p out_len: a command that does
 * not fit makes the result -1, never a truncated command.
 */
int actions_map(const char *line, const mmu_model_t *m, char *out, size_t out_len);

/**
 * Translate one display action @p line to a Moonraker RPC call.
 *
 * Returns 1 and fills @p method (at most @p mlen bytes) and @p *params (a new
 * cJSON object the caller owns) when @p line is an RPC action.  Returns 0 when
 * @p line is not an RPC action (method and params are then unchanged).
 *
 * RPC actions: "spool list", "vendor list", "spool confirm <slot> <grams>",
 * "job metadata <file>" and "spool edit <slot> key=value ..." (see
 * actions_rpc_why()). A line actions_rpc_why() refuses is not an RPC action
 * here: 0.
 */
int actions_rpc(const char *line, const mmu_model_t *m,
                char *method, size_t mlen, cJSON **params);

/**
 * actions_rpc() with the refusals told apart: returns 1 as it does, 0 when
 * @p line is not an RPC action, and -1 when it is one the model cannot send
 * right now, with @p why (when not NULL) pointing at the reason: a fixed
 * string from the view's table.
 *
 * `spool edit <slot> [material=<n>] [color=<rgb>] [vendor=<n>] [remaining=<g>]
 * [initial=<g>] [pa=<x>] [then load]` becomes
 * server.openams_spoolman.edit. The values are the edit form's own: material
 * and vendor are indices into the lists it offers (mmu_material_name(),
 * mmu_edit_vendor_name(); an index one past the list is the bay's own), color
 * is 0xRRGGBB in decimal or -1, the weights are grams, and pa is the pressure
 * advance. Only what differs from mmu_edit_defaults() is sent (a spool with no
 * host record is always sent with its material: the host adds it). Refused
 * while the host has no edit endpoint, Spoolman is offline, or the bay is
 * empty; with nothing changed ("No changes to save"); and for a value out of
 * range. A `then load` line whose material is still the -1 of an unanswered
 * "Choose a material" is refused for that, in its own words.
 */
int actions_rpc_why(const char *line, const mmu_model_t *m,
                    char *method, size_t mlen, cJSON **params, const char **why);

/**
 * actions_rpc_why() and, beside the method and params, what the caller sends
 * once the host has taken the call: @p then_line (when not NULL) is the line
 * `then load` asked for - "load <slot>" - or "" for a line that asks for
 * nothing after it. It is the renderer that sends it, and only then: a refusal
 * has to leave the user where they were.
 *
 * actions_rpc_why() is this without the follow-up, for the callers that never
 * need one.
 */
int actions_rpc_then(const char *line, const mmu_model_t *m,
                     char *method, size_t mlen, cJSON **params,
                     char *then_line, size_t tlen, const char **why);

/** What a caller does with an RPC's answer: the tag it hands mc_send_rpc(), which
 * core_rpc_result() reads back. */
enum {
    ACTIONS_RPC_NONE = 0,          /**< nothing to do with the answer */
    ACTIONS_RPC_SPOOL_LIST = 1,    /**< `spool list`: an array of spools */
    ACTIONS_RPC_METADATA = 2,      /**< `job metadata`: a file's metadata */
    ACTIONS_RPC_VENDOR_LIST = 3,   /**< `vendor list`: an array of vendors */
    ACTIONS_RPC_EDIT = 4,          /**< `spool edit`: the host took it, or refused it in words */
};

/** The tag of the call @p method with @p params (as actions_rpc() made them). */
int actions_rpc_tag(const char *method, const cJSON *params);

/**
 * The one enabled/reason predicate the view builders use
 * (docs/design/UNIFIED_UI.md 4): dry-runs @p line against both actions_map()
 * and actions_rpc() and reports whether it would go anywhere.
 *
 * Returns 1 (enabled, @p why left "") when actions_map() or actions_rpc()
 * would send it; 0 (disabled) with @p why filled from actions_map()'s reason
 * when actions_map() returns -1 (a host line the model cannot address right
 * now, e.g. "positioning"). A @p line neither map recognizes at all - which a
 * builder should never dry-run, since it only ever builds lines from the host
 * vocabulary - is reported enabled with no reason rather than silently
 * hiding a real action behind a false negative.
 *
 * @p line is expected to already have any form placeholders resolved to the
 * form's default values (the caller's job: the stored action.line the
 * renderer receives keeps the placeholders, only the check's own copy is
 * resolved).
 *
 * @p why, when not NULL, receives the reason: a pointer to a fixed string
 * from the view's table (view_str()) or to the host's own words. It is a
 * pointer and not a buffer so a translation of any length reaches the view
 * whole, and it stays valid after the call - a builder fills one view_action_t
 * per action, so a scratch buffer would leave every action but the last
 * pointing at the last one's text. "command too long" is the one reason that
 * is a literal here.
 */
int actions_check(const char *line, const mmu_model_t *m, const char **why);

/**
 * Why @p u cannot dry, in the current language: "" when it can. A unit whose
 * `act` lacks `dryer_start` is worded here, live, so a language change reaches
 * it (the model's `cannot_dry_reason` is the same sentence as mapped, and is
 * the host's own words for any other reason). The pointer is a fixed string or
 * the model's buffer.
 */
const char *actions_dry_reason(const mmu_unit_t *u);

#endif /* ACTIONS_MAP_H */
