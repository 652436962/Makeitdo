/**
 * @file    io_ctrl.cpp
 * @brief   IO 控制实现 — LED PWM 模拟 + 按键去抖状态机
 *
 * LED 无硬件 PWM, 由 50ms tick 累计计数模拟不同频率闪烁:
 *   s_cnt 每 tick 加 1, 50ms×20 = 1s 一个周期。
 *   - 1Hz  : (s_cnt/10)&1 → 500ms on / 500ms off
 *   - 5Hz  : (s_cnt/2)&1  → 100ms on / 100ms off
 *   - 10Hz : s_cnt&1       →  50ms on /  50ms off
 *
 * 按键去抖:
 *   状态机跟踪 GPIO0 电平变化, 记录按下时长:
 *   - <30ms:  忽略 (去抖)
 *   - 30ms~3s: 短按
 *   - >3s:     长按 (触发 ESP.restart)
 */

#include "io_ctrl.h"
#include "app_config.h"

static volatile LedPattern s_pattern = LED_OFF;  // 当前 LED 模式 (中断安全)
static uint32_t s_cnt = 0;                       // 50ms tick 累计计数器

void io_init(void)
{
    // GPIO 初始化: LED 和蜂鸣器为输出低, 按键和电池检测为输入上拉
    pinMode(USR_LED_GPIO, OUTPUT);
    pinMode(BUZZER_GPIO,  OUTPUT);
    pinMode(USR_KEY_GPIO, INPUT_PULLUP);          // GPIO0, 按下为低
    pinMode(BAT_DET_GPIO, INPUT_PULLUP);          // GPIO35, 电池接入检测
    digitalWrite(USR_LED_GPIO, LOW);              // 默认灭
    digitalWrite(BUZZER_GPIO,  LOW);              // 默认静音
}

void io_set_led(LedPattern p) { s_pattern = p; }
void io_buzzer(bool on)        { digitalWrite(BUZZER_GPIO, on ? HIGH : LOW); }

void io_tick(void)
{
    s_cnt++;                     // 50ms 一次, 支持最大计数 ~6.8年不溢出
    bool on = false;
    switch (s_pattern) {
    case LED_OFF:      on = false;                  break;  // 始终灭
    case LED_IDLE:     on = (s_cnt / 10) & 1;       break;  // 1Hz   (500ms on/off)
    case LED_CHARGING: on = (s_cnt / 2)  & 1;       break;  // 5Hz   (100ms on/off)
    case LED_FULL:     on = true;                   break;  // 常亮
    case LED_FAULT:    on = s_cnt & 1;              break;  // 10Hz  (50ms on/off)
    }
    digitalWrite(USR_LED_GPIO, on ? HIGH : LOW);
}

KeyEvent io_key_scan(void)
{
    static uint32_t press_ms = 0;                  // 按下时刻 (ms)
    static bool last = true;                       // 上一次 GPIO 电平 (true=未按)
    bool now = digitalRead(USR_KEY_GPIO);          // 当前电平 (LOW=按下)

    // 下降沿: 记录按下时刻
    if (last && !now) {
        press_ms = millis();
    }
    // 上升沿: 计算按下时长, 判定短按/长按
    else if (!last && now) {
        uint32_t dt = millis() - press_ms;
        last = now;
        if (dt > 3000) return KEY_LONG;            // >3s 长按
        if (dt > 30)   return KEY_SHORT;           // >30ms 短按 (去抖阈)
    }
    last = now;
    return KEY_NONE;
}
#include "io_ctrl.h"
#include "app_config.h"

static volatile LedPattern s_pattern = LED_OFF;
static uint32_t s_cnt = 0;

void io_init(void)
{
    pinMode(USR_LED_GPIO, OUTPUT);
    pinMode(BUZZER_GPIO,  OUTPUT);
    pinMode(USR_KEY_GPIO, INPUT_PULLUP);
    pinMode(BAT_DET_GPIO, INPUT_PULLUP);
    digitalWrite(USR_LED_GPIO, LOW);
    digitalWrite(BUZZER_GPIO,  LOW);
}

void io_set_led(LedPattern p) { s_pattern = p; }
void io_buzzer(bool on)        { digitalWrite(BUZZER_GPIO, on ? HIGH : LOW); }

void io_tick(void)
{
    s_cnt++;
    bool on = false;
    switch (s_pattern) {
    case LED_OFF:      on = false;            break;
    case LED_IDLE:     on = (s_cnt / 10) & 1; break;  /* 1Hz @ 50ms tick */
    case LED_CHARGING: on = (s_cnt / 2) & 1;  break;  /* ~5Hz */
    case LED_FULL:     on = true;             break;
    case LED_FAULT:    on = s_cnt & 1;        break;  /* 10Hz */
    }
    digitalWrite(USR_LED_GPIO, on ? HIGH : LOW);
}

KeyEvent io_key_scan(void)
{
    static uint32_t press_ms = 0;
    static bool last = true;
    bool now = digitalRead(USR_KEY_GPIO);
    uint32_t t = millis();

    if (last && !now)              { press_ms = t; }
    else if (!last && now) {
        uint32_t dt = t - press_ms;
        last = now;
        if (dt > 3000) return KEY_LONG;
        if (dt > 30)   return KEY_SHORT;
    }
    last = now;
    return KEY_NONE;
}
