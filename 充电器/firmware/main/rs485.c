#include "rs485.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

#define TAG "rs485"
#define UART_BUF_SZ 256

#if RS485_LOG_HEXDUMP
/* 将字节流格式化为 "AA BB CC ..." 打印 */
static void hexdump(const char *dir, uart_port_t port,
                    const uint8_t *buf, int len)
{
    char line[3 * 32 + 8];
    int  n = (len > 32) ? 32 : len;
    int  p = 0;
    for (int i = 0; i < n; i++) {
        p += snprintf(line + p, sizeof(line) - p, "%02X ", buf[i]);
    }
    if (len > 32) snprintf(line + p, sizeof(line) - p, "...");
    ESP_LOGV(TAG, "U%d %s[%d] %s", port, dir, len, line);
}
#else
#define hexdump(...)  do {} while (0)
#endif

void rs485_init(uart_port_t port, int tx_gpio, int rx_gpio, int de_gpio)
{
    const uart_config_t cfg = {
        .baud_rate  = 9600,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(port, UART_BUF_SZ, UART_BUF_SZ,
                                        0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(port, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(port, tx_gpio, rx_gpio,
                                 de_gpio /* RTS=DE/RE */,
                                 UART_PIN_NO_CHANGE));
    /* 关键: 半双工模式下硬件自动翻转 RTS, 无需软件控制 DE */
    ESP_ERROR_CHECK(uart_set_mode(port, UART_MODE_RS485_HALF_DUPLEX));

    ESP_LOGI(TAG, "UART%d init: TX=%d RX=%d DE=%d @9600 8N1",
             port, tx_gpio, rx_gpio, de_gpio);
}

int rs485_xfer(uart_port_t port,
               const uint8_t *tx_buf, int tx_len,
               uint8_t *rx_buf, int rx_max,
               int timeout_ms)
{
    /* 清空残留, 防止上一帧未读完干扰本次接收 */
    uart_flush_input(port);

    hexdump("TX", port, tx_buf, tx_len);

    int sent = uart_write_bytes(port, (const char *)tx_buf, tx_len);
    if (sent != tx_len) {
        ESP_LOGW(TAG, "U%d write %d/%d (drv err)", port, sent, tx_len);
        return RS485_ERR_WRITE;
    }

    /* 等待发送完成, RTS 自动复位, 总线进入接收态 */
    if (uart_wait_tx_done(port, pdMS_TO_TICKS(timeout_ms)) != ESP_OK) {
        ESP_LOGW(TAG, "U%d wait_tx_done timeout", port);
        return RS485_ERR_TIMEOUT;
    }

    int got = uart_read_bytes(port, rx_buf, rx_max,
                              pdMS_TO_TICKS(timeout_ms));
    if (got <= 0) {
        ESP_LOGW(TAG, "U%d rx timeout (%d ms)", port, timeout_ms);
        return RS485_ERR_TIMEOUT;
    }
    hexdump("RX", port, rx_buf, got);
    return got;
}
