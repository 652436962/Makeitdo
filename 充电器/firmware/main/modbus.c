#include "modbus.h"
#include "rs485.h"
#include "app_config.h"
#include "esp_log.h"
#include <string.h>

#define TAG "modbus"

/* ============ CRC16 (协议原文移植, 注意末尾高低字节交换) ============ */
uint16_t modbus_crc16(const uint8_t *buf, int len)
{
    uint16_t c = 0xFFFF;
    for (int k = 0; k < len; k++) {
        uint16_t b = buf[k];
        for (int i = 0; i < 8; i++) {
            c = ((b ^ c) & 1) ? (c >> 1) ^ 0xA001 : (c >> 1);
            b >>= 1;
        }
    }
    return (uint16_t)((c << 8) | (c >> 8));
}

static int append_crc(uint8_t *buf, int len)
{
    uint16_t crc = modbus_crc16(buf, len);
    buf[len]     = (uint8_t)(crc >> 8);
    buf[len + 1] = (uint8_t)(crc & 0xFF);
    return len + 2;
}

static bool verify_crc(const uint8_t *buf, int len)
{
    if (len < 4) return false;
    uint16_t crc_calc = modbus_crc16(buf, len - 2);
    uint16_t crc_recv = ((uint16_t)buf[len - 2] << 8) | buf[len - 1];
    return crc_calc == crc_recv;
}

/* ============ 统计 / 诊断 ============ */
/* UART_NUM_1 / UART_NUM_2, 使用 port 作为下标 */
static modbus_stats_t s_stats[UART_NUM_MAX];

const modbus_stats_t *modbus_get_stats(uart_port_t port)
{
    if (port >= UART_NUM_MAX) return NULL;
    return &s_stats[port];
}

bool modbus_is_offline(uart_port_t port)
{
    if (port >= UART_NUM_MAX) return true;
    return s_stats[port].consecutive_fail >= MB_OFFLINE_THRESHOLD;
}

const char *modbus_err_str(modbus_err_t e)
{
    switch (e) {
    case MB_OK:            return "OK";
    case MB_ERR_TIMEOUT:   return "TIMEOUT";
    case MB_ERR_WRITE:     return "WRITE";
    case MB_ERR_LEN:       return "LEN";
    case MB_ERR_ADDR:      return "ADDR";
    case MB_ERR_FC:        return "FC";
    case MB_ERR_CRC:       return "CRC";
    case MB_ERR_EXCEPTION: return "EXCEPTION";
    }
    return "?";
}

/* 记录一次交互结果, 更新统计 + 连续失败计数 + 离线事件边沿日志 */
static void stats_record(uart_port_t port, modbus_err_t err)
{
    if (port >= UART_NUM_MAX) return;
    modbus_stats_t *s = &s_stats[port];
    s->tx++;
    s->last_err = err;

    if (err == MB_OK) {
        if (s->consecutive_fail >= MB_OFFLINE_THRESHOLD) {
            ESP_LOGW(TAG, "[U%d] bus recovered (was offline %lu)",
                     port, (unsigned long)s->consecutive_fail);
        }
        s->rx_ok++;
        s->consecutive_fail = 0;
        return;
    }

    switch (err) {
    case MB_ERR_TIMEOUT:   s->err_timeout++;   break;
    case MB_ERR_WRITE:     s->err_write++;     break;
    case MB_ERR_LEN:       s->err_len++;       break;
    case MB_ERR_ADDR:      s->err_addr++;      break;
    case MB_ERR_FC:        s->err_fc++;        break;
    case MB_ERR_CRC:       s->err_crc++;       break;
    case MB_ERR_EXCEPTION: s->err_exception++; break;
    default: break;
    }
    s->consecutive_fail++;

    /* 离线事件边沿触发, 仅在第 N 次失败时打印一次 */
    if (s->consecutive_fail == MB_OFFLINE_THRESHOLD) {
        ESP_LOGE(TAG, "[U%d] *** BUS OFFLINE *** last_err=%s",
                 port, modbus_err_str(err));
    }
}

/* ============ 统一的底层帧交互: 解析长度/地址/功能码/CRC/异常帧 ============ */
static modbus_err_t do_xfer(uart_port_t port, uint8_t addr, uint8_t fc,
                            const uint8_t *tx, int tx_len,
                            uint8_t *rx, int rx_max, int expect_len)
{
    int n = rs485_xfer(port, tx, tx_len, rx, rx_max, RS485_TIMEOUT_MS);
    if (n == RS485_ERR_WRITE)   return MB_ERR_WRITE;
    if (n == RS485_ERR_TIMEOUT) return MB_ERR_TIMEOUT;
    if (n <= 0)                 return MB_ERR_TIMEOUT;

    /* Modbus 异常帧: 地址 + (fc|0x80) + 异常码 + CRC2, 共 5 字节 */
    if (n == 5 && rx[0] == addr && rx[1] == (fc | 0x80) && verify_crc(rx, n)) {
        ESP_LOGW(TAG, "[U%d-%02X] exception fc=0x%02X code=0x%02X",
                 port, addr, fc, rx[2]);
        return MB_ERR_EXCEPTION;
    }

    if (n != expect_len)   return MB_ERR_LEN;
    if (rx[0] != addr)     return MB_ERR_ADDR;
    if (rx[1] != fc)       return MB_ERR_FC;
    if (!verify_crc(rx, n))return MB_ERR_CRC;
    return MB_OK;
}

/* =====================================================================
 * 1. 读电压/电流/状态  (功能码 0x03, 读 0x0001 起 3 个寄存器)
 * =================================================================== */
bool modbus_read_dev_info(uart_port_t port, uint8_t addr, dev_info_t *out)
{
    uint8_t tx[8];
    tx[0] = addr;
    tx[1] = 0x03;
    tx[2] = 0x00; tx[3] = 0x01;
    tx[4] = 0x00; tx[5] = 0x03;
    int tx_len = append_crc(tx, 6);

    uint8_t rx[16];
    modbus_err_t err = do_xfer(port, addr, 0x03, tx, tx_len, rx, sizeof(rx), 11);
    if (err == MB_OK) {
        /* 附加校验: 字节计数位必须为 6 */
        if (rx[2] != 0x06) err = MB_ERR_LEN;
    }
    stats_record(port, err);

    if (err != MB_OK) {
        ESP_LOGW(TAG, "[U%d-%02X] read fail: %s (cfail=%lu)",
                 port, addr, modbus_err_str(err),
                 (unsigned long)s_stats[port].consecutive_fail);
        return false;
    }

    out->voltage_dV = ((uint16_t)rx[3] << 8) | rx[4];
    out->current_cA = ((uint16_t)rx[5] << 8) | rx[6];
    out->status     = ((uint16_t)rx[7] << 8) | rx[8];
    return true;
}

/* =====================================================================
 * 2. 设置电压电流  (功能码 0x0F, 写 0x0001 起 2 个寄存器)
 * =================================================================== */
bool modbus_set_voltage_current(uart_port_t port, uint8_t addr,
                                uint16_t v_dV, uint16_t i_dA)
{
    uint8_t tx[13];
    tx[0]  = addr;
    tx[1]  = 0x0F;
    tx[2]  = 0x00; tx[3] = 0x01;
    tx[4]  = 0x00; tx[5] = 0x02;
    tx[6]  = 0x04;
    tx[7]  = (uint8_t)(v_dV >> 8); tx[8]  = (uint8_t)(v_dV & 0xFF);
    tx[9]  = (uint8_t)(i_dA >> 8); tx[10] = (uint8_t)(i_dA & 0xFF);
    int tx_len = append_crc(tx, 11);

    uint8_t rx[8];
    modbus_err_t err = do_xfer(port, addr, 0x0F, tx, tx_len, rx, sizeof(rx), 8);
    stats_record(port, err);

    if (err != MB_OK) {
        ESP_LOGW(TAG, "[U%d-%02X] set V/I fail: %s",
                 port, addr, modbus_err_str(err));
        return false;
    }
    ESP_LOGI(TAG, "[U%d-%02X] set V=%.1fV I=%.1fA OK",
             port, addr, v_dV / 10.0f, i_dA / 10.0f);
    return true;
}

/* =====================================================================
 * 3. 开关机  (功能码 0x06, 寄存器 0x0000)
 * =================================================================== */
static bool send_06(uart_port_t port, uint8_t addr, uint16_t value)
{
    uint8_t tx[8];
    tx[0] = addr;
    tx[1] = 0x06;
    tx[2] = 0x00; tx[3] = 0x00;
    tx[4] = (uint8_t)(value >> 8); tx[5] = (uint8_t)(value & 0xFF);
    int tx_len = append_crc(tx, 6);

    uint8_t rx[8];
    modbus_err_t err = do_xfer(port, addr, 0x06, tx, tx_len, rx, sizeof(rx), 8);
    stats_record(port, err);

    if (err != MB_OK) {
        ESP_LOGW(TAG, "[U%d-%02X] 0x06 val=0x%04X fail: %s",
                 port, addr, value, modbus_err_str(err));
        return false;
    }
    return true;
}

bool modbus_power_on(uart_port_t port, uint8_t addr, uint8_t channel)
{
    ESP_LOGI(TAG, "[U%d-%02X] POWER ON  ch=%d", port, addr, channel);
    return send_06(port, addr, channel ? 0x10FF : 0x00FF);
}

bool modbus_power_off(uart_port_t port, uint8_t addr, uint8_t channel)
{
    ESP_LOGI(TAG, "[U%d-%02X] POWER OFF ch=%d", port, addr, channel);
    return send_06(port, addr, channel ? 0x1000 : 0x0000);
}
