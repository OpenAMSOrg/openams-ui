/**
 * @file moonraker_client.h
 * The transport-independent half of the Moonraker link.
 *
 * The display mirrors a live AMS through Moonraker's WebSocket JSON-RPC 2.0
 * API. Everything that decides *what* to say — the handshake, the object
 * subscription, the diff merge, the request timeouts and the reconnect
 * backoff — lives here, in pure C, driven only by text messages in and a
 * send-text callback out. The transport (a POSIX WebSocket in the simulator,
 * `esp_websocket_client` in the firmware) owns the socket and the clock and
 * feeds this state machine.
 *
 * Moonraker sends diffs (only the top-level keys of a subscribed object that
 * changed), so this client neither merges nor interprets any of them: every
 * subscribed key, at subscribe and again on every later diff, goes out
 * exactly as received through `on_object`. The consumer (`core_apply`, see
 * `core.h`) owns the merge into a full object and the mapping onto the
 * model. The `openams_spoolman` component status (C4) is not part of
 * Moonraker's own status-update diffs, so it keeps its own callback.
 *
 * One client instance per program: the API is a set of free functions over a
 * single file-static state, so there is no handle to thread through.
 *
 * No sockets, no threads, no LVGL, no platform headers.
 */

#ifndef MOONRAKER_CLIENT_H
#define MOONRAKER_CLIENT_H

#include "lib/cjson/cJSON.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Where the link is in the handshake. */
typedef enum {
    MC_LINK_DOWN,         /**< no socket, waiting for the reconnect timer */
    MC_LINK_CONNECTING,   /**< socket up, `server.info` in flight */
    MC_LINK_KLIPPY_WAIT,  /**< connected but Klippy is not ready yet */
    MC_LINK_READY         /**< subscribed; status updates flow */
} mc_link_t;

/**
 * The transport's hooks and the display's consumers. Every callback may be
 * NULL. `ctx` is passed back verbatim.
 *
 * The `const cJSON *` arguments are owned by the client and stay valid until
 * the next callback of the same kind (or `mc_deinit`); a consumer that keeps
 * one must duplicate it.
 */
typedef struct {
    void *ctx;

    /** Put one WebSocket text frame on the wire. False when it cannot be
     *  queued; the outgoing request is then reported as failed. */
    bool (*send_text)(void *ctx, const char *text, size_t len);

    /**
     * One raw, unmerged object for a subscribed key: once from the subscribe
     * result (the object's full current value) and then again for every
     * `notify_status_update` diff that carries that key (a partial value —
     * only the sub-keys that changed). `kind`/`name`:
     *   - "openams_ui", @p name = the bound unit — `openams_ui <unit>`
     *   - "oams_manager", @p name = "" — only when the object list publishes
     *     `oams_manager`
     *   - "print_stats", @p name = ""
     *   - "toolhead", @p name = ""
     * @p obj is owned by the client and stays valid only for the callback
     * (see the struct comment); the consumer duplicates whatever it merges.
     */
    void (*on_object)(void *ctx, const char *kind, const char *name, const cJSON *obj);

    /** The `openams_spoolman` component status (C4), full each time. */
    void (*on_component_status)(void *ctx, const cJSON *status);

    /**
     * Link state changes, in order and without repeats. READY fires before
     * the subscribe result's objects are handed to `on_object`/
     * `on_component_status`, so a consumer that owns merged copies (see
     * `core.h`) can drop them there first: on the very first connect there
     * is nothing to drop, and on a reconnect (a Klipper restart or a
     * Moonraker restart) whatever it owned is now stale.
     */
    void (*on_link)(void *ctx, mc_link_t state);

    /** The answer to `mc_send_gcode()`. @p ok false carries the JSON-RPC
     *  error message, "timeout" or "disconnected". */
    void (*on_gcode_result)(void *ctx, int request_id, bool ok, const char *message);

    /** The answer to `mc_send_rpc()`.  @p tag is the value passed to
     *  mc_send_rpc(); @p result is the JSON result on success (NULL on error);
     *  @p error is the message on failure (NULL on success). */
    void (*on_rpc_result)(void *ctx, int tag, const cJSON *result, const char *error);
} mc_callbacks_t;

/** Bind to `openams_ui <bound_unit>`. A NULL @p bound_unit takes the first
 *  `openams_ui ...` object `printer.objects.list` reports. */
void mc_init(const mc_callbacks_t *cb, const char *bound_unit);

/** Drop the stored status copy. The client cannot be used again until
 *  `mc_init`. */
void mc_deinit(void);

/** The socket came up: start the handshake at @p now_ms. */
void mc_on_connected(uint32_t now_ms);

/** The socket went away: fail pending work and arm the reconnect backoff. */
void mc_on_disconnected(uint32_t now_ms);

/** Feed one received text frame. False when it is malformed, not a JSON
 *  object, or over 16 KB; such a frame is ignored, never fatal. */
bool mc_on_text(const char *text, size_t len, uint32_t now_ms);

/** Advance time: request timeouts, the Klippy retry and the reconnect clock. */
void mc_poll(uint32_t now_ms);

/** How long a load or an unload may take to answer. Moonraker answers
 *  `printer.gcode.script` only once the script has run - heating, feeding,
 *  purging - so the 10 s a plain request gets would call a working load a
 *  failure (it once said "Could not start loading: timeout" mid-load). */
#define MC_GCODE_RUN_TIMEOUT_MS (15u * 60u * 1000u)

/** Send `printer.gcode.script`. Returns the request id, or -1 when the link
 *  is not READY. An unanswered script fails with "timeout" after 10 s. */
int mc_send_gcode(const char *script);

/** The same, with its own deadline: MC_GCODE_RUN_TIMEOUT_MS for a script that
 *  runs an operation. */
int mc_send_gcode_within(const char *script, uint32_t timeout_ms);

/** Send an arbitrary JSON-RPC method; always takes ownership of @p params
 *  (may be NULL), even on -1. @p tag is passed back verbatim to on_rpc_result.
 *  Returns the request id, or -1 when the link is not READY or the frame
 *  cannot be sent. */
int mc_send_rpc(const char *method, cJSON *params, int tag);

/** The current link state. */
mc_link_t mc_link(void);

/** Why the link is in MC_LINK_KLIPPY_WAIT (the last reason, until READY). */
typedef enum {
    MC_WAIT_NONE,
    MC_WAIT_KLIPPY,      /**< Klipper is not ready; detail = its state */
    MC_WAIT_NO_OPENAMS,  /**< ready, but no `openams_ui <unit>` object at all */
    MC_WAIT_NO_UNIT      /**< ready, but no `openams_ui <bound unit>`; detail = the unit */
} mc_wait_t;

/** The reason, with its detail (Klipper's state or the bound unit) copied
 *  into @p detail ("" when there is none). */
mc_wait_t mc_wait_reason(char *detail, size_t n);

/** True once the backoff after a disconnect has elapsed, so the transport may
 *  open a new socket. The delay is 1 s, 2 s, 4 s … capped at 30 s and resets
 *  when the link reaches READY. */
bool mc_reconnect_due(uint32_t now_ms);

#endif /* MOONRAKER_CLIENT_H */
