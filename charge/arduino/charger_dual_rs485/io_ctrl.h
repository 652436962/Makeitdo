#pragma once
/**
 * @file    io_ctrl.h
 * @brief   板载 IO 控制 — LED 闪烁模式 + 蜂鸣器 + 按键长短按检测
 *
 * LED 闪烁模式 (50ms tick 驱动, 软件 PWM):
 *   OFF      — 熄灭
 *   IDLE     — 1Hz (500ms亮/500ms灭)
 *   CHARGING — 5Hz (100ms亮/100ms灭)
 *   FULL     — 常亮
 *   FAULT    — 10Hz (50ms亮/50ms灭, 急闪)
 *
 * 按键检测:
 *   SHORT — 按下 30ms~3s 释放
 *   LONG  — 按下 >3s 释放 (长按3秒触发软件复位)
 */

#include <Arduino.h>

/** @brief LED 闪烁模式枚举 */
enum LedPattern : uint8_t {
    LED_OFF,        /**< 熄灭 */
    LED_IDLE,       /**< 空闲 — 1Hz 慢闪 */
    LED_CHARGING,   /**< 充电中 — 5Hz 快闪 */
    LED_FULL,       /**< 充满 — 常亮 */
    LED_FAULT,      /**< 故障 — 10Hz 急闪 */
};

/** @brief 按键事件枚举 */
enum KeyEvent : uint8_t {
    KEY_NONE,       /**< 无按键 */
    KEY_SHORT,      /**< 短按 (30ms~3s) */
    KEY_LONG,       /**< 长按 (>3s, 触发复位) */
};

/** @brief 初始化 GPIO: LED/蜂鸣器/按键/电池检测 */
void io_init(void);

/** @brief 设置 LED 闪烁模式 (下次 io_tick 生效) */
void io_set_led(LedPattern p);

/** @brief 控制蜂鸣器: true=鸣响, false=关闭 */
void io_buzzer(bool on);

/** @brief 50ms 周期调用: 根据当前模式翻转 LED GPIO */
void io_tick(void);

/** @brief 按键去抖 + 长短按状态机, 在 loop 中轮询 */
KeyEvent io_key_scan(void);
#pragma once
#include <Arduino.h>

enum LedPattern : uint8_t {
    LED_OFF, LED_IDLE, LED_CHARGING, LED_FULL, LED_FAULT,
};

enum KeyEvent : uint8_t { KEY_NONE, KEY_SHORT, KEY_LONG };

void io_init(void);
void io_set_led(LedPattern p);
void io_buzzer(bool on);
void io_tick(void);            /* 50ms 周期, 在 loop 中调用 */
KeyEvent io_key_scan(void);
