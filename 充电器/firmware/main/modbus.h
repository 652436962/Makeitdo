#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 协议读寄存器返回的设备实时信息 */
typedef struct {
    uint16_t voltage_dV;   /* 单位 0.1V    (协议: 电压×10) */
    uint16_t current_cA;   /* 单位 0.01A   (协议: 读电流×100) */
    uint16_t status;       /* 状态字, bit 定义见协议 §3 */
} dev_info_t;

/* 细分的协议层错误码 */
typedef enum {
    MB_OK            = 0,
    MB_ERR_TIMEOUT   = -1,   /* 总线超时 / 对端无响应 */
    MB_ERR_WRITE     = -2,   /* 驱动发送失败 */
    MB_ERR_LEN       = -3,   /* 应答长度不符 */
    MB_ERR_ADDR      = -4,   /* 从机地址不符 */
    MB_ERR_FC        = -5,   /* 功能码不符 */
    MB_ERR_CRC       = -6,   /* CRC 校验失败 */
    MB_ERR_EXCEPTION = -7,   /* 从机返回异常帧 (fc | 0x80) */
} modbus_err_t;

/* 每路 UART 的累计通信统计 */
typedef struct {
    uint32_t tx;             /* 发送帧数 */
    uint32_t rx_ok;          /* 正确应答数 */
    uint32_t err_timeout;
    uint32_t err_write;
    uint32_t err_len;
    uint32_t err_addr;
    uint32_t err_fc;
    uint32_t err_crc;
    uint32_t err_exception;
    uint32_t consecutive_fail;  /* 连续失败计数, 成功后清零 */
    modbus_err_t last_err;      /* 最近一次错误码 */
} modbus_stats_t;

/* 连续失败超过该阈值判定为总线离线 */
#define MB_OFFLINE_THRESHOLD  5

/* 状态字低 4 位 */
#define ST_LO4(s)        ((s) & 0x000F)
#define ST_HI(s)         ((s) & 0xFFF0)
#define ST_IS_IDLE(s)    (ST_LO4(s) == 0x0)
#define ST_IS_BOOTING(s) (ST_LO4(s) == 0x1)
#define ST_IS_CHARGING(s)(ST_LO4(s) == 0x2)
#define ST_IS_FULL(s)    (ST_LO4(s) == 0x3)
#define ST_IS_FAULT(s)   ({ uint8_t _c = ST_LO4(s); \
                             (_c==0xA||_c==0xB||_c==0xC||_c==0xD) || ST_HI(s); })

/* ----- Modbus CRC16 (协议 §CRC16 算法源码) ----- */
uint16_t modbus_crc16(const uint8_t *buf, int len);

/* ----- 业务接口 (port 选择 UART1=电源, UART2=充电器) ----- */
bool modbus_read_dev_info(uart_port_t port, uint8_t addr, dev_info_t *out);

/* 设置电压电流, v_dV: 电压×10, i_dA: 电流×10 (设置时×10, 注意与读取×100 不同) */
bool modbus_set_voltage_current(uart_port_t port, uint8_t addr,
                                uint16_t v_dV, uint16_t i_dA);

/* channel: 0=通道1, 1=通道2 */
bool modbus_power_on (uart_port_t port, uint8_t addr, uint8_t channel);
bool modbus_power_off(uart_port_t port, uint8_t addr, uint8_t channel);

/* ----- 诊断接口 ----- */
const modbus_stats_t *modbus_get_stats(uart_port_t port);
bool                  modbus_is_offline(uart_port_t port);
const char           *modbus_err_str(modbus_err_t e);

#ifdef __cplusplus
}
#endif
