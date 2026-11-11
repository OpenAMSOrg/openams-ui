/**
 * @file net_config.h
 * Network settings persisted in NVS (namespace "net").
 *
 * Nothing is compiled in: an empty moonraker_url means demo mode and an
 * empty ssid means no Wi-Fi is started.
 */
#pragma once

#include <stdbool.h>

typedef struct {
    char ssid[33];
    char password[65];
    char moonraker_url[128]; /* "ws://host:7125/websocket"; "" = demo mode */
    char bind_unit[48];      /* "" = the first openams_ui unit */
    char bind_serial[33];    /* the bound unit's serial, learned on connect;
                              * found before the name, so a rename in oams.cfg
                              * keeps the binding ("" = not known yet) */
} net_config_t;

/* Zeroes *out first. Returns true if an SSID is stored. */
bool net_config_load(net_config_t *out);

/* Writes every key; true on success. */
bool net_config_save(const net_config_t *cfg);
