/**
 * @file net_console.c
 * A one-time serial setup console. Nothing here is echoed: the host is
 * expected to type blind (the password must not appear on the panel).
 */
#include "net_console.h"
#include "ui_diag.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "net_config.h"
#include "net_wifi.h"
#include "ota.h"

static const char *TAG = "net_console";

#define CONSOLE_UART UART_NUM_0
#define CONSOLE_STACK 3072
#define CONSOLE_PRIORITY 2
#define NETCON_LINE_MAX 160
#define NETCON_ARG_MAX 4

static void print_help(void)
{
    printf("commands:\n");
    printf("  wifi <ssid> [password]   store Wi-Fi credentials (quote the SSID for spaces)\n");
    printf("  moonraker <url>          store the Moonraker WebSocket URL (ws://...)\n");
    printf("  unit <name>              bind to one openams_ui unit; \"unit\" alone clears it\n");
    printf("  net                      show the stored settings, Wi-Fi state and IP\n");
    printf("  ota status               show the running and other OTA slot\n");
    printf("  ota <url> [force]        download and boot a firmware image (http or https)\n");
    printf("  ui                       what the screen task is doing (flushes, objects, memory, stacks)\n");
    printf("  perf                     timing since the last perf: refreshes, loop, builds, touch latency\n");
    printf("  nav <n>                  go to screen n (0 home, 1 load, 2 alerts, ...), for timing\n");
    printf("  tap <x> <y> [ms]         tap the screen at x,y (held ms, default 60) and print the top layer\n");
    printf("  reboot                   restart the display\n");
    printf("  help                     this list\n");
    fflush(stdout);
}

/* Splits on spaces, keeping a double-quoted token (spaces included) together.
 * The string is modified in place; returns the number of tokens. */
static int split_args(char *s, char **argv, int max)
{
    int argc = 0;
    char *p = s;
    while(*p && argc < max) {
        while(*p == ' ' || *p == '\t') p++;
        if(!*p) break;
        if(*p == '"') {
            p++;
            argv[argc++] = p;
            while(*p && *p != '"') p++;
            if(*p) *p++ = '\0';
        }
        else {
            argv[argc++] = p;
            while(*p && *p != ' ' && *p != '\t') p++;
            if(*p) *p++ = '\0';
        }
    }
    return argc;
}

static void print_settings(void)
{
    net_config_t cfg;
    net_config_load(&cfg);

    printf("ssid: %s\n", cfg.ssid[0] ? cfg.ssid : "(none)");
    printf("password: %s\n", cfg.password[0] ? "set" : "none");
    printf("moonraker: %s\n", cfg.moonraker_url[0] ? cfg.moonraker_url : "(none)");
    printf("unit: %s\n", cfg.bind_unit[0] ? cfg.bind_unit : "(first unit)");
    if(net_wifi_is_up()) {
        printf("wifi: up, ip %s\n", net_wifi_ip());
    }
    else {
        printf("wifi: down\n");
    }
    fflush(stdout);
}

static void report_save(bool ok)
{
    if(ok) {
        printf("saved; type reboot to apply\n");
    }
    else {
        printf("error: could not save to NVS\n");
    }
    fflush(stdout);
}

static void cmd_wifi(int argc, char **argv)
{
    if(argc < 2) {
        printf("usage: wifi <ssid> [password]\n");
        fflush(stdout);
        return;
    }
    net_config_t cfg;
    net_config_load(&cfg);
    strlcpy(cfg.ssid, argv[1], sizeof(cfg.ssid));
    cfg.password[0] = '\0';
    if(argc >= 3) {
        strlcpy(cfg.password, argv[2], sizeof(cfg.password));
    }
    report_save(net_config_save(&cfg));
}

static void cmd_moonraker(int argc, char **argv)
{
    if(argc < 2) {
        printf("usage: moonraker <ws://host:port/path>\n");
        fflush(stdout);
        return;
    }
    if(strncmp(argv[1], "ws://", 5) != 0) {
        printf("error: url must start with ws://\n");
        fflush(stdout);
        return;
    }
    net_config_t cfg;
    net_config_load(&cfg);
    strlcpy(cfg.moonraker_url, argv[1], sizeof(cfg.moonraker_url));
    report_save(net_config_save(&cfg));
}

static void cmd_unit(int argc, char **argv)
{
    net_config_t cfg;
    net_config_load(&cfg);
    cfg.bind_unit[0] = '\0';
    if(argc >= 2) {
        strlcpy(cfg.bind_unit, argv[1], sizeof(cfg.bind_unit));
    }
    report_save(net_config_save(&cfg));
}

static void cmd_ota(int argc, char **argv)
{
    if(argc < 2) {
        printf("usage: ota status | ota <url> [force]\n");
        fflush(stdout);
        return;
    }
    if(!strcmp(argv[1], "status")) {
        ota_cmd_status();
        return;
    }
    bool force = (argc >= 3 && !strcmp(argv[2], "force"));
    ota_cmd_update(argv[1], force);
}

static void run_command(char *line)
{
    char *argv[NETCON_ARG_MAX];
    int argc = split_args(line, argv, NETCON_ARG_MAX);
    if(argc == 0) {
        return;
    }

    if(!strcmp(argv[0], "wifi")) {
        cmd_wifi(argc, argv);
    }
    else if(!strcmp(argv[0], "moonraker")) {
        cmd_moonraker(argc, argv);
    }
    else if(!strcmp(argv[0], "unit")) {
        cmd_unit(argc, argv);
    }
    else if(!strcmp(argv[0], "net")) {
        print_settings();
    }
    else if(!strcmp(argv[0], "ota")) {
        cmd_ota(argc, argv);
    }
    else if(!strcmp(argv[0], "ui")) {
        ui_diag_request();
    }
    else if(!strcmp(argv[0], "perf")) {
        ui_perf_request();
    }
    else if(!strcmp(argv[0], "nav") && argc >= 2) {
        ui_nav_request(atoi(argv[1]));
    }
    else if(!strcmp(argv[0], "tap") && argc >= 3) {
        ui_tap_request(atoi(argv[1]), atoi(argv[2]), argc >= 4 ? atoi(argv[3]) : 60);
    }
    else if(!strcmp(argv[0], "reboot")) {
        printf("rebooting\n");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(50));
        esp_restart();
    }
    else if(!strcmp(argv[0], "help")) {
        print_help();
    }
    else {
        printf("unknown command \"%s\"; type help\n", argv[0]);
        fflush(stdout);
    }
}

static void console_task(void *arg)
{
    (void)arg;

    esp_err_t err = uart_driver_install(CONSOLE_UART, 512, 0, 0, NULL, 0);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
    }
    uart_vfs_dev_use_driver(CONSOLE_UART);

    ESP_LOGI(TAG, "serial setup console ready");
    print_help();

    char line[NETCON_LINE_MAX];
    size_t len = 0;
    bool overflow = false;

    for(;;) {
        char c;
        int n = uart_read_bytes(CONSOLE_UART, &c, 1, pdMS_TO_TICKS(100));
        if(n <= 0) {
            continue;
        }
        if(c == '\r' || c == '\n') {
            if(overflow) {
                overflow = false;
            }
            else if(len > 0) {
                line[len] = '\0';
                run_command(line);
            }
            len = 0;
            continue;
        }
        if(len >= NETCON_LINE_MAX - 1) {
            if(!overflow) {
                printf("line too long, dropped\n");
                fflush(stdout);
                overflow = true;
            }
            continue;
        }
        line[len++] = c;
    }
}

void net_console_start(void)
{
    xTaskCreate(console_task, "netcfg", CONSOLE_STACK, NULL, CONSOLE_PRIORITY, NULL);
}
