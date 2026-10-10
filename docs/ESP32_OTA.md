# ESP32 display: over-the-air updates

Two equal app slots (`ota_0`, `ota_1`) fill the 4 MB flash; there is no
factory app (`platform/esp32/partitions.csv`). `nvs` keeps its original
offset and size, so stored Wi-Fi and Moonraker settings survive an update.

## Updating

From the serial setup console (see `net_console.c`):

```
ota status
ota <url> [force]
```

- `ota status` prints the running slot and its version, and the other slot's
  state (new / pending verify / valid / invalid / aborted) and version.
- `ota <url>` downloads the image at `url` into the inactive slot, logs
  progress every 10%, checks the image header's project name matches this
  firmware's, and refuses an image whose version string equals the running
  one. Add `force` to install it anyway. On success it sets the boot
  partition and reboots.

Both `http://` and `https://` URLs work. A printer host on the LAN usually
serves the update over plain HTTP (e.g. Moonraker's `/server/files/...`);
that needs `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y` (set in `sdkconfig.defaults`),
since `esp_https_ota` refuses a plain-HTTP URL by default. For `https://`,
`esp_http_client_config_t.crt_bundle_attach` is set to
`esp_crt_bundle_attach`, so the server certificate is verified against
ESP-IDF's bundled trusted root CAs (`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE`,
on by default) — a self-signed or private-CA host will fail unless its CA is
added to that bundle.

## Rollback

App rollback is enabled (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`). A freshly
updated image boots in the bootloader's "pending verify" state; if it resets
again before confirming itself valid, the bootloader falls back to the
previous slot. `platform/esp32/main/ota.c` confirms the running image once
the UI is up **and** either Wi-Fi has connected once, or — if no Wi-Fi is
configured — 60 s have passed since the UI started. The decision logic
(`ota_url_ok`, `ota_decide`) lives in the portable, unit-tested
`src/app/ota_policy.c`.

## Moving from the old factory layout

Boards already flashed with the previous single-`factory`-app partition
table need one serial flash of the new partition table (`idf.py flash`, or
just `idf.py partition-table-flash`) before OTA works, since the partition
table itself changed. That flash is part of hardware qualification, not
something OTA can do for itself.

## CI

`tools/ci/esp32_size_check.py` also fails the build when the app image
exceeds 90% of one OTA slot's size (read from `partitions.csv`), and prints
the headroom.
