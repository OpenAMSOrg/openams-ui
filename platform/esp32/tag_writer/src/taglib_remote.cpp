// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

// The tag library served by OpenAMS on the printer host (its openams_taglib
// Moonraker component, /server/openams/taglib/...), with the interface of the
// on-device one (taglib.h): each call is one HTTP request to the Moonraker the
// display already talks to. The replies are small, flat JSON, read in place
// with no parse tree: the 4" board has about 10 KB of heap with the tag writer
// open, so a reply is held only as large as it is, a picked tag's 2 KB of hex
// is decoded as it arrives, and no closed connection is left in TIME_WAIT.
#include "taglib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"

namespace taglib {

static const char *TAG = "taglib";

static const int TIMEOUT_MS = 2500;
static const size_t REPLY_MAX = 3072;  // the largest list reply held whole
static const size_t IMAGE_SIZE = 1024;

static char base[64];     // "http://host:port", from the Moonraker URL
static char why[96];      // status_text()
static int known = -1;    // count()
static char picked_uid[9];
static uint8_t *picked;   // the last pick's image, until load() takes it
static char loaded_uid[9];

// The screen and a console command may both ask at once; the buffers above
// are shared.
static StaticSemaphore_t lock_buf;
static SemaphoreHandle_t lock;

struct Hold {
    Hold() { xSemaphoreTake(lock, portMAX_DELAY); }
    ~Hold() { xSemaphoreGive(lock); }
};

void init() { lock = xSemaphoreCreateMutexStatic(&lock_buf); }

void set_service(const char *ws_url) {
    Hold hold;
    base[0] = 0;
    const char *host = ws_url && !strncmp(ws_url, "ws://", 5) ? ws_url + 5 : nullptr;
    if (!host || !*host) {
        snprintf(why, sizeof(why), "%s",
                 ws_url && *ws_url ? "The printer link is not ws://: no tag library"
                                   : "No printer set up (moonraker <url>)");
        return;
    }
    size_t len = strcspn(host, "/");
    if (len + 8 >= sizeof(base)) {
        snprintf(why, sizeof(why), "The printer's address is too long");
        return;
    }
    snprintf(base, sizeof(base), "http://%.*s", (int)len, host);
    why[0] = 0;
}

bool has_partition() { return false; }
int slot_count() { return 0; }
void clear() {}

int count() {
    Hold hold;
    return known;
}

const char *status_text() { return why[0] ? why : "The library is empty"; }

// ----------------------------------------------------------- the reply

static const char *skip(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    return p;
}

// The value of "key" in the reply, or null. In these replies a key's name is
// never also a string value followed by a colon.
static const char *value_of(const char *json, const char *key) {
    size_t n = strlen(key);
    for (const char *p = json; (p = strchr(p, '"')); p++)
        if (!strncmp(p + 1, key, n) && p[n + 1] == '"') {
            const char *v = skip(p + n + 2);
            if (*v == ':')
                return skip(v + 1);
        }
    return nullptr;
}

static int utf8_length(uint8_t lead) {
    return lead < 0x80 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
}

static int hex_digit(char c) {
    return c >= '0' && c <= '9' ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
}

// Copies the JSON string at p (its opening quote) into out, cut to whole
// characters; returns what follows its closing quote, or null.
static const char *read_string(const char *p, char *out, size_t size) {
    if (*p != '"')
        return nullptr;
    size_t n = 0;
    bool full = false;
    for (p++; *p != '"'; ) {
        char c[4];
        int len = 1;
        if (!*p)
            return nullptr;
        if (*p == '\\') {
            p++;
            switch (*p) {
            case 'n': c[0] = '\n'; break;
            case 't': c[0] = '\t'; break;
            case 'r': c[0] = '\r'; break;
            case 'b': c[0] = '\b'; break;
            case 'f': c[0] = '\f'; break;
            case 'u': {
                unsigned cp = 0;
                for (int i = 1; i <= 4; i++) {
                    int d = hex_digit(p[i]);
                    if (d < 0)
                        return nullptr;
                    cp = cp << 4 | d;
                }
                p += 4;
                if (cp < 0x80) {
                    c[0] = (char)cp;
                } else if (cp < 0x800) {
                    c[0] = (char)(0xC0 | cp >> 6);
                    c[1] = (char)(0x80 | (cp & 0x3F));
                    len = 2;
                } else {  // a surrogate half stays one (unprintable) character
                    c[0] = (char)(0xE0 | cp >> 12);
                    c[1] = (char)(0x80 | (cp >> 6 & 0x3F));
                    c[2] = (char)(0x80 | (cp & 0x3F));
                    len = 3;
                }
                break;
            }
            case 0: return nullptr;
            default: c[0] = *p; break;  // \" \\ \/
            }
            p++;
        } else {
            len = utf8_length((uint8_t)*p);
            for (int i = 0; i < len; i++) {
                if (!p[i] || (i && p[i] == '"'))
                    return nullptr;
                c[i] = p[i];
            }
            p += len;
        }
        if (!full && n + len < size) {
            memcpy(out + n, c, len);
            n += len;
        } else {
            full = true;
        }
    }
    out[n] = 0;
    return p + 1;
}

static const char *read_int(const char *p, long *value) {
    char *end;
    *value = strtol(p, &end, 10);
    return end == p ? nullptr : end;
}

// A comma between elements, or the end of the array.
static const char *next_element(const char *p, char close) {
    p = skip(p);
    if (*p == ',')
        return skip(p + 1);
    return *p == close ? p : nullptr;
}

// ----------------------------------------------------------- the request

static void escape_into(char *&out, const char *end, const char *text) {
    for (; *text && out < end - 2; text++) {
        if (*text == '"' || *text == '\\')
            *out++ = '\\';
        *out++ = *text;
    }
}

static void url_encode_into(char *&out, const char *end, const char *text) {
    static const char HEX[] = "0123456789ABCDEF";
    for (; *text && out < end - 3; text++) {
        uint8_t c = (uint8_t)*text;
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            *out++ = (char)c;
        } else {
            *out++ = '%';
            *out++ = HEX[c >> 4];
            *out++ = HEX[c & 15];
        }
    }
}

// One request to Moonraker in flight: GET `path`, or POST it with the JSON
// `body`, sent and its headers read.
struct Call {
    esp_http_client_handle_t client = nullptr;
    int status = -1;
    int64_t length = -1;  // the reply's Content-Length; -1 when it has none
    size_t bytes = 0;     // read so far
    int64_t start = 0;
    unsigned heap = 0, block = 0;  // free when it began: the 4" board's scarce resource
};

// Ends a call. Moonraker closes the connection first (the request says
// Connection: close), and the board waits for that before it closes too: the
// side that closes first keeps the connection in TIME_WAIT for two minutes,
// about 200 bytes of heap per request, which the printer host can spare and
// the 4" board cannot.
static void end_call(Call &call, const char *path) {
    if (!call.client)
        return;
    if (call.status >= 0) {
        int fd = esp_http_client_get_socket(call.client);
        if (fd >= 0) {
            struct timeval wait = {0, 300 * 1000};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
            char c;
            while (recv(fd, &c, 1, 0) > 0) {
            }
        }
        ESP_LOGI(TAG, "%s: HTTP %d, %u bytes in %u ms (heap free %u, largest block %u)", path,
                 call.status, (unsigned)call.bytes,
                 (unsigned)((esp_timer_get_time() - call.start) / 1000), call.heap, call.block);
    }
    esp_http_client_close(call.client);
    esp_http_client_cleanup(call.client);
    call.client = nullptr;
}

// Begins a call. False, with `why` set and the call ended, when there was no
// answer.
static bool begin_call(Call &call, const char *path, const char *body) {
    call.start = esp_timer_get_time();
    call.heap = (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    call.block = (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!base[0])
        return false;  // set_service() said why
    char url[320];
    snprintf(url, sizeof(url), "%s%s", base, path);
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = body ? HTTP_METHOD_POST : HTTP_METHOD_GET;
    cfg.timeout_ms = TIMEOUT_MS;
    cfg.buffer_size = 512;
    cfg.buffer_size_tx = 512;
    cfg.disable_auto_redirect = true;
    call.client = esp_http_client_init(&cfg);
    size_t length = body ? strlen(body) : 0;
    if (call.client) {
        esp_http_client_set_header(call.client, "Connection", "close");
        if (body)
            esp_http_client_set_header(call.client, "Content-Type", "application/json");
        if (esp_http_client_open(call.client, length) == ESP_OK &&
            (!length || esp_http_client_write(call.client, body, length) == (int)length)) {
            call.length = esp_http_client_fetch_headers(call.client);
            call.status = esp_http_client_get_status_code(call.client);
        }
    }
    if (call.status >= 0)
        return true;
    if (call.client)
        snprintf(why, sizeof(why), "The printer did not answer\n(%.56s)", base + 7);
    else
        snprintf(why, sizeof(why), "Not enough memory to ask the printer");
    ESP_LOGW(TAG, "%s%s: %s after %u ms (heap free %u, largest block %u)", base, path,
             call.client ? "no answer" : "no memory",
             (unsigned)((esp_timer_get_time() - call.start) / 1000), call.heap, call.block);
    end_call(call, path);
    return false;
}

// Reads up to `n` bytes of the reply; 0 at its end.
static int read_reply(Call &call, char *buf, size_t n) {
    int got = esp_http_client_read(call.client, buf, (int)n);
    if (got > 0)
        call.bytes += (size_t)got;
    return got;
}

// Reads the rest of the reply into `buf` (NUL-terminated, cut at `size`).
static void read_all(Call &call, char *buf, size_t size) {
    size_t n = 0;
    int got;
    while (n < size - 1 && (got = read_reply(call, buf + n, size - 1 - n)) > 0)
        n += (size_t)got;
    buf[n] = 0;
}

// One whole request: its reply, NUL-terminated, in a heap buffer as large as
// the reply, which the caller frees. Returns the HTTP status, or -1 (with
// `why` set) when there was no answer.
static int request(const char *path, const char *body, char **reply) {
    *reply = nullptr;
    Call call;
    if (!begin_call(call, path, body))
        return -1;
    size_t size = call.length >= 0 && call.length < (int64_t)REPLY_MAX
                      ? (size_t)call.length + 1 : REPLY_MAX;
    char *buf = static_cast<char *>(malloc(size));
    if (!buf) {
        snprintf(why, sizeof(why), "Not enough memory to ask the printer");
        end_call(call, path);
        return -1;
    }
    read_all(call, buf, size);
    int status = call.status;
    end_call(call, path);
    *reply = buf;
    return status;
}

// Moonraker's error reply, into `why`.
static void refused(const char *path, int status, const char *reply) {
    const char *message = value_of(reply, "message");
    if (!message || !read_string(message, why, sizeof(why)))
        snprintf(why, sizeof(why), "The printer refused (HTTP %d)", status);
    // Moonraker's answer for an endpoint nobody registered.
    if (status == 404 && (!message || !strcmp(why, "Not Found")))
        snprintf(why, sizeof(why), "This OpenAMS has no tag library:\nupdate OpenAMS");
    ESP_LOGW(TAG, "%s: HTTP %d: %s", path, status, why);
}

// A request whose reply is a success: its "result", else null with `why` set
// from Moonraker's error, or from the library's state when it is not ready.
static const char *ask(const char *path, const char *body, char **reply) {
    int status = request(path, body, reply);
    if (status < 0)
        return nullptr;
    const char *result = value_of(*reply, "result");
    if (status == 200 && result)
        return result;
    refused(path, status, *reply);
    free(*reply);
    *reply = nullptr;
    return nullptr;
}

// A reply's "state", into `why` when it is not "ready". True when it is.
static bool ready(const char *result) {
    char state[16] = "";
    const char *p = value_of(result, "state");
    if (!p || !read_string(p, state, sizeof(state)) || !strcmp(state, "ready"))
        return true;
    char text[64] = "";
    const char *message = value_of(result, "message");
    if (message && *message == '"' && !read_string(message, text, sizeof(text)))
        text[0] = 0;
    if (!strcmp(state, "updating"))  // the first download is about 200 MB
        snprintf(why, sizeof(why), "%s:\ntry again in a few minutes",
                 text[0] ? text : "OpenAMS is downloading the tag library");
    else if (text[0])
        snprintf(why, sizeof(why), "%s", text);
    else
        snprintf(why, sizeof(why), "The tag library on OpenAMS is %s", state);
    return false;
}

static void note_count(const char *result) {
    long n;
    const char *p = value_of(result, "count");
    if (p && read_int(p, &n))
        known = (int)n;
}

// ----------------------------------------------------------- the library

bool refresh() {
    Hold hold;
    char *reply;
    const char *result = ask("/server/openams/taglib/status", nullptr, &reply);
    if (!result)
        return false;
    note_count(result);
    if (ready(result))
        why[0] = 0;
    free(reply);
    return true;
}

// A list is asked for when the confirm page was left: its pick goes.
static void drop_pick() {
    free(picked);
    picked = nullptr;
}

int list_materials(char out[][NAME_LEN], int max) {
    Hold hold;
    drop_pick();
    char *reply;
    const char *result = ask("/server/openams/taglib/materials", nullptr, &reply);
    if (!result)
        return 0;
    note_count(result);
    bool ok = ready(result);
    int n = 0;
    const char *p = value_of(result, "materials");
    if (p && *p == '[') {
        for (p = skip(p + 1); p && *p == '"' && n < max; p = next_element(p, ']'))
            p = read_string(p, out[n++], NAME_LEN);
    }
    if (ok && !n)
        why[0] = 0;
    free(reply);
    return n;
}

int list_colors(const char *material, char out[][NAME_LEN], uint16_t *counts, uint16_t *fresh,
                int max) {
    Hold hold;
    drop_pick();
    char path[160] = "/server/openams/taglib/colors?material=";
    char *at = path + strlen(path);
    url_encode_into(at, path + sizeof(path), material);
    *at = 0;
    char *reply;
    const char *result = ask(path, nullptr, &reply);
    if (!result)
        return 0;
    int n = 0;
    const char *p = value_of(result, "colors");
    if (p && *p == '[') {
        // [["Black", 13, 12], ...]: name, entries, never cloned
        for (p = skip(p + 1); p && *p == '[' && n < max; p = next_element(p + 1, ']')) {
            long total = 0, unused = 0;
            p = read_string(skip(p + 1), out[n], NAME_LEN);
            if (p)
                p = next_element(p, ']');
            if (p && *p != ']')
                p = read_int(p, &total);
            if (p)
                p = next_element(p, ']');
            if (p && *p != ']')
                p = read_int(p, &unused);
            if (p)
                p = skip(p);
            if (!p || *p != ']')
                break;
            if (counts)
                counts[n] = (uint16_t)total;
            if (fresh)
                fresh[n] = (uint16_t)unused;
            n++;
        }
    }
    free(reply);
    return n;
}

int pick(const char *material, const char *color, char uid_out[9], bool *fresh) {
    Hold hold;
    char path[256] = "/server/openams/taglib/pick?material=";
    char *at = path + strlen(path);
    const char *end = path + sizeof(path);
    url_encode_into(at, end, material);
    if (at < end - 8) {
        memcpy(at, "&color=", 7);
        at += 7;
    }
    url_encode_into(at, end, color);
    *at = 0;
    Call call;
    if (!begin_call(call, path, nullptr))
        return -1;
    if (call.status != 200) {
        char reply[256];
        read_all(call, reply, sizeof(reply));
        end_call(call, path);
        refused(path, call.status, reply);
        return -1;
    }
    drop_pick();
    picked = static_cast<uint8_t *>(malloc(IMAGE_SIZE));
    // {"result": {"uid": "07F0DB02", "fresh": true, "image": "07F0..."}}: what
    // comes before the image is kept, to read the uid and fresh from, and the
    // image's 2,048 hex digits go into it as they arrive, never held whole.
    char head[160] = "";
    char chunk[128];
    size_t head_n = 0, got = 0;
    int high = -1, n;
    bool in_image = false, closed = false, bad = !picked;
    while ((n = read_reply(call, chunk, sizeof(chunk))) > 0) {
        for (int i = 0; i < n && !bad && !closed; i++) {
            char c = chunk[i];
            if (in_image) {
                int d = hex_digit(c);
                if (c == '"')
                    closed = true;
                else if (d < 0 || got == IMAGE_SIZE)
                    bad = true;
                else if (high < 0)
                    high = d;
                else {
                    picked[got++] = (uint8_t)(high << 4 | d);
                    high = -1;
                }
                continue;
            }
            if (head_n == sizeof(head) - 1) {
                bad = true;
                break;
            }
            head[head_n++] = c;
            head[head_n] = 0;
            if (c == '"') {  // the image's opening quote, after "image":
                const char *key = strstr(head, "\"image\"");
                const char *q = key ? skip(key + 7) : nullptr;
                if (q && *q == ':')
                    q = skip(q + 1);
                in_image = q == head + head_n - 1;
            }
        }
    }
    end_call(call, path);
    char uid[9] = "";
    const char *u = value_of(head, "uid");
    const char *f = value_of(head, "fresh");
    bool ok = !bad && closed && got == IMAGE_SIZE && u && read_string(u, uid, sizeof(uid)) &&
              strlen(uid) == 8;
    if (!ok) {
        drop_pick();
        snprintf(why, sizeof(why), "OpenAMS sent a tag that could not be read");
        return -1;
    }
    memcpy(picked_uid, uid, sizeof(picked_uid));
    memcpy(uid_out, uid, 9);
    if (fresh)
        *fresh = f && !strncmp(f, "true", 4);
    return 0;
}

bool load(int slot, uint8_t image[1024]) {
    Hold hold;
    if (slot != 0 || !picked)
        return false;
    memcpy(image, picked, IMAGE_SIZE);
    memcpy(loaded_uid, picked_uid, sizeof(loaded_uid));
    free(picked);
    picked = nullptr;
    return true;
}

void mark_cloned(int slot) {
    Hold hold;
    if (slot != 0 || !loaded_uid[0])
        return;
    char body[32];
    snprintf(body, sizeof(body), "{\"uid\":\"%s\"}", loaded_uid);
    char *reply;
    if (ask("/server/openams/taglib/cloned", body, &reply))
        free(reply);
    else  // the next clone of this colour may then offer the same UID
        ESP_LOGW(TAG, "could not record %s as cloned: %s", loaded_uid, why);
    loaded_uid[0] = 0;
}

bool add(const char *material, const char *color, const uint8_t image[1024], const char **reason) {
    Hold hold;
    const size_t SIZE = 2 * IMAGE_SIZE + 2 * 2 * NAME_LEN + 64;
    char *body = static_cast<char *>(malloc(SIZE));
    if (!body) {
        *reason = "no memory";
        return false;
    }
    char *at = body;
    const char *end = body + SIZE;
    at += snprintf(at, end - at, "{\"material\":\"");
    escape_into(at, end, material);
    at += snprintf(at, end - at, "\",\"color\":\"");
    escape_into(at, end, color);
    at += snprintf(at, end - at, "\",\"image\":\"");
    for (size_t i = 0; i < IMAGE_SIZE; i++)
        at += snprintf(at, end - at, "%02X", image[i]);
    snprintf(at, end - at, "\"}");
    char *reply;
    const char *result = ask("/server/openams/taglib/add", body, &reply);
    free(body);
    if (!result) {
        *reason = why;
        return false;
    }
    note_count(result);
    free(reply);
    return true;
}

}  // namespace taglib
