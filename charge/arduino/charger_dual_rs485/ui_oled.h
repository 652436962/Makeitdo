#pragma once
/**
 * @file    ui_oled.h
 * @brief   SSD1306 OLED 128×64 显示接口 — 基于 U8g2 库
 *
 * 显示内容 (5行布局):
 *   第1行: CHG 电压/电流/状态低4位 (充电器实时遥测)
 *   第2行: PSU 电压/电流/状态字   (BMS/电源实时遥测)
 *   第3行: 充电累计时长 HH:MM:SS
 *   第4行: 电压百分比进度条 (按电池类型分段映射)
 *   第5行: 状态机名称 + 百分比
 *
 * 刷新策略: 全缓冲模式 (u8g2.sendBuffer), 2Hz 刷新
 * I2C: 硬件 I2C, 400kHz, SDA=21/SCL=22, 地址 0x3C
 */

#include <Arduino.h>
#include "charger_fsm.h"

/** @brief 初始化 I2C + U8g2, 显示开机引导画面 */
void ui_init(void);

/**
 * @brief 刷新 OLED 显示 (全缓冲)
 * @param fsm      充电状态机引用, 获取 chgInfo/psuInfo/stateName
 * @param tick_sec 累计充电秒数
 */
void ui_draw(const ChargerFsm &fsm, uint32_t tick_sec);
#pragma once
#include <Arduino.h>
#include "charger_fsm.h"

void ui_init(void);
void ui_draw(const ChargerFsm &fsm, uint32_t tick_sec);
