/**
 * @file ota_policy.c
 * See ota_policy.h.
 */
#include "ota_policy.h"

#include <string.h>

/* Long enough for "http(s)://host:port/very/long/moonraker/path", short
 * enough to keep a bad or truncated console line from being taken seriously. */
#define OTA_URL_MAX_LEN 256
#define OTA_HOST_MAX_LEN 128

bool ota_url_ok(const char *url)
{
    if(!url) return false;

    size_t len = strlen(url);
    if(len == 0 || len >= OTA_URL_MAX_LEN) return false;

    const char *host;
    if(strncmp(url, "http://", 7) == 0) {
        host = url + 7;
    }
    else if(strncmp(url, "https://", 8) == 0) {
        host = url + 8;
    }
    else {
        return false;
    }

    if(*host == '\0' || *host == '/') return false;

    size_t host_len = 0;
    while(host[host_len] != '\0' && host[host_len] != '/' && host[host_len] != '?') {
        host_len++;
    }
    if(host_len == 0 || host_len > OTA_HOST_MAX_LEN) return false;

    return true;
}

int ota_decide(const char *running_ver, const char *new_ver,
               const char *running_project, const char *new_project, bool force)
{
    if(strcmp(running_project, new_project) != 0) {
        return OTA_DECIDE_WRONG_PROJECT;
    }
    if(!force && strcmp(running_ver, new_ver) == 0) {
        return OTA_DECIDE_SAME_VERSION;
    }
    return OTA_DECIDE_ACCEPT;
}
