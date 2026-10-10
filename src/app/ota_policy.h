/**
 * @file ota_policy.h
 * Pure-C decisions for the ESP32 OTA path (platform/esp32/main/ota.c):
 * whether a console-supplied URL is worth opening at all, and whether a
 * downloaded image's header should be accepted. No I/O, no ESP-IDF headers,
 * so this is unit-testable on the host.
 */
#ifndef OTA_POLICY_H
#define OTA_POLICY_H

#include <stdbool.h>

/** True for a non-empty "http://" or "https://" URL with a host and a
 * bounded overall length. Does not resolve or open anything. */
bool ota_url_ok(const char *url);

enum {
    OTA_DECIDE_ACCEPT = 0,       /* proceed with the update */
    OTA_DECIDE_SAME_VERSION,     /* new_ver == running_ver and !force */
    OTA_DECIDE_WRONG_PROJECT,    /* new_project != running_project */
};

/** Decides whether a downloaded image should be installed, given the
 * version and project-name strings of the running image and the new one
 * (as read from each image's esp_app_desc_t). A project-name mismatch is
 * refused outright; a matching version is refused unless @p force is set. */
int ota_decide(const char *running_ver, const char *new_ver,
               const char *running_project, const char *new_project, bool force);

#endif /* OTA_POLICY_H */
