/**
 * @file test_ota_policy.c
 * ota_url_ok() and ota_decide(), one case at a time. Plain C, plain assert,
 * no framework, matching the rest of tests/.
 */

/* The default CMAKE_BUILD_TYPE here is Release, which puts -DNDEBUG on the
 * command line and would compile every assert() in this file away: the CTest
 * run would then pass without checking anything. Keep them live. */
#undef NDEBUG

#include <assert.h>
#include <stddef.h>

#include "app/ota_policy.h"

static void test_url_ok(void)
{
    assert(ota_url_ok("http://printer.local/server/files/openams-ui.bin"));
    assert(ota_url_ok("https://10.0.0.5:8080/fw.bin"));
    assert(ota_url_ok("http://printer/"));

    assert(!ota_url_ok(NULL));
    assert(!ota_url_ok(""));
    assert(!ota_url_ok("ftp://printer.local/fw.bin"));
    assert(!ota_url_ok("printer.local/fw.bin"));
    assert(!ota_url_ok("http://"));
    assert(!ota_url_ok("http:///fw.bin"));
    assert(!ota_url_ok("https://"));
}

/* Length limits: a URL right at the edge is fine, one past it is not. Built
 * by hand rather than with <string.h>, which this file otherwise has no
 * need for. */
static void test_url_length(void)
{
    char url[300];
    const char *prefix = "http://h/";
    size_t plen = 0;
    while(prefix[plen] != '\0') {
        url[plen] = prefix[plen];
        plen++;
    }
    size_t i;
    for(i = plen; i < 255; i++) {
        url[i] = 'a';
    }
    url[255] = '\0';
    assert(ota_url_ok(url)); /* 255 chars: under OTA_URL_MAX_LEN (256) */

    for(i = plen; i < 260; i++) {
        url[i] = 'a';
    }
    url[260] = '\0';
    assert(!ota_url_ok(url)); /* 260 chars: over the limit */
}

static void test_decide(void)
{
    /* Different version, same project: accept. */
    assert(ota_decide("1.0.0", "1.1.0", "openams-ui", "openams-ui", false) == OTA_DECIDE_ACCEPT);

    /* Same version, same project, no force: refused. */
    assert(ota_decide("1.0.0", "1.0.0", "openams-ui", "openams-ui", false) == OTA_DECIDE_SAME_VERSION);

    /* Same version, same project, forced: accept. */
    assert(ota_decide("1.0.0", "1.0.0", "openams-ui", "openams-ui", true) == OTA_DECIDE_ACCEPT);

    /* Wrong project always wins, force or not. */
    assert(ota_decide("1.0.0", "1.1.0", "openams-ui", "other-app", false) == OTA_DECIDE_WRONG_PROJECT);
    assert(ota_decide("1.0.0", "1.0.0", "openams-ui", "other-app", true) == OTA_DECIDE_WRONG_PROJECT);
}

int main(void)
{
    test_url_ok();
    test_url_length();
    test_decide();
    return 0;
}
