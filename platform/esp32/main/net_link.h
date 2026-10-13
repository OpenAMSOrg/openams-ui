/**
 * @file net_link.h
 * The ESP32 half of the Moonraker link: it owns the `esp_websocket_client`
 * transport (connect, reconnect, frame reassembly), feeds the received text
 * into the portable `moonraker_client` state machine, and keeps a shadow copy
 * of the live model. The LVGL task picks that copy up with net_link_sync();
 * the network side never touches LVGL.
 */
#pragma once

#include "lib/cjson/cJSON.h"
#include "model/mmu_model.h"

#include "net_config.h"

#include <stdbool.h>

/** Start the link from @p cfg. No-op when `cfg->moonraker_url` is empty. */
void net_link_start(const net_config_t *cfg);

/** LVGL task: copy the live model into @p m when it changed; true when it
 * did. Always false when the link was never started. */
bool net_link_sync(mmu_model_t *m);

/** Send @p script as G-code over the link; false when the link was never
 * started or is not READY (the request is not queued for later). Called from
 * the LVGL task, so it must not block on anything but the link's own lock.
 *
 * @p kind is what the action line does (mmu_action_kind_of()), kept against
 * the request's id so the answer names the action it is about; pass
 * MMU_ACTION_OTHER for a line that is not one of ours. @p request_id, when
 * not NULL, receives the id the answer will come under (-1 when nothing was
 * queued). */
bool net_link_send_gcode(const char *script, mmu_action_kind_t kind, int *request_id);

/** Send an arbitrary JSON-RPC method.  Always takes ownership of @p params
 * (may be NULL).  @p tag is passed back to the on_rpc_result callback. False
 * when the link was never started or is not READY. */
bool net_link_send_rpc(const char *method, cJSON *params, int tag);

/** Stop the Moonraker link and free the websocket client's buffers (its RX
 * buffer and 6 KB task), so a foreground feature can use the heap; the live
 * model stops updating until net_link_resume(). No-op if the link was never
 * started or is already paused. Called from the LVGL task. */
void net_link_pause(void);
/** Rebuild the websocket client and reconnect after net_link_pause(). */
void net_link_resume(void);

/** Print what the link has received and applied (the console's `ui`). */
void net_link_diag_print(void);
