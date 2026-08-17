#include "io_ctrl.h"
#include "app_config.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>

static led_pattern_t s_led_pattern = LED_OFF;

/* ---------- LED 软件定时翻转 ---------- */
static void led_timer_cb(void *arg)
{
    static uint32_t cnt = 0;
    cnt++;
    bool on = false;
    switch (s_led_pattern) {
    case LED_OFF:      on = false; break;
    case LED_IDLE:     on = (cnt / 5) & 1; break;   /* 1Hz  (50ms*10) */
    case LED_CHARGING: on = (cnt / 1) & 1; break;   /* 4Hz */
    case LED_FULL:     on = true;          break;
    case LED_FAULT:    on = cnt & 1;       break;   /* 8Hz */
    }
    gpio_set_level(USR_LED_GPIO, on ? 1 : 0);
}

void io_init(void)
{
    /* LED */
    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << USR_LED_GPIO) | (1ULL << BUZZER_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&led_cfg);

    /* KEY */
    gpio_config_t key_cfg = {
        .pin_bit_mask = (1ULL << USR_KEY_GPIO) | (1ULL << BAT_DET_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&key_cfg);

    /* 50ms 周期定时器驱动 LED 软件 PWM */
    const esp_timer_create_args_t a = {
        .callback = led_timer_cb,
        .name = "led_blink",
    };
    esp_timer_handle_t h;
    esp_timer_create(&a, &h);
    esp_timer_start_periodic(h, 50 * 1000);
}

void io_set_led(led_pattern_t p)   { s_led_pattern = p; }
void io_buzzer(bool on)            { gpio_set_level(BUZZER_GPIO, on ? 1 : 0); }

key_event_t io_key_scan(void)
{
    static int64_t press_us = 0;
    static bool    last     = true;       /* 默认上拉=1 */
    bool now = gpio_get_level(USR_KEY_GPIO);
    int64_t t  = esp_timer_get_time();

    if (last && !now) {                   /* 按下沿 */
        press_us = t;
    } else if (!last && now) {            /* 松开沿 */
        int64_t dt = t - press_us;
        last = now;
        if (dt > 3000000) return KEY_LONG;
        if (dt > 30000)   return KEY_SHORT;
    }
    last = now;
    return KEY_NONE;
}
