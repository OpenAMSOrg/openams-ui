/**
 * @file actions_esp32.c
 * ESP32 platform's implementation of the one hook src/app/actions.c calls.
 */
#include "app/actions.h"

#include "backend/actions_map.h"
#include "lib/cjson/cJSON.h"
#include "model/mmu_model.h"
#include "net_link.h"

#include "esp_log.h"

static const char *TAG = "action";

void platform_action(const char *line)
{
    char out[128];
    char rpc_method[64];
    cJSON *rpc_params = NULL;
    const char *why = "";
    mmu_action_kind_t kind;

    ESP_LOGI(TAG, "%s", line);

    /* With a live link the host owns the machine, so a line that names
     * something it controls goes out as G-code instead of driving the local
     * demo model (which stands down in live mode: see mmu_model_set_live).
     * The local lines (nav, sheets, toasts) map to 0 and stop here. */
    if(!mmu_model_is_live()) return;

    /* A load or an unload is worth waiting on: the link marks it pending on
     * the shadow model, so the load screen says it is starting until the host
     * reports the operation instead of claiming nothing is loading. */
    kind = mmu_action_kind_of(line);

    switch(actions_map(line, &g_mmu, out, sizeof out)) {
    case 1:
        if(net_link_send_gcode(out, kind, NULL)) {
            ESP_LOGI(TAG, "GCODE: %s", out);
        }
        else {
            ESP_LOGW(TAG, "GCODE-REFUSED: link not ready: %s", out);
        }
        return;
    case -1:
        ESP_LOGW(TAG, "ACTION-UNMAPPED: %s", out);
        return;
    default:
        break;
    }

    /* G-code did not match; try RPC. */
    switch(actions_rpc_why(line, &g_mmu, rpc_method, sizeof rpc_method, &rpc_params, &why)) {
    case 1: {
        int tag = actions_rpc_tag(rpc_method, rpc_params);

        if(net_link_send_rpc(rpc_method, rpc_params, tag)) {
            ESP_LOGI(TAG, "RPC: %s", rpc_method);
        }
        else {
            ESP_LOGW(TAG, "RPC-REFUSED: link not ready");
        }
        break;
    }
    case -1:
        ESP_LOGW(TAG, "ACTION-UNMAPPED: %s", why);
        break;
    default:
        break;
    }
}
