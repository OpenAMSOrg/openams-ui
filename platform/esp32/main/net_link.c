/**
 * @file net_link.c
 * The ESP32 Moonraker link (see net_link.h).
 *
 * Threading: the WebSocket events run in esp_websocket_client's own task and
 * `mc_poll` runs in ours, while the `moonraker_client` state machine and the
 * shadow model are plain statics with no internal locking. One FreeRTOS
 * mutex therefore guards every `mc_*` call and every shadow access; the
 * callbacks below are always entered with it held, so they never take it
 * themselves (it is not recursive). Nothing here calls LVGL: the LVGL task
 * only ever reads a copy made by net_link_sync().
 *
 * Lock order: the WebSocket client holds its own lock while it delivers an
 * event (which then takes s_lock), so nothing may take the client's lock
 * while holding s_lock. Outgoing text therefore never goes straight to the
 * client from a callback: send_text_cb queues a heap copy, and net_task sends
 * the queue after releasing s_lock.
 */
#include "net_link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend/actions_map.h"
#include "backend/moonraker_client.h"
#include "backend/status_map.h"
#include "core/core.h"
#include "app/platform.h"
#include "net_wifi.h"

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "net_link";

/* A Moonraker status message is a JSON object of a few kilobytes; one this
 * large is not one we can use, so it is dropped rather than grown for. */
#define RX_MAX 8192

/* The live model, written only by the network side (under the lock) and
 * copied out to g_mmu by the LVGL task. Internal RAM: the LVGL task reads it
 * often and it must never sit behind a PSRAM cache stall. */
static mmu_model_t *s_shadow;
static bool s_dirty;

/* The merge-owning path onto s_shadow (core.h). Its per-(kind,name) copies
 * are heap cJSON, so this adds nothing to the static-DRAM gate. */
static core_t *s_core;

static SemaphoreHandle_t s_lock;
static esp_websocket_client_handle_t s_client;

/* What each outstanding G-code request was, so the answer can name the action
 * it is about. Moonraker's ids are small and go up one at a time, and the
 * display sends one action at a time, so the last few are all that can be
 * outstanding; a byte each is what makes this fit. */
#define SENT_KINDS 8
static int s_sent_id[SENT_KINDS];
static uint8_t s_sent_kind[SENT_KINDS];
static int s_sent_next;

/* Remember what a request of @p id was, under the lock the caller already
 * holds. */
static void sent_remember(int id, mmu_action_kind_t kind)
{
    if(id < 0) return;
    s_sent_id[s_sent_next] = id;
    s_sent_kind[s_sent_next] = (uint8_t)kind;
    s_sent_next = (s_sent_next + 1) % SENT_KINDS;
}

/* What the request of @p id was, MMU_ACTION_OTHER when it is not one of ours
 * (or has already been forgotten). Under the lock. */
static mmu_action_kind_t sent_kind_of(int id)
{
    int i;

    for(i = 0; i < SENT_KINDS; i++)
        if(s_sent_id[i] == id) return (mmu_action_kind_t)s_sent_kind[i];
    return MMU_ACTION_OTHER;
}

/* Outgoing messages (heap copies, freed by whoever takes them off). A JSON-RPC
 * request is a few hundred bytes and the client sends a handful at a time. */
#define TXQ_DEPTH 8
static QueueHandle_t s_txq;

/* One received message may span several frames (text then continuation) and
 * each frame may be handed over in several chunks, so this is the message
 * being reassembled, not the chunk. */
static char *s_rx;
static size_t s_rx_len;
static bool s_rx_drop; /* the message does not fit; ignore it to its end */

/* What the home screen names while there is no printer data (see
 * platform_link_status): the network and the URL this link was started with. */
static char s_ssid[33];
static char s_url[128];

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static const char *link_state_name(mc_link_t state)
{
    switch(state) {
    case MC_LINK_DOWN: return "down";
    case MC_LINK_CONNECTING: return "connecting";
    case MC_LINK_KLIPPY_WAIT: return "klippy-wait";
    case MC_LINK_READY: return "ready";
    }
    return "?";
}

/* ----------------------------------------------------------- mc callbacks */
/* All of these run with s_lock held (see the file header). */

static bool send_text_cb(void *ctx, const char *text, size_t len)
{
    char *copy;

    (void)ctx;
    if(!s_client || !s_txq) return false;
    copy = malloc(len + 1);
    if(!copy) return false;
    memcpy(copy, text, len);
    copy[len] = '\0';
    if(xQueueSend(s_txq, &copy, 0) != pdTRUE) {
        free(copy); /* full: the client's own timeout retries the request */
        return false;
    }
    return true;
}

/* Free whatever is still queued: after a disconnect it belongs to the old
 * connection and must not reach the next one. */
static void txq_drop_all(void)
{
    char *msg;

    while(s_txq && xQueueReceive(s_txq, &msg, 0) == pdTRUE) free(msg);
}

/* For the console's `ui` command: what has come in and what the core made of it. */
static unsigned s_diag_objects, s_diag_refused, s_diag_syncs;
static char s_diag_last[48];

static void on_object_cb(void *ctx, const char *kind, const char *name, const cJSON *obj)
{
    (void)ctx;
    s_diag_objects++;
    snprintf(s_diag_last, sizeof s_diag_last, "%s%s%s", kind ? kind : "?", name ? " " : "", name ? name : "");
    if(!core_apply(s_core, kind, name, obj)) {
        /* Never silent: a refused object leaves the display without the
         * printer's state (an empty home screen), and the usual cause is a
         * heap too small for the core's copy of it. */
        s_diag_refused++;
        ESP_LOGW(TAG, "the core did not take %s (heap free %u, largest block %u)", s_diag_last,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    s_dirty = true;
}

void net_link_diag_print(void)
{
    unsigned units = 0, toolheads = 0;

    if(s_lock && s_shadow) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        units = s_shadow->unit_count;
        toolheads = s_shadow->toolhead_count;
        xSemaphoreGive(s_lock);
    }
    printf("link: state=%s objects=%u not_applied=%u last=%s syncs=%u shadow units=%u toolheads=%u\n",
           link_state_name(mc_link()), s_diag_objects, s_diag_refused,
           s_diag_last[0] ? s_diag_last : "-", s_diag_syncs, units, toolheads);
}

static void on_component_status_cb(void *ctx, const cJSON *status)
{
    (void)ctx;
    core_apply(s_core, "component_status", NULL, status);
    s_dirty = true;
}

static void on_link_cb(void *ctx, mc_link_t state)
{
    int i;

    (void)ctx;
    if(state == MC_LINK_KLIPPY_WAIT) {
        char detail[48];

        switch(mc_wait_reason(detail, sizeof detail)) {
        case MC_WAIT_KLIPPY:
            ESP_LOGI(TAG, "Moonraker link klippy-wait: Klipper is not ready (%s)", detail);
            break;
        case MC_WAIT_NO_OPENAMS:
            ESP_LOGW(TAG, "Moonraker link klippy-wait: the printer has no openams_ui object "
                          "(is OpenAMS installed and up to date?)");
            break;
        case MC_WAIT_NO_UNIT:
            ESP_LOGW(TAG, "Moonraker link klippy-wait: no \"openams_ui %s\" object "
                          "(the unit this display is bound to)", detail);
            break;
        default:
            ESP_LOGI(TAG, "Moonraker link klippy-wait");
            break;
        }
    }
    else {
        ESP_LOGI(TAG, "Moonraker link %s", link_state_name(state));
    }
    /* Every change: while there is no printer data the home screen says
     * where the link is, so it has to be redrawn. */
    s_dirty = true;

    if(state == MC_LINK_DOWN) {
        for(i = 0; i < (int)s_shadow->unit_count; i++) s_shadow->units[i].connected = false;
        s_dirty = true;
    }
    else if(state == MC_LINK_READY) {
        /* A fresh subscribe result is about to be applied (moonraker_client
         * reaches READY before dispatching it): drop whatever copies and
         * settings/lane state the core owns first, so a reconnect after a
         * Klipper restart cannot leave stale ones behind. Harmless (a no-op)
         * on the very first connect, when there is nothing to drop yet. */
        core_reset(s_core, true);
        s_dirty = true;
    }
}

/* The host's answer to a G-code line the display sent. A refusal stops here
 * otherwise: it reaches the model through core_apply("action_result"), so the
 * load screen says the host's words and any other refusal is said once as a
 * toast (see app_live_poll). */
static void on_gcode_result_cb(void *ctx, int request_id, bool ok, const char *message)
{
    cJSON *obj;
    mmu_action_kind_t kind;

    (void)ctx;
    ESP_LOGI(TAG, "gcode id=%d ok=%d %s", request_id, ok ? 1 : 0, message ? message : "");

    kind = sent_kind_of(request_id);
    if(kind == MMU_ACTION_OTHER && ok) return;   /* nothing to tell the display */

    obj = cJSON_CreateObject();
    if(!obj) return;
    cJSON_AddNumberToObject(obj, "kind", (double)kind);
    cJSON_AddBoolToObject(obj, "ok", ok);
    cJSON_AddStringToObject(obj, "message", message ? message : "");
    if(core_apply(s_core, "action_result", NULL, obj)) s_dirty = true;
    cJSON_Delete(obj);
}

/* The answer to an RPC the display sent: a spool or vendor list or a file's
 * metadata lands in the shadow model, and the host's word on a spool edit is
 * kept there for the LVGL task to say (see app_live_poll). */
static void on_rpc_result_cb(void *ctx, int tag, const cJSON *result, const char *error)
{
    (void)ctx;
    if(error) ESP_LOGW(TAG, "rpc tag=%d error=%s", tag, error);
    if(core_rpc_result(s_core, tag, result, error)) s_dirty = true;
}

/* ------------------------------------------------------------- transport */

/* One chunk of a data frame: append it to the message under construction and
 * deliver the message once its final frame (and that frame's final chunk)
 * has arrived. */
static void on_ws_data(const esp_websocket_event_data_t *d)
{
    int complete;

    if(d->data_len <= 0 || d->payload_offset < 0) return;
    complete = (d->payload_offset + d->data_len == d->payload_len);

    if(s_rx_drop) {
        if(complete && d->fin) s_rx_drop = false;
        return;
    }
    if(s_rx_len + (size_t)d->data_len > RX_MAX) {
        ESP_LOGW(TAG, "dropping a WebSocket message over %d bytes", RX_MAX);
        s_rx_len = 0;
        s_rx_drop = !(complete && d->fin); /* a later frame carries the rest */
        return;
    }

    memcpy(s_rx + s_rx_len, d->data_ptr, (size_t)d->data_len);
    s_rx_len += (size_t)d->data_len;

    if(complete && d->fin) {
        s_rx[s_rx_len] = '\0';
        if(!mc_on_text(s_rx, s_rx_len, now_ms())) {
            /* A message the client refused (malformed, or the heap could not
             * hold its parse) is otherwise invisible: the handshake just
             * stalls. One line per 5 s keeps a broadcast storm off the log. */
            static uint32_t s_last_reject_ms;
            uint32_t t = now_ms();

            if(s_last_reject_ms == 0 || (uint32_t)(t - s_last_reject_ms) >= 5000) {
                s_last_reject_ms = t ? t : 1;
                ESP_LOGW(TAG, "rejected a %u-byte message (free heap %u, largest block %u)",
                         (unsigned)s_rx_len,
                         (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                         (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
            }
        }
        s_rx_len = 0;
    }
}

static void on_ws_event(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    esp_websocket_event_data_t *d = (esp_websocket_event_data_t *)event_data;
    uint32_t now = now_ms();

    (void)arg;
    (void)base;
    if(!s_lock) return;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    switch(id) {
    case WEBSOCKET_EVENT_CONNECTED:
        mc_on_connected(now);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
        mc_on_disconnected(now);
        txq_drop_all();
        break;
    case WEBSOCKET_EVENT_DATA:
        /* Text (1) and its continuation (0) only; ping/pong/close payloads
         * are the transport's business, not the display's. */
        if(d && (d->op_code == 0x1 || d->op_code == 0x0)) on_ws_data(d);
        break;
    default:
        break;
    }
    xSemaphoreGive(s_lock);
}

/* ----------------------------------------------------------- network task */

static void net_task(void *arg)
{
    (void)arg;
    for(;;) {
        uint32_t now = now_ms();

        char *msg;

        xSemaphoreTake(s_lock, portMAX_DELAY);
        mc_poll(now);
        xSemaphoreGive(s_lock);

        /* Outside s_lock (see the lock order in the file header). */
        while(xQueueReceive(s_txq, &msg, 0) == pdTRUE) {
            if(esp_websocket_client_send_text(s_client, msg, (int)strlen(msg),
                                              pdMS_TO_TICKS(1000)) < 0) {
                ESP_LOGW(TAG, "send failed; the request will time out and retry");
            }
            free(msg);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* -------------------------------------------------------------- public API */

void net_link_start(const net_config_t *cfg)
{
    mc_callbacks_t cb;
    esp_websocket_client_config_t ws_cfg;

    if(!cfg || cfg->moonraker_url[0] == '\0') return;
    snprintf(s_ssid, sizeof s_ssid, "%s", cfg->ssid);
    snprintf(s_url, sizeof s_url, "%s", cfg->moonraker_url);

    /* MALLOC_CAP_8BIT as well: internal memory alone can be IRAM, which only
     * takes 32-bit accesses, and the model is filled byte by byte (memset,
     * strings). A model grown past the DRAM holes lands there otherwise and
     * panics with LoadStoreError on the first byte store. */
    s_shadow = heap_caps_malloc(sizeof(mmu_model_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_rx = heap_caps_malloc(RX_MAX + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_lock = xSemaphoreCreateMutex();
    s_txq = xQueueCreate(TXQ_DEPTH, sizeof(char *));
    if(!s_shadow || !s_rx || !s_lock || !s_txq) {
        ESP_LOGE(TAG, "no memory for the link — staying in demo mode");
        return;
    }
    status_map_reset(s_shadow);

    /* The core's own copies are heap cJSON (see core.h); this adds nothing
     * static. */
    s_core = core_create(s_shadow);
    if(!s_core) {
        ESP_LOGE(TAG, "no memory for the core — staying in demo mode");
        return;
    }

    memset(&ws_cfg, 0, sizeof(ws_cfg));
    ws_cfg.uri = cfg->moonraker_url;
    ws_cfg.buffer_size = 2048;
    ws_cfg.task_stack = 6144;
    ws_cfg.reconnect_timeout_ms = 5000;
    ws_cfg.network_timeout_ms = 10000;

    s_client = esp_websocket_client_init(&ws_cfg);
    if(!s_client) {
        ESP_LOGE(TAG, "esp_websocket_client_init failed");
        return;
    }

    memset(&cb, 0, sizeof(cb));
    cb.send_text = send_text_cb;
    cb.on_object = on_object_cb;
    cb.on_component_status = on_component_status_cb;
    cb.on_link = on_link_cb;
    cb.on_gcode_result = on_gcode_result_cb;
    cb.on_rpc_result = on_rpc_result_cb;
    mc_init(&cb, cfg->bind_unit[0] ? cfg->bind_unit : NULL);

    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, on_ws_event, NULL);
    /* The client reconnects by itself, so mc_reconnect_due() is not consulted
     * here; the client's DISCONNECTED event feeds the state machine instead. */
    esp_websocket_client_start(s_client);

    xTaskCreate(net_task, "netlink", 4096, NULL, 3, NULL);
}

bool net_link_sync(mmu_model_t *m)
{
    bool copied = false;

    if(!m || !s_lock || !s_shadow) return false;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if(s_dirty) {
        mmu_model_merge_live(m, s_shadow);
        s_dirty = false;
        copied = true;
        s_diag_syncs++;
    }
    xSemaphoreGive(s_lock);

    return copied;
}

bool net_link_send_gcode(const char *script, mmu_action_kind_t kind, int *request_id)
{
    bool ok;
    int id = -1;

    if(request_id) *request_id = -1;
    if(!s_lock || !script) return false;

    /* The client's send callback only queues a heap copy (see the lock order
     * in the file header), so sending under s_lock is safe. */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    /* A load or an unload is answered once it has run, so it gets that long. */
    id = (kind == MMU_ACTION_OTHER) ? mc_send_gcode(script)
                                    : mc_send_gcode_within(script, MC_GCODE_RUN_TIMEOUT_MS);
    ok = id >= 0;
    if(ok) {
        sent_remember(id, kind);
        /* On the shadow model, not the display's: net_link_sync() copies this
         * one over in full, so anything set on the display's own copy of a
         * load would be gone by the next status message. */
        core_action_sent(s_core, kind);
        s_dirty = true;
    }
    xSemaphoreGive(s_lock);

    if(request_id) *request_id = ok ? id : -1;

    return ok;
}

bool net_link_send_rpc(const char *method, cJSON *params, int tag)
{
    bool ok;

    if(!s_lock || !method) {
        cJSON_Delete(params);
        return false;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    ok = mc_send_rpc(method, params, tag) >= 0;
    xSemaphoreGive(s_lock);

    return ok;
}

platform_link_t platform_link_status(char *detail, size_t n)
{
    platform_link_t status = PLATFORM_LINK_READY;
    char wait[48];

    if(detail && n) detail[0] = '\0';
    if(!s_lock || !s_url[0]) return PLATFORM_LINK_READY;   /* demo mode */
    if(!net_wifi_is_up()) {
        if(detail && n) snprintf(detail, n, "%s", s_ssid);
        return PLATFORM_LINK_WIFI;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    switch(mc_link()) {
    case MC_LINK_DOWN:
    case MC_LINK_CONNECTING:
        status = PLATFORM_LINK_PRINTER;
        if(detail && n) snprintf(detail, n, "%s", s_url);
        break;
    case MC_LINK_KLIPPY_WAIT:
        switch(mc_wait_reason(wait, sizeof wait)) {
        case MC_WAIT_NO_OPENAMS: status = PLATFORM_LINK_NO_OPENAMS; break;
        case MC_WAIT_NO_UNIT: status = PLATFORM_LINK_NO_UNIT; break;
        default: status = PLATFORM_LINK_KLIPPER; break;
        }
        if(detail && n) snprintf(detail, n, "%s", wait);
        break;
    case MC_LINK_READY:
        break;
    }
    xSemaphoreGive(s_lock);
    return status;
}
