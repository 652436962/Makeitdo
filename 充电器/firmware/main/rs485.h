#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 将下发/应答 HEX dump 打印到日志 (VERBOSE 级).
 * 生产环境设置 0 关闭以降低串口负载 */
#ifndef RS485_LOG_HEXDUMP
#define RS485_LOG_HEXDUMP     1
#endif

/* rs485_xfer 返回值语义 */
#define RS485_ERR_WRITE       (-1)
#define RS485_ERR_TIMEOUT     (-2)

/**
 * 初始化指定 UART 为 RS485 半双工模式 (9600 8N1)。
 * DE/RE 共用一根 GPIO, 由硬件 RTS 自动翻转。
 */
void rs485_init(uart_port_t port, int tx_gpio, int rx_gpio, int de_gpio);

/**
 * 阻塞式发送 + 接收一帧。
 * @return  >0 实际接收字节数;
 *          RS485_ERR_WRITE   驱动写入失败;
 *          RS485_ERR_TIMEOUT 在 timeout_ms 内未收到任何字节。
 */
int rs485_xfer(uart_port_t port,
               const uint8_t *tx_buf, int tx_len,
               uint8_t *rx_buf, int rx_max,
               int timeout_ms);

#ifdef __cplusplus
}
#endif
