#pragma once
/* ========================================================================
 *  全局引脚 / 串口 / 业务参数配置
 *  对应《ESP32-PicoD4硬件实现方案.md》§3 引脚映射
 * ====================================================================== */

#include "driver/uart.h"
#include "driver/gpio.h"

/* ---------- RS485-A: 充电器 (UART2) ---------- */
#define CHG_UART_PORT     UART_NUM_2
#define CHG_UART_TXD      GPIO_NUM_17
#define CHG_UART_RXD      GPIO_NUM_16
#define CHG_UART_DE       GPIO_NUM_4

/* ---------- RS485-B: 电源 (UART1, GPIO Matrix 重映射) ---------- */
#define PSU_UART_PORT     UART_NUM_1
#define PSU_UART_TXD      GPIO_NUM_27
#define PSU_UART_RXD      GPIO_NUM_26
#define PSU_UART_DE       GPIO_NUM_14

#define RS485_BAUD        9600
#define RS485_TIMEOUT_MS  500

/* ---------- 板载外设 ---------- */
#define USR_LED_GPIO      GPIO_NUM_2
#define USR_KEY_GPIO      GPIO_NUM_0
#define BUZZER_GPIO       GPIO_NUM_25

/* ---------- 电池辅助检测 ---------- */
#define BAT_DET_GPIO      GPIO_NUM_35   /* 光耦输出, 接入=低 */
#define BAT_ADC_CHANNEL   ADC_CHANNEL_6 /* GPIO34 = ADC1_CH6 */

/* ---------- I2C / OLED ---------- */
#define I2C_SDA_GPIO      GPIO_NUM_21
#define I2C_SCL_GPIO      GPIO_NUM_22
#define OLED_I2C_ADDR     0x3C

/* ---------- 业务参数 ---------- */
#define MODBUS_ADDR_CHG   0x00          /* 充电器从机地址, 协议默认 0x00 */
#define MODBUS_ADDR_PSU   0x00          /* 电源模块从机地址, 按现场配置 */

#define POLL_PERIOD_MS    1000          /* 1s 心跳 + 数据轮询 */
#define BAT_INSERT_THRESHOLD_DV  100    /* 10.0V (单位 0.1V), 高于此判定接入 */
