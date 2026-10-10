/**
 * @file ws_client.c
 * POSIX WebSocket client for the simulator's Moonraker link. See ws_client.h.
 *
 * The Windows cross-build has no POSIX sockets, and the simulator does not
 * need Moonraker there: every function below collapses to "no link".
 */
#include "ws_client.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32

bool ws_connect(const char *url)
{
    (void)url;
    return false;
}

void ws_close(void) {}

bool ws_is_open(void)
{
    return false;
}

bool ws_send_text(const char *text, size_t len)
{
    (void)text;
    (void)len;
    return false;
}

void ws_poll(ws_on_text_fn on_text, void *ctx)
{
    (void)on_text;
    (void)ctx;
}

uint32_t ws_now_ms(void)
{
    return 0;
}

#else /* !_WIN32 */

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* The GUID mixed into the key by RFC 6455 section 4.2.2. */
static const char WS_GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/* A send to a socket the peer already closed must not kill the simulator. */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* Bytes the header read took off the socket past the headers themselves.
 * The server's first frame often arrives in the same segment, so they must
 * be replayed through the frame parser, not dropped. */
static uint8_t s_pushback[1024];
static size_t s_pushback_len;

/* The one connection. */
static int s_fd = -1;
static bool s_open;

/* A byte-at-a-time RFC 6455 frame parser. Byte-at-a-time keeps the state
 * machine readable, and the traffic here is a handful of status objects per
 * second, so its cost is irrelevant. */
#define WS_MSG_MAX 65535

typedef enum {
    FS_HEAD,   /**< the two header bytes */
    FS_EXT,    /**< an extended payload length */
    FS_MASK,   /**< the four mask bytes */
    FS_PAYLOAD
} fs_state_t;

static fs_state_t s_state;
static int s_head_n;
static bool s_fin;
static uint8_t s_opcode;
static bool s_masked;
static uint64_t s_len;
static uint64_t s_got;
static int s_ext_n, s_ext_i, s_mask_n;
static uint8_t s_mask[4];
static bool s_discard;
static int s_msg_opcode; /**< 1 while a text message is being assembled */
static char s_msg[WS_MSG_MAX + 1];
static size_t s_msg_len;
static uint8_t s_ctrl[125];
static size_t s_ctrl_len;
static ws_on_text_fn s_on_text;
static void *s_on_text_ctx;

static void msg_reset(void)
{
    s_msg_len = 0;
    s_msg_opcode = -1;
}

static void parser_reset(void)
{
    s_state = FS_HEAD;
    s_head_n = 0;
    msg_reset();
}

uint32_t ws_now_ms(void)
{
    struct timespec ts;

    if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000));
}

/* ------------------------------------------------------------ io helpers */

static bool set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if(flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

/* Wait for @p events on @p fd. False on timeout or error, including a
 * socket that went readable only because it was closed (POLLERR/POLLHUP). */
static bool wait_io(int fd, short events, int timeout_ms)
{
    struct pollfd p;
    int r;

    p.fd = fd;
    p.events = events;
    p.revents = 0;
    do {
        r = poll(&p, 1, timeout_ms);
    } while(r < 0 && errno == EINTR);

    return r > 0 && (p.revents & events) != 0;
}

static bool send_all(const char *buf, size_t len)
{
    size_t off = 0;

    while(off < len) {
        ssize_t n = send(s_fd, buf + off, len - off, MSG_NOSIGNAL);

        if(n > 0) {
            off += (size_t)n;
        }
        else if(n < 0 && errno == EINTR) {
            continue;
        }
        else if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if(!wait_io(s_fd, POLLOUT, 5000)) return false;
        }
        else {
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------- base64 and SHA-1 */

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t b64_encode(const uint8_t *in, size_t len, char *out)
{
    size_t i = 0, o = 0;

    while(i + 3 <= len) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];

        out[o++] = B64[(v >> 18) & 63u];
        out[o++] = B64[(v >> 12) & 63u];
        out[o++] = B64[(v >> 6) & 63u];
        out[o++] = B64[v & 63u];
        i += 3;
    }
    if(i + 1 == len) {
        uint32_t v = (uint32_t)in[i] << 16;

        out[o++] = B64[(v >> 18) & 63u];
        out[o++] = B64[(v >> 12) & 63u];
        out[o++] = '=';
        out[o++] = '=';
    }
    else if(i + 2 == len) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);

        out[o++] = B64[(v >> 18) & 63u];
        out[o++] = B64[(v >> 12) & 63u];
        out[o++] = B64[(v >> 6) & 63u];
        out[o++] = '=';
    }
    out[o] = '\0';
    return o;
}

typedef struct {
    uint32_t h[5];
    uint64_t bits;
    uint8_t buf[64];
    size_t n;
} sha1_t;

static uint32_t rol32(uint32_t v, int n)
{
    return (v << n) | (v >> (32 - n));
}

static void sha1_block(sha1_t *s, const uint8_t *p)
{
    uint32_t w[80], a, b, c, d, e, f, k, t;
    int i;

    for(i = 0; i < 16; i++) {
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
               ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
    }
    for(i = 16; i < 80; i++) w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    a = s->h[0];
    b = s->h[1];
    c = s->h[2];
    d = s->h[3];
    e = s->h[4];
    for(i = 0; i < 80; i++) {
        if(i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999u;
        }
        else if(i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        }
        else if(i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        }
        else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        t = rol32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol32(b, 30);
        b = a;
        a = t;
    }
    s->h[0] += a;
    s->h[1] += b;
    s->h[2] += c;
    s->h[3] += d;
    s->h[4] += e;
}

static void sha1_update(sha1_t *s, const uint8_t *p, size_t len)
{
    s->bits += (uint64_t)len * 8u;
    while(len > 0) {
        size_t take = 64 - s->n;

        if(take > len) take = len;
        memcpy(s->buf + s->n, p, take);
        s->n += take;
        p += take;
        len -= take;
        if(s->n == 64) {
            sha1_block(s, s->buf);
            s->n = 0;
        }
    }
}

static void sha1(const uint8_t *in, size_t len, uint8_t out[20])
{
    sha1_t s;
    uint8_t pad;
    uint64_t bits;
    int i;

    s.h[0] = 0x67452301u;
    s.h[1] = 0xEFCDAB89u;
    s.h[2] = 0x98BADCFEu;
    s.h[3] = 0x10325476u;
    s.h[4] = 0xC3D2E1F0u;
    s.bits = 0;
    s.n = 0;
    sha1_update(&s, in, len);

    bits = s.bits;
    pad = 0x80;
    sha1_update(&s, &pad, 1);
    pad = 0;
    while(s.n != 56) sha1_update(&s, &pad, 1);
    for(i = 7; i >= 0; i--) {
        uint8_t b = (uint8_t)(bits >> (i * 8));

        sha1_update(&s, &b, 1);
    }
    for(i = 0; i < 5; i++) {
        out[i * 4] = (uint8_t)(s.h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(s.h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(s.h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)s.h[i];
    }
}

/* A handshake nonce does not need to be unguessable, but a fixed key would
 * make every run look alike in logs, so prefer the kernel's entropy source
 * and fall back to rand() only when it is unavailable. */
static void ws_random_bytes(uint8_t *out, size_t len)
{
    static bool seeded;
    int fd = open("/dev/urandom", O_RDONLY);
    size_t i;

    if(fd >= 0) {
        ssize_t n = read(fd, out, len);

        close(fd);
        if(n == (ssize_t)len) return;
    }
    if(!seeded) {
        srand((unsigned)time(NULL) ^ (unsigned)getpid());
        seeded = true;
    }
    for(i = 0; i < len; i++) out[i] = (uint8_t)(rand() & 0xFF);
}

/* ------------------------------------------------------------- url parsing */

static bool parse_url(const char *url, char *host, size_t host_sz, char *port,
                      size_t port_sz, char *path, size_t path_sz)
{
    const char *p, *hend, *colon, *slash;
    size_t n;

    if(strncmp(url, "ws://", 5) != 0) return false;
    p = url + 5;
    slash = strchr(p, '/');
    hend = slash ? slash : p + strlen(p);

    colon = memchr(p, ':', (size_t)(hend - p));
    if(colon) {
        n = (size_t)(colon - p);
        if(n == 0 || n >= host_sz) return false;
        memcpy(host, p, n);
        host[n] = '\0';
        n = (size_t)(hend - colon - 1);
        if(n == 0 || n >= port_sz) return false;
        memcpy(port, colon + 1, n);
        port[n] = '\0';
    }
    else {
        n = (size_t)(hend - p);
        if(n == 0 || n >= host_sz) return false;
        memcpy(host, p, n);
        host[n] = '\0';
        if(port_sz < 3) return false;
        memcpy(port, "80", 3);
    }

    if(slash) {
        n = strlen(slash);
        if(n >= path_sz) return false;
        memcpy(path, slash, n + 1);
    }
    else {
        if(path_sz < 2) return false;
        path[0] = '/';
        path[1] = '\0';
    }
    return true;
}

/* ---------------------------------------------------------------- upgrade */

static const char *find_header(const char *resp, const char *name)
{
    size_t nlen = strlen(name);
    const char *p = strchr(resp, '\n');

    while(p) {
        p++;
        if(strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
            p += nlen + 1;
            while(*p == ' ' || *p == '\t') p++;
            return p;
        }
        p = strchr(p, '\n');
    }
    return NULL;
}

/* Send the Upgrade request and validate the 101. Bytes that arrived after
 * the headers go to s_pushback for the frame parser. */
static bool http_upgrade(const char *host, const char *port, const char *path)
{
    uint8_t nonce[16], digest[20];
    char key[32], expect[32], req[1024], resp[1024];
    char *end;
    uint8_t *joined;
    const char *accept;
    size_t key_len, got = 0;

    ws_random_bytes(nonce, sizeof nonce);
    key_len = b64_encode(nonce, sizeof nonce, key);

    joined = malloc(key_len + sizeof WS_GUID - 1);
    if(!joined) return false;
    memcpy(joined, key, key_len);
    memcpy(joined + key_len, WS_GUID, sizeof WS_GUID - 1);
    sha1(joined, key_len + sizeof WS_GUID - 1, digest);
    free(joined);
    b64_encode(digest, sizeof digest, expect);

    snprintf(req, sizeof req,
             "GET %s HTTP/1.1\r\n"
             "Host: %s:%s\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: %s\r\n"
             "Sec-WebSocket-Version: 13\r\n"
             "\r\n",
             path, host, port, key);
    if(!send_all(req, strlen(req))) return false;

    for(;;) {
        ssize_t n;

        if(!wait_io(s_fd, POLLIN, 5000)) return false;
        n = recv(s_fd, resp + got, sizeof resp - 1 - got, 0);
        if(n > 0) {
            got += (size_t)n;
            resp[got] = '\0';
            end = strstr(resp, "\r\n\r\n");
            if(end) break;
            if(got >= sizeof resp - 1) return false;
        }
        else if(n == 0) {
            return false;
        }
        else if(errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            continue;
        }
        else {
            return false;
        }
    }

    if(strncmp(resp, "HTTP/", 5) != 0) return false;
    {
        const char *sp = strchr(resp, ' ');

        if(!sp) return false;
        if(strncmp(sp + 1, "101 ", 4) != 0 && strncmp(sp + 1, "101\r", 4) != 0) return false;
    }
    accept = find_header(resp, "Sec-WebSocket-Accept");
    if(!accept) return false;
    {
        size_t elen = strlen(expect);

        if(strncmp(accept, expect, elen) != 0) return false;
        if(accept[elen] != '\r' && accept[elen] != '\n') return false;
    }

    end += 4;
    got -= (size_t)(end - resp);
    if(got > sizeof s_pushback) return false;
    memcpy(s_pushback, end, got);
    s_pushback_len = got;
    return true;
}

bool ws_connect(const char *url)
{
    char host[128], port[8], path[256];
    struct addrinfo hints, *res = NULL, *ai;
    int fd = -1;

    ws_close();
    s_pushback_len = 0;
    if(!parse_url(url, host, sizeof host, port, sizeof port, path, sizeof path)) return false;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if(getaddrinfo(host, port, &hints, &res) != 0) return false;

    for(ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if(fd < 0) continue;
#ifdef SO_NOSIGPIPE
        {
            int one = 1;

            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
        }
#endif
        if(!set_nonblocking(fd)) {
            close(fd);
            fd = -1;
            continue;
        }
        if(connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        if(errno == EINPROGRESS && wait_io(fd, POLLOUT, 5000)) {
            int err = 0;
            socklen_t elen = sizeof err;

            if(getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen) == 0 && err == 0) break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if(fd < 0) return false;

    s_fd = fd;
    if(!http_upgrade(host, port, path)) {
        ws_close();
        return false;
    }
    s_open = true;
    return true;
}

void ws_close(void)
{
    if(s_fd >= 0) close(s_fd);
    s_fd = -1;
    s_open = false;
    s_pushback_len = 0;
    parser_reset();
}

bool ws_is_open(void)
{
    return s_open;
}

/* One masked frame. Control frames reuse this with a different opcode. */
static bool send_frame(uint8_t opcode, const char *text, size_t len)
{
    uint8_t hdr[10], mask[4], chunk[1024];
    size_t hlen = 0, i, j;

    if(len > 65535) return false;

    hdr[hlen++] = (uint8_t)(0x80u | opcode);
    if(len < 126) {
        hdr[hlen++] = (uint8_t)(0x80u | (uint8_t)len);
    }
    else {
        hdr[hlen++] = 0x80u | 126u;
        hdr[hlen++] = (uint8_t)(len >> 8);
        hdr[hlen++] = (uint8_t)(len & 0xFFu);
    }
    ws_random_bytes(mask, sizeof mask);
    memcpy(hdr + hlen, mask, sizeof mask);
    hlen += sizeof mask;

    if(!send_all((const char *)hdr, hlen)) return false;
    for(i = 0; i < len; i += j) {
        size_t take = len - i;

        if(take > sizeof chunk) take = sizeof chunk;
        for(j = 0; j < take; j++) chunk[j] = (uint8_t)((uint8_t)text[i + j] ^ mask[(i + j) & 3u]);
        if(!send_all((const char *)chunk, take)) return false;
    }
    return true;
}

bool ws_send_text(const char *text, size_t len)
{
    if(!s_open || s_fd < 0) return false;
    if(!send_frame(0x1, text, len)) {
        ws_close();
        return false;
    }
    return true;
}

/* ------------------------------------------------------------- reader */

static void send_control(uint8_t opcode, const uint8_t *payload, size_t len)
{
    if(!s_open || s_fd < 0) return;
    if(!send_frame(opcode, (const char *)payload, len)) ws_close();
}

static void finish_frame(void)
{
    s_state = FS_HEAD;
    if(s_opcode >= 0x8) {
        if(s_opcode == 0x9) {
            send_control(0xA, s_ctrl, s_ctrl_len); /* pong */
        }
        else if(s_opcode == 0x8) {
            if(!s_discard) send_control(0x8, s_ctrl, s_ctrl_len);
            ws_close();
        }
        return;
    }
    if(s_fin) {
        if(s_msg_opcode == 1) {
            s_msg[s_msg_len] = '\0';
            if(s_on_text) s_on_text(s_on_text_ctx, s_msg, s_msg_len);
        }
        msg_reset();
    }
}

static void begin_payload(void)
{
    s_got = 0;
    s_ctrl_len = 0;
    if(s_opcode >= 0x8) {
        s_discard = s_len > sizeof s_ctrl;
    }
    else if(s_opcode == 0x1) {
        msg_reset();
        s_msg_opcode = 1;
        s_discard = s_len > WS_MSG_MAX;
    }
    else if(s_opcode == 0x0) {
        s_discard = s_msg_opcode != 1 || s_msg_len + s_len > WS_MSG_MAX;
    }
    else {
        s_discard = true; /* binary or unknown: nothing to deliver */
    }
    if(s_discard && s_opcode < 0x8) s_msg_opcode = -1;

    if(s_len == 0) finish_frame();
    else s_state = FS_PAYLOAD;
}

static void after_length(void)
{
    if(s_masked) {
        s_mask_n = 0;
        s_state = FS_MASK;
    }
    else {
        begin_payload();
    }
}

static void payload_byte(uint8_t b)
{
    if(s_masked) b = (uint8_t)(b ^ s_mask[s_got & 3u]);
    if(!s_discard) {
        if(s_opcode >= 0x8) s_ctrl[s_ctrl_len++] = b;
        else s_msg[s_msg_len++] = (char)b;
    }
    if(++s_got == s_len) finish_frame();
}

static void feed_byte(uint8_t b)
{
    switch(s_state) {
    case FS_HEAD:
        if(s_head_n == 0) {
            s_fin = (b & 0x80u) != 0;
            s_opcode = (uint8_t)(b & 0x0Fu);
            s_head_n = 1;
            break;
        }
        s_head_n = 0;
        s_masked = (b & 0x80u) != 0;
        s_len = b & 0x7Fu;
        if(s_len == 126 || s_len == 127) {
            s_ext_n = s_len == 126 ? 2 : 8;
            s_ext_i = 0;
            s_state = FS_EXT;
        }
        else {
            after_length();
        }
        break;

    case FS_EXT:
        if(s_ext_i == 0) s_len = 0;
        s_len = (s_len << 8) | b;
        if(++s_ext_i == s_ext_n) after_length();
        break;

    case FS_MASK:
        s_mask[s_mask_n++] = b;
        if(s_mask_n == 4) begin_payload();
        break;

    case FS_PAYLOAD:
        payload_byte(b);
        break;
    }
}

void ws_poll(ws_on_text_fn on_text, void *ctx)
{
    uint8_t buf[4096];
    size_t i;

    s_on_text = on_text;
    s_on_text_ctx = ctx;

    for(i = 0; i < s_pushback_len && s_open; i++) feed_byte(s_pushback[i]);
    s_pushback_len = 0;

    while(s_open && s_fd >= 0) {
        ssize_t n = recv(s_fd, buf, sizeof buf, 0);

        if(n > 0) {
            ssize_t k;

            /* A close frame (or a failed pong) closes the socket mid-buffer;
             * whatever follows it is not ours to parse. */
            for(k = 0; k < n && s_open; k++) feed_byte(buf[k]);
            continue;
        }
        if(n == 0) {
            ws_close(); /* peer closed the TCP connection */
            return;
        }
        if(errno == EINTR) continue;
        if(errno == EAGAIN || errno == EWOULDBLOCK) return;
        ws_close();
        return;
    }
}

#endif /* _WIN32 */
