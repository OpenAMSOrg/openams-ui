/**
 * @file net_wifi.h
 * Wi-Fi station bring-up from a net_config_t.
 */
#pragma once

#include <stdbool.h>

#include "net_config.h"

/* Does nothing when cfg->ssid is empty (demo mode). */
void net_wifi_start(const net_config_t *cfg);

/* True once an IPv4 address is held. */
bool net_wifi_is_up(void);

/* "" while down. */
const char *net_wifi_ip(void);
