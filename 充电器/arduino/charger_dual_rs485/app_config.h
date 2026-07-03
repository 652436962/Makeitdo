#pragma once
/* ========================================================================
 *  全局引脚 / 串口 / 业务参数 — 单一配置入口
 *
 *  硬件基线: ESP32-PicoD4 开源迷你开发板
 *  与 ESP32-PicoD4硬件实现方案.md §3 引脚映射严格一致
 *
 *  RS485 总线拓扑:
 *   换电柜(Modbus主机) ──1路RS485总线──┬── 充电器模块 (从机地址 0x00)
 *                                     └── BMS保护板   (从机地址 0x01)
 *
 *  串口分配:
 *   Serial2  → 充电器    (GPIO 17/16/4,  内置 Serial2 默认引脚)
 *   Serial1  → BMS/电源  (GPIO 27/26/14, 重映射以避开 Flash GPIO6~11)
 * ====================================================================== */
#include <Arduino.h>

/* ========== RS485 — 充电器 (Serial2, 默认引脚) ====================== */
// 功能码: 03(读寄存器) / 06(写单寄存器) / 0F(写多寄存器)
// 从机地址: 0x00 (广播模式下可接收, 协议中未明确固定)
#define CHG_UART_TXD   17   // 发送 (接 MAX485 DI)
#define CHG_UART_RXD   16   // 接收 (接 MAX485 RO)
#define CHG_UART_DE    4    // 收发使能 (接 MAX485 DE+RE 并联)

/* ========== RS485 — BMS/电源 (Serial1, GPIO 矩阵重映射) ============= */
// Serial1 默认 GPIO 9/10 与 Pico-D4 内置 Flash 冲突,
// 通过 HardwareSerial::begin(rx,tx) 重定向到以下空闲引脚
#define PSU_UART_TXD   27   // 发送 (GPIO27, 远离 Flash 区域)
#define PSU_UART_RXD   26   // 接收 (GPIO26, ADC2_CH9 也可复用)
#define PSU_UART_DE    14   // 收发使能

/* ========== RS485 总线参数 =========================================== */
#define RS485_BAUD          9600     // 波特率 (协议强制)
#define RS485_TIMEOUT_MS    500      // 帧间超时: 主站发出查询后最多等 500ms

/* ========== 板载外设 GPIO 映射 ======================================= */
// GPIO2   — 板载蓝色 LED (低电平有效, 多数 Pico-D4 板)
// GPIO0   — BOOT 按键 (按下为低, 内部上拉)
// GPIO25  — 蜂鸣器 (NPN 三极管驱动, 高电平鸣响)
#define USR_LED_GPIO    2
#define USR_KEY_GPIO    0
#define BUZZER_GPIO     25

/* ========== 电池辅助检测 ============================================= */
// GPIO35  — 数字检测 (用于快速判断电池物理接入/拔出)
// GPIO34  — ADC1_CH6 (用于电池电压模拟采样, 可选)
#define BAT_DET_GPIO    35
#define BAT_ADC_GPIO    34

/* ========== I2C / OLED (SSD1306 128x64) ============================== */
// 硬件 I2C 0 号控制器, 400kHz 快速模式
// OLED 地址 0x3C (<<1 后为 0x78, u8g2 内部处理)
#define I2C_SDA_GPIO    21
#define I2C_SCL_GPIO    22
#define OLED_I2C_ADDR   0x3C

/* ========== 业务参数 ================================================= */
#define MODBUS_ADDR_CHG          0x00   // 充电器从机地址
#define MODBUS_ADDR_BMS          0x01   // BMS 保护板从机地址
#define MODBUS_ADDR_PSU          0x00   // 电源模块从机地址 (复用充电器地址)
#define POLL_PERIOD_MS           1000   // Modbus 轮询周期 (同时作为心跳保活)

// 电池插入判定: 电压 ≥ 100 dV (10.0V) 认为电池已接入
// 低于此值认为电池已拔出, 状态机退回 IDLE
#define BAT_INSERT_THRESHOLD_DV  100

// 连续通信失败达到此阈值判定总线离线, 触发 FAULT
// 离线恢复: 连续成功≥1次自动清除离线标志
#define MB_OFFLINE_THRESHOLD     5
#pragma once
/* ========================================================================
 *  全局引脚 / 串口 / 业务参数
 *  与硬件方案 §3 引脚映射一致
 * ====================================================================== */
#include <Arduino.h>

/* ---------- RS485-A: 充电器 (Serial2) ---------- */
#define CHG_UART_TXD   17
#define CHG_UART_RXD   16
#define CHG_UART_DE    4

/* ---------- RS485-B: 电源 (Serial1, 重映射) ---------- */
#define PSU_UART_TXD   27
#define PSU_UART_RXD   26
#define PSU_UART_DE    14

#define RS485_BAUD          9600
#define RS485_TIMEOUT_MS    500

/* ---------- 板载外设 ---------- */
#define USR_LED_GPIO    2
#define USR_KEY_GPIO    0
#define BUZZER_GPIO     25

/* ---------- 电池辅助检测 ---------- */
#define BAT_DET_GPIO    35
#define BAT_ADC_GPIO    34   /* GPIO34 = ADC1_CH6 */

/* ---------- I2C / OLED ---------- */
#define I2C_SDA_GPIO    21
#define I2C_SCL_GPIO    22
#define OLED_I2C_ADDR   0x3C

/* ---------- 业务参数 ---------- */
#define MODBUS_ADDR_CHG          0x00
#define MODBUS_ADDR_PSU          0x00
#define POLL_PERIOD_MS           1000
#define BAT_INSERT_THRESHOLD_DV  100   /* 10.0V */
#define MB_OFFLINE_THRESHOLD     5
