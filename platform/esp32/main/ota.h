/**
 * @file ota.h
 * Over-the-air updates: rollback confirmation and the "ota" console command.
 * See docs/ESP32_OTA.md.
 */
#pragma once

#include <stdbool.h>

/* Call once, right before the UI task starts. Notes the time and whether the
 * running image is a freshly-updated one still pending verification (a
 * no-op call otherwise). */
void ota_rollback_init(void);

/* Call periodically from the UI loop with whether Wi-Fi credentials are
 * configured and whether Wi-Fi is currently connected. Marks the running
 * image valid (cancelling rollback) once Wi-Fi has connected, or, when no
 * Wi-Fi is configured, 60 s after ota_rollback_init(). A no-op once
 * confirmed, or if the image was never pending verification. */
void ota_rollback_poll(bool wifi_configured, bool wifi_up);

/* "ota status": prints the running slot and version, and the other slot's
 * state and version (if any), to stdout. */
void ota_cmd_status(void);

/* "ota <url> [force]": downloads the image at url into the inactive slot,
 * refuses it unless its project name matches and (absent force) its version
 * differs from the running image, then sets it as the boot partition and
 * reboots. Prints progress and errors to stdout; never returns on success. */
void ota_cmd_update(const char *url, bool force);
