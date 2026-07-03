/**
 * @file    rs485.cpp
 * @brief   RS485 半双工收发实现 — 软件 DE 控制 + 自适应帧尾检测
 *
 * 不使用 ESP-IDF 硬件 RTS 自动翻转模式, 改为 GPIO 手动控制:
 *   1. 发送前 DE=HIGH + 50μs 稳定 → write → flush → 50μs → DE=LOW
 *   2. 接收时逐个字节读取, 字节间停顿超过 8ms (9600 一字节~1ms×8) 判帧尾
 *
 * 调试宏: RS485_LOG_HEXDUMP (默认开启) 在 Serial 输出 HEX dump
 *   定义前可 #define RS485_LOG_HEXDUMP 0 关闭
 */

#include "rs485.h"
#include "app_config.h"

#ifndef RS485_LOG_HEXDUMP
#define RS485_LOG_HEXDUMP 1   // 默认开启 HEX dump
#endif

#if RS485_LOG_HEXDUMP
static void hexdump(const char *dir, int port_num,
                    const uint8_t *buf, int len)
{
    Serial.printf("U%d %s[%d]", port_num, dir, len);
    int n = (len > 32) ? 32 : len;         // 最多打印 32 字节, 防止刷屏
    for (int i = 0; i < n; i++) Serial.printf(" %02X", buf[i]);
    if (len > 32) Serial.print(" ...");
    Serial.println();
}
#else
#define hexdump(...)  do {} while (0)
#endif

void RS485Bus::begin(int rx, int tx, uint32_t baud)
{
    // 初始化串口: 8 数据位, 无校验, 1 停止位 (8N1)
    _ser.begin(baud, SERIAL_8N1, rx, tx);
    _ser.setTimeout(2);                 // 内部超时极短, 实际由本层控制
    pinMode(_de, OUTPUT);
    digitalWrite(_de, LOW);             // 默认接收态 (MAX485 RE=0)
}

int RS485Bus::xfer(const uint8_t *tx, int tx_len,
                   uint8_t *rx, int rx_max,
                   uint32_t timeout_ms)
{
    // 清空硬件 FIFO 和软件缓冲区残留, 避免读到上一帧残留数据
    while (_ser.available()) _ser.read();

    hexdump("TX", 0, tx, tx_len);

    /* ===== 发送阶段 ===== */
    digitalWrite(_de, HIGH);
    delayMicroseconds(50);              // 等待 MAX485 从接收切换到发送 (数据手册 ≤1μs)

    size_t sent = _ser.write(tx, tx_len);
    _ser.flush();                       // 阻塞等待硬件 FIFO 全部移出
    delayMicroseconds(50);              // 等待总线最后一位发送完毕 (9600bps ≈104μs/byte)
    digitalWrite(_de, LOW);             // 切回接收态

    if ((int)sent != tx_len) return RS485_ERR_WRITE;

    /* ===== 接收阶段 ===== */
    uint32_t t0 = millis();            // 首字节计时起点
    int got = 0;
    bool first = true;                 // 尚未收到首字节标志

    while (got < rx_max) {
        if (_ser.available()) {
            rx[got++] = _ser.read();
            t0 = millis();             // 每收到一字节重置帧尾计时
            first = false;
        } else {
            uint32_t dt = millis() - t0;
            // 首字节等待 timeout_ms (默认 500ms, 协议规定 500ms 无应答=超时)
            if (first  && dt > timeout_ms) break;
            // 非首字节: 连续 8ms 无新字节判定帧尾
            // 9600bps 每字节 ~1.04ms, 8ms ≈ 7.7 字节间隔, 远大于正常间隙
            if (!first && dt > 8)         break;
        }
    }
    if (got <= 0) return RS485_ERR_TIMEOUT;
    hexdump("RX", 0, rx, got);
    return got;
}
