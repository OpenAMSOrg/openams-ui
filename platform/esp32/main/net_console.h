/**
 * @file net_console.h
 * Serial setup console on UART0: stores Wi-Fi/Moonraker settings in NVS.
 */
#pragma once

/* Starts the "netcfg" task. Safe to call once, after NVS is initialized. */
void net_console_start(void);
