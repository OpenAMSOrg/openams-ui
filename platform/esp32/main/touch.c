/**
 * @file touch.c
 * XPT2046 sharing the display's SPI2_HOST (HSPI) bus, its own CS33 device
 * at 1 MHz, IRQ36 as a plain GPIO gate.
 *
 * All SPI traffic here happens from lv_timer_handler() (the indev read
 * callback) or from touch_calib_run(), both of which only ever run from the
 * single task that also owns the display flush (lvgl_task on core 1 at
 * runtime; the boot task itself before that task exists). Nothing else
 * touches SPI2_HOST, so there is no cross-task interleaving to guard
 * against and no need for spi_device_acquire_bus()/release_bus().
 */
#include "touch.h"
#include "esp_timer.h"

#include "ui/ui_theme.h" /* UI_SCREEN_W / UI_SCREEN_H: same geometry display.c derives */

#include <string.h>
#include <stdlib.h>

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define PIN_CS  33
#define PIN_IRQ 36

#define TOUCH_SPI_HZ (1 * 1000 * 1000) /* the display keeps 40 MHz on the same bus */

#define CMD_X     0xD0 /* channel 101, 12-bit, differential, PD1:PD0=00 */
#define CMD_Y     0x90 /* channel 001, 12-bit, differential, PD1:PD0=00 */
#define CMD_Z1    0xB0 /* channel 011, 12-bit, differential, PD1:PD0=00 */
#define CMD_Z2    0xC0 /* channel 100, 12-bit, differential, PD1:PD0=00 */
#define CMD_DUMMY 0x80 /* throwaway conversion, PD1:PD0=00 too: extra insurance that
                         * PENIRQ' re-arms once we're done reading this cycle */

/* z1 (raw pressure) is POSITION-DEPENDENT on this panel: a real session log
 * (196 presses, cyd-serial8.log) showed z1 falling from a median ~1160 near
 * the top edge to ~145 near the bottom, with real bottom-edge presses
 * running right down to 120 - i.e. AT the old Z1_THRESHOLD, so they were
 * being rejected outright. R (the touch-resistance ratio computed below)
 * stayed flat at ~1.8-3.6 across every band in that same log, top to
 * bottom, so it - not z1 - is the real accept/reject signal. z1 keeps only
 * a small floor to reject noise/an open circuit. */
#define Z1_NOISE_FLOOR 40
/* R_MAX over that log's 196 real presses was 3.60; this is that x1.5. */
#define R_MAX_PRESS 5.4f
#define RAW_SAMPLES 7     /* samples per axis after discarding the settle sample */
#define SPREAD_MAX 60     /* reject if the middle 5 of 7 samples span more than this,
                            * but ONLY while detecting a fresh press — see
                            * acquire_sample_indev()'s `pressed` parameter. */
#define DRAG_SPREAD_WARN 250 /* purely informational once already pressed: a moving
                              * stylus legitimately spreads samples, so this never
                              * rejects, it just gets a DEBUG breadcrumb */

#define TOUCH_READ_PERIOD_MS 10 /* how often LVGL reads the panel (see touch_init) */
#define PRESS_DEBOUNCE_GOOD 2  /* consecutive good reads before reporting PRESSED */
#define RELEASE_DEBOUNCE_BAD 3 /* consecutive irq-high/pressure reads before reporting
                                * RELEASED; a spread rejection never counts toward this
                                * while already pressed (a drag, not a lift) */

/* Same panel/UI geometry as display.c: the digitizer covers the whole
 * 480x320 panel; the UI occupies a UI_SCREEN_W x UI_SCREEN_H window,
 * centred (offset 0 once the UI fills the whole panel). */
#define PANEL_W 480
#define PANEL_H 320
#define UI_W UI_SCREEN_W
#define UI_H UI_SCREEN_H
#define OFFSET_X (((PANEL_W - UI_W) / 2) > 0 ? ((PANEL_W - UI_W) / 2) : 0)
#define OFFSET_Y (((PANEL_H - UI_H) / 2) > 0 ? ((PANEL_H - UI_H) / 2) : 0)

/* When the last press was recognized (esp_timer us), for the `perf` latency;
 * 0 once it has been taken. */
static volatile int64_t s_press_us;

static const char *TAG = "touch";

static spi_device_handle_t s_spi;

/* Affine raw->panel transform; touch_init() seeds an identity-ish default
 * so an uncalibrated read doesn't produce nonsense coordinates, but nothing
 * should be relying on it — app_main() calibrates before app_init() runs
 * whenever NVS has no valid fit. */
static float s_a = 1.0f, s_b = 0.0f, s_c = 0.0f;
static float s_d = 0.0f, s_e = 1.0f, s_f = 0.0f;

/* z1 histogram: 16 buckets of 256 raw units, logged every 64 samples so the
 * owner can tune Z1_THRESHOLD from real presses. */
#define Z1_HIST_BUCKETS 16
#define Z1_HIST_LOG_EVERY 64
static uint16_t s_z1_hist[Z1_HIST_BUCKETS];
static int s_z1_hist_n;

/* Debounce state for the LVGL indev callback. */
static bool s_pressed_state;
static int s_good_streak, s_bad_streak;
static int32_t s_last_ui_x, s_last_ui_y;
static bool s_last_inside;

/* Mirrors display_set_flip(): applied to the panel-space point AFTER the
 * affine transform, BEFORE the UI offset, so a stored calibration stays
 * valid regardless of flip. */
static bool s_flip;

/* Set by display_power's wake-on-tap handling: the LVGL indev reports
 * RELEASED for the rest of the CURRENT contact (a physical touch already in
 * progress when this was set), so a wake-up tap never also presses a
 * button. Cleared automatically once the contact truly ends (IRQ high). */
static bool s_swallow_active;

/* ------------------------------------------------------------- low level */

static uint16_t xpt_read_once(uint8_t cmd)
{
    uint8_t tx[3] = { cmd, 0x00, 0x00 };
    uint8_t rx[3] = { 0 };

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8 * 3;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    spi_device_polling_transmit(s_spi, &t);

    /* 12-bit result starts one bit into the second byte. */
    return (uint16_t)(((rx[1] << 8) | rx[2]) >> 3) & 0x0FFFU;
}

static int cmp_u16(const void *a, const void *b)
{
    return (int)(*(const uint16_t *)a) - (int)(*(const uint16_t *)b);
}

/* Discards one settle sample after a channel switch, takes RAW_SAMPLES more,
 * and returns both the median and the spread (the middle 5 of 7 samples'
 * span) — the caller decides what to do with the spread. */
static uint16_t read_axis_raw(uint8_t cmd, uint16_t *out_median)
{
    uint16_t samples[RAW_SAMPLES];

    xpt_read_once(cmd); /* discard: mux/reference settling after the channel switch */
    for(int i = 0; i < RAW_SAMPLES; i++) samples[i] = xpt_read_once(cmd);

    qsort(samples, RAW_SAMPLES, sizeof(samples[0]), cmp_u16);
    *out_median = samples[RAW_SAMPLES / 2];
    return samples[RAW_SAMPLES - 2] - samples[1]; /* drop the one lowest/highest */
}

/* As read_axis_raw(), but rejects outright if the spread exceeds spread_max
 * (used where a wide spread should simply fail the read — calibration and
 * initial press detection, never a live drag). */
static bool read_axis_ex(uint8_t cmd, uint16_t *out_median, uint16_t spread_max)
{
    return read_axis_raw(cmd, out_median) <= spread_max;
}

static void z1_histogram_record(uint16_t z1)
{
    int bucket = z1 / (4096 / Z1_HIST_BUCKETS);
    if(bucket >= Z1_HIST_BUCKETS) bucket = Z1_HIST_BUCKETS - 1;
    s_z1_hist[bucket]++;
    s_z1_hist_n++;

    if(s_z1_hist_n >= Z1_HIST_LOG_EVERY) {
        char line[160];
        int off = 0;
        for(int i = 0; i < Z1_HIST_BUCKETS && off < (int)sizeof(line) - 8; i++) {
            off += snprintf(line + off, sizeof(line) - off, "%s%u", i ? "," : "", s_z1_hist[i]);
        }
        ESP_LOGI(TAG, "z1 histogram (bucket width %d, n=%d): [%s]", 4096 / Z1_HIST_BUCKETS,
                 s_z1_hist_n, line);
        memset(s_z1_hist, 0, sizeof(s_z1_hist));
        s_z1_hist_n = 0;
    }
}

typedef struct {
    uint16_t raw_x, raw_y, z1, z2;
    float r;
} touch_raw_t;

/* One full acquisition cycle, with the pressure floor and per-axis spread
 * ceiling as parameters (calibration passes a wider spread_max than
 * indev's initial-press detection does; see acquire_sample_indev()).
 * z1_min is a noise/open-circuit floor only — the real accept/reject
 * signal is R (position-independent; see R_MAX_PRESS above). Returns NULL
 * on success, else a short reject reason string ("irq-high" / "pressure" /
 * "spread-x" / "spread-y" / "resistance") for the DEBUG-level reject log. */
static const char *acquire_sample_ex(touch_raw_t *out, uint16_t z1_min, uint16_t spread_max)
{
    if(gpio_get_level(PIN_IRQ) != 0) return "irq-high";

    xpt_read_once(CMD_Z1); /* discard: settle after whatever channel ran last cycle */
    uint16_t z1 = xpt_read_once(CMD_Z1);
    z1_histogram_record(z1);

    if(z1 < z1_min) return "pressure";

    xpt_read_once(CMD_Z2); /* discard: settle after the Z1->Z2 channel switch */
    uint16_t z2 = xpt_read_once(CMD_Z2);

    uint16_t raw_x;
    if(!read_axis_ex(CMD_X, &raw_x, spread_max)) return "spread-x";

    uint16_t raw_y;
    if(!read_axis_ex(CMD_Y, &raw_y, spread_max)) return "spread-y";

    float r = (float)raw_x * ((float)z2 / (float)z1 - 1.0f) / 4096.0f;
    if(r < 0) r = -r; /* guard against a wiring/ratio quirk giving a negative value */

    out->raw_x = raw_x;
    out->raw_y = raw_y;
    out->z1 = z1;
    out->z2 = z2;
    out->r = r;

    if(r > R_MAX_PRESS) return "resistance";

    /* Return the chip to the IRQ-enabled power-down state: every command
     * above already used PD1:PD0=00, but a final dummy conversion is cheap
     * insurance that PENIRQ' is re-armed for the next poll. */
    xpt_read_once(CMD_DUMMY);
    return NULL;
}

static const char *acquire_sample(touch_raw_t *out)
{
    return acquire_sample_ex(out, Z1_NOISE_FLOOR, SPREAD_MAX);
}

/* Like acquire_sample_ex(), but for the LVGL indev only: while `pressed` is
 * true (a drag already in progress), the per-axis spread never rejects the
 * read — a moving stylus legitimately spreads samples, and that is not the
 * same thing as the pen lifting. The median is used regardless; a spread
 * past DRAG_SPREAD_WARN only gets a DEBUG breadcrumb. While `pressed` is
 * false (looking for a fresh press), SPREAD_MAX applies as normal, so an
 * unstable/moving contact doesn't falsely start a press. `out` is always
 * populated once z1 has cleared the noise floor, even on a later reject,
 * so the caller can log R/position for tuning regardless of outcome. */
static const char *acquire_sample_indev(touch_raw_t *out, bool pressed)
{
    if(gpio_get_level(PIN_IRQ) != 0) return "irq-high";

    xpt_read_once(CMD_Z1); /* discard: settle after whatever channel ran last cycle */
    uint16_t z1 = xpt_read_once(CMD_Z1);
    z1_histogram_record(z1);

    if(z1 < Z1_NOISE_FLOOR) return "pressure";

    xpt_read_once(CMD_Z2); /* discard: settle after the Z1->Z2 channel switch */
    uint16_t z2 = xpt_read_once(CMD_Z2);

    uint16_t raw_x, raw_y;
    uint16_t spread_x = read_axis_raw(CMD_X, &raw_x);
    uint16_t spread_y = read_axis_raw(CMD_Y, &raw_y);

    float r = (float)raw_x * ((float)z2 / (float)z1 - 1.0f) / 4096.0f;
    if(r < 0) r = -r; /* guard against a wiring/ratio quirk giving a negative value */

    out->raw_x = raw_x;
    out->raw_y = raw_y;
    out->z1 = z1;
    out->z2 = z2;
    out->r = r;

    if(!pressed) {
        if(spread_x > SPREAD_MAX) return "spread-x";
        if(spread_y > SPREAD_MAX) return "spread-y";
    }
    else if(spread_x > DRAG_SPREAD_WARN || spread_y > DRAG_SPREAD_WARN) {
        ESP_LOGD(TAG, "large spread while dragging: x=%u y=%u (still using the median)",
                 spread_x, spread_y);
    }

    if(r > R_MAX_PRESS) return "resistance";

    /* Return the chip to the IRQ-enabled power-down state: every command
     * above already used PD1:PD0=00, but a final dummy conversion is cheap
     * insurance that PENIRQ' is re-armed for the next poll. */
    xpt_read_once(CMD_DUMMY);
    return NULL;
}

/* ------------------------------------------------------------- indev cb */

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;

    touch_raw_t s;
    const char *reject = acquire_sample_indev(&s, s_pressed_state);

    if(!reject) {
        s_good_streak++;
        s_bad_streak = 0;

        float px = s_a * s.raw_x + s_b * s.raw_y + s_c;
        float py = s_d * s.raw_x + s_e * s.raw_y + s_f;
        if(s_flip) {
            px = (float)(PANEL_W - 1) - px;
            py = (float)(PANEL_H - 1) - py;
        }
        s_last_ui_x = (int32_t)px - OFFSET_X;
        s_last_ui_y = (int32_t)py - OFFSET_Y;
        s_last_inside =
            (s_last_ui_x >= 0 && s_last_ui_x < UI_W && s_last_ui_y >= 0 && s_last_ui_y < UI_H);

        bool was_pressed = s_pressed_state;
        if(!s_pressed_state && s_good_streak >= PRESS_DEBOUNCE_GOOD) {
            s_pressed_state = true;
            s_press_us = esp_timer_get_time();
        }

        if(s_pressed_state && !was_pressed) {
            ESP_LOGI(TAG,
                     "press: raw=(%u,%u) z1=%u R=%.1f -> panel=(%.0f,%.0f) ui=(%ld,%ld)%s",
                     s.raw_x, s.raw_y, s.z1, (double)s.r, (double)px, (double)py,
                     (long)s_last_ui_x, (long)s_last_ui_y,
                     s_last_inside ? "" : " [outside UI window, ignored]");
        }
    }
    else {
        /* "irq-high" and "pressure" return before touch_raw_t is filled;
         * every other reason (spread-x/y, resistance) has real raw/R data
         * worth surfacing at INFO while the position-independent R gate is
         * being validated across the whole panel. */
        if(strcmp(reject, "irq-high") != 0 && strcmp(reject, "pressure") != 0) {
            float py = s_d * s.raw_x + s_e * s.raw_y + s_f;
            if(s_flip) py = (float)(PANEL_H - 1) - py;
            ESP_LOGI(TAG, "touch rejected: %s raw=(%u,%u) z1=%u R=%.1f panel_y=%.0f", reject,
                     s.raw_x, s.raw_y, s.z1, (double)s.r, (double)py);
        }
        else {
            ESP_LOGD(TAG, "touch rejected: %s", reject);
        }
        /* A spread rejection only ever happens while !s_pressed_state (a
         * moving stylus mid-drag never rejects on spread) — every reject
         * reaching here while already pressed is irq-high, pressure or
         * resistance, all valid release causes. */
        s_bad_streak++;
        s_good_streak = 0;
        if(s_pressed_state && s_bad_streak >= RELEASE_DEBOUNCE_BAD) s_pressed_state = false;
    }

    if(s_swallow_active) {
        if(!touch_is_pressed_raw()) s_swallow_active = false; /* contact ended: swallow window over */
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if(s_pressed_state && s_last_inside) {
        data->point.x = s_last_ui_x;
        data->point.y = s_last_ui_y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* -------------------------------------------------------- calibration API */

bool touch_acquire_raw(uint16_t *raw_x, uint16_t *raw_y, uint16_t *z1)
{
    touch_raw_t s;
    if(acquire_sample(&s) != NULL) return false;
    *raw_x = s.raw_x;
    *raw_y = s.raw_y;
    *z1 = s.z1;
    return true;
}

bool touch_acquire_raw_ex(uint16_t *raw_x, uint16_t *raw_y, uint16_t *z1, uint16_t z1_min,
                          uint16_t spread_max)
{
    touch_raw_t s;
    if(acquire_sample_ex(&s, z1_min, spread_max) != NULL) return false;
    *raw_x = s.raw_x;
    *raw_y = s.raw_y;
    *z1 = s.z1;
    return true;
}

bool touch_is_pressed_raw(void)
{
    return gpio_get_level(PIN_IRQ) == 0;
}

int64_t touch_take_press_us(void)
{
    int64_t t = s_press_us;

    s_press_us = 0;
    return t;
}

void touch_set_affine(const float coef[6])
{
    s_a = coef[0];
    s_b = coef[1];
    s_c = coef[2];
    s_d = coef[3];
    s_e = coef[4];
    s_f = coef[5];
}

void touch_get_affine(float coef[6])
{
    coef[0] = s_a;
    coef[1] = s_b;
    coef[2] = s_c;
    coef[3] = s_d;
    coef[4] = s_e;
    coef[5] = s_f;
}

void touch_set_flip(bool flip)
{
    s_flip = flip;
    ESP_LOGI(TAG, "touch flip: %s", flip ? "on" : "off");
}

void touch_swallow_current_press(void)
{
    s_swallow_active = true;
}

/* ---------------------------------------------------------------- init */

lv_indev_t *touch_init(void)
{
    gpio_config_t irq_cfg;
    memset(&irq_cfg, 0, sizeof(irq_cfg));
    irq_cfg.pin_bit_mask = 1ULL << PIN_IRQ;
    irq_cfg.mode = GPIO_MODE_INPUT;
    /* GPIO36 has no internal pull resistor on the ESP32; this board's touch
     * breakout is expected to supply its own pull-up on IRQ. */
    gpio_config(&irq_cfg);

    /* Shares SPI2_HOST (HSPI) with the display; display_init() must have
     * already called spi_bus_initialize() for that host. */
    spi_device_interface_config_t devcfg;
    memset(&devcfg, 0, sizeof(devcfg));
    devcfg.mode = 0;
    devcfg.clock_speed_hz = TOUCH_SPI_HZ;
    devcfg.spics_io_num = PIN_CS;
    devcfg.queue_size = 1;

    esp_err_t err = spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device (touch) failed: %s", esp_err_to_name(err));
        return NULL;
    }

    ESP_LOGI(TAG, "IRQ idle level: %d (expect 1 = high, untouched)", gpio_get_level(PIN_IRQ));

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    /* 10 ms: a tap is 2 good reads to press and 3 to release (a click fires
     * on release), so about 50 ms to recognize; with no finger down a read
     * is one GPIO check (the IRQ line), so polling this often costs nothing. */
    lv_timer_set_period(lv_indev_get_read_timer(indev), TOUCH_READ_PERIOD_MS);
    lv_indev_set_scroll_limit(indev, 6); /* a stylus doesn't hold as still as a fingertip */

    ESP_LOGI(TAG,
             "touch ready: XPT2046 sharing SPI2_HOST (CS33) at %u Hz, panel %dx%d, UI offset "
             "(%d,%d), read period %dms",
             (unsigned)TOUCH_SPI_HZ, PANEL_W, PANEL_H, OFFSET_X, OFFSET_Y, TOUCH_READ_PERIOD_MS);

    return indev;
}
