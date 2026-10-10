/**
 * @file net_wifi.c
 * Wi-Fi station: connect, reconnect with backoff, publish the IPv4 address.
 */
#include "net_wifi.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "net_wifi";

#define IP_STR_LEN 16
#define BACKOFF_MIN_S 1
#define BACKOFF_MAX_S 30

static volatile bool s_up;
static char s_ip[IP_STR_LEN] = "";

static esp_timer_handle_t s_retry_timer;
static uint32_t s_backoff_s = BACKOFF_MIN_S;

static void retry_cb(void *arg)
{
    (void)arg;
    esp_wifi_connect();
}

static void schedule_retry(void)
{
    ESP_LOGI(TAG, "reconnecting in %us", (unsigned)s_backoff_s);
    esp_err_t err = esp_timer_start_once(s_retry_timer, (uint64_t)s_backoff_s * 1000000ULL);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "retry timer start failed: %s", esp_err_to_name(err));
    }
    s_backoff_s = (s_backoff_s >= BACKOFF_MAX_S) ? BACKOFF_MAX_S : s_backoff_s * 2;
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;

    if(base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
    else if(base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_up = false;
        s_ip[0] = '\0';
        const wifi_event_sta_disconnected_t *d = (const wifi_event_sta_disconnected_t *)data;
        ESP_LOGW(TAG, "disconnected (reason %d)", (int)d->reason);
        schedule_retry();
    }
    else if(base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = (const ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
        s_backoff_s = BACKOFF_MIN_S;
        s_up = true;
        ESP_LOGI(TAG, "got IP %s", s_ip);
    }
}

void net_wifi_start(const net_config_t *cfg)
{
    if(cfg->ssid[0] == '\0') {
        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_event,
                                                        NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_event,
                                                        NULL, NULL));

    const esp_timer_create_args_t retry_args = {
        .callback = &retry_cb,
        .name = "wifi_retry",
    };
    ESP_ERROR_CHECK(esp_timer_create(&retry_args, &s_retry_timer));

    wifi_config_t wc;
    memset(&wc, 0, sizeof(wc));
    strlcpy((char *)wc.sta.ssid, cfg->ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, cfg->password, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = cfg->password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "station starting, ssid \"%s\"", cfg->ssid);
}

bool net_wifi_is_up(void)
{
    return s_up;
}

const char *net_wifi_ip(void)
{
    return s_ip;
}
