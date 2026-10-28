/**
 * @file backlight.c
 * The panel backlight (backlight.h): LEDC PWM, gamma-corrected, with fades.
 */
#include "backlight.h"
#include "display.h"

#include <math.h>

#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "backlight";

/* Backlight LEDC (PWM): 5 kHz, 10-bit duty. */
#define BL_LEDC_TIMER LEDC_TIMER_0
#define BL_LEDC_MODE  LEDC_LOW_SPEED_MODE
#define BL_LEDC_CH    LEDC_CHANNEL_0
#define BL_LEDC_HZ    5000
#define BL_LEDC_RES   LEDC_TIMER_10_BIT
#define BL_LEDC_MAX   1023

/* Perceptual gamma (~2.2) so 10-30% duty is visibly distinct instead of
 * crowding near zero — a linear duty-vs-percent map makes the low end of
 * the slider feel like an on/off switch. */
static uint32_t backlight_gamma_duty(uint8_t pct)
{
    if(pct == 0) return 0;
    if(pct > 100) pct = 100;
    float f = (float)pct / 100.0f;
    float g = powf(f, 2.2f);
    uint32_t duty = (uint32_t)(g * BL_LEDC_MAX + 0.5f);
    return duty > BL_LEDC_MAX ? BL_LEDC_MAX : duty;
}

void backlight_init(int gpio)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = BL_LEDC_MODE,
        .duty_resolution = BL_LEDC_RES,
        .timer_num = BL_LEDC_TIMER,
        .freq_hz = BL_LEDC_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_cfg);

    ledc_channel_config_t ch_cfg = {
        .gpio_num = gpio,
        .speed_mode = BL_LEDC_MODE,
        .channel = BL_LEDC_CH,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BL_LEDC_TIMER,
        .duty = 0, /* stay off until display_set_brightness() is called explicitly */
        .hpoint = 0,
    };
    ledc_channel_config(&ch_cfg);

    ledc_fade_func_install(0);
}

void display_set_brightness(uint8_t pct, uint16_t fade_ms)
{
    if(pct > 100) pct = 100;
    uint32_t duty = backlight_gamma_duty(pct);

    if(fade_ms == 0) {
        ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CH, duty);
        ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CH);
    }
    else {
        ledc_set_fade_with_time(BL_LEDC_MODE, BL_LEDC_CH, duty, fade_ms);
        ledc_fade_start(BL_LEDC_MODE, BL_LEDC_CH, LEDC_FADE_NO_WAIT);
    }

    ESP_LOGI(TAG, "backlight brightness %u%% (duty %u/%u%s)", pct, (unsigned)duty, BL_LEDC_MAX,
             fade_ms ? ", fading" : "");
}
