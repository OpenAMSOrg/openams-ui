/**
 * @file ws_client.h
 * A minimal RFC 6455 WebSocket client for the simulator's Moonraker link.
 *
 * This is the POSIX transport under `moonraker_client.h`: it owns the TCP
 * socket, performs the HTTP Upgrade handshake, frames outgoing text and
 * reassembles incoming text frames. Everything that decides *what* to say
 * lives in the client; this file only moves bytes.
 *
 * Deliberately small: one connection per program (file-static state, like the
 * client above), `ws://` only, no TLS, no extensions, no compression, and a
 * non-blocking socket so the caller's loop never stalls. It is simulator
 * tooling and never ships to the firmware, which uses `esp_websocket_client`.
 */

#ifndef WS_CLIENT_H
#define WS_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** One complete text message, already reassembled and NUL-terminated. */
typedef void (*ws_on_text_fn)(void *ctx, const char *text, size_t len);

/**
 * Connect to @p url ("ws://host[:port]/path") and complete the WebSocket
 * handshake. Blocking, but bounded: the TCP connect, the Upgrade request and
 * the response each get a few seconds. False when the URL cannot be parsed,
 * the connection fails, or the server does not answer with a valid 101.
 */
bool ws_connect(const char *url);

/** Close the socket (idempotent). The client must be `ws_connect`ed again. */
void ws_close(void);

/** True while the socket is up and the handshake completed. */
bool ws_is_open(void);

/**
 * Send one masked text frame. False when the socket is down, the payload
 * exceeds 64 KB, or the send fails (the socket is then closed).
 */
bool ws_send_text(const char *text, size_t len);

/**
 * Read whatever has arrived without blocking, reassemble frames, deliver
 * complete text messages to @p on_text, answer pings with pongs and handle
 * close. A dropped or closed socket leaves `ws_is_open()` false.
 */
void ws_poll(ws_on_text_fn on_text, void *ctx);

/** A monotonic millisecond clock for the link's timers (never wraps in
 *  practice; the client's arithmetic is wrap-safe anyway). */
uint32_t ws_now_ms(void);

#endif /* WS_CLIENT_H */
