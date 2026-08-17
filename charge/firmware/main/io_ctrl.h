#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_OFF,
    LED_IDLE,      /* 慢闪 1Hz */
    LED_CHARGING,  /* 快闪 4Hz */
    LED_FULL,      /* 常亮     */
    LED_FAULT,     /* 急闪 8Hz */
} led_pattern_t;

void io_init(void);
void io_set_led(led_pattern_t p);
void io_buzzer(bool on);

/* 按键扫描结果 (供其他任务轮询) */
typedef enum {
    KEY_NONE,
    KEY_SHORT,
    KEY_LONG,
} key_event_t;
key_event_t io_key_scan(void);

#ifdef __cplusplus
}
#endif
