#pragma once
/**
 * @file    rs485.h
 * @brief   RS485 半双工物理层抽象 — HardwareSerial + GPIO 控制 DE
 *
 * 封装 Arduino HardwareSerial 的 RS485 收发流程:
 *   1. 清空 RX 缓冲区残留
 *   2. DE 拉高 (发送态) → write + flush → DE 拉低 (接收态)
 *   3. 自适应超时接收: 首字节等待 timeout_ms, 字节间停顿 >8ms 判帧尾
 *
 * 错误返回:
 *   RS485_ERR_WRITE    — 发送字节数不匹配
 *   RS485_ERR_TIMEOUT  — 超时未收到任何数据
 *   >0                 — 成功接收字节数
 */

#include <Arduino.h>
#include <HardwareSerial.h>

/** @brief 发送失败 (实际写入字节数与请求不符) */
#define RS485_ERR_WRITE    (-1)
/** @brief 接收超时 (timeout_ms 内未收到任何字节) */
#define RS485_ERR_TIMEOUT  (-2)

class RS485Bus {
public:
    /**
     * @param serial  Arduino HardwareSerial 实例引用 (如 Serial1/Serial2)
     * @param de_pin  RS485 收发使能 GPIO (接 MAX485 DE+RE 并联端)
     */
    RS485Bus(HardwareSerial &serial, int de_pin)
        : _ser(serial), _de(de_pin) {}

    /**
     * @brief 初始化串口 + 配置 DE 引脚为输出低 (默认接收态)
     * @param rx   接收引脚 (GPIO 矩阵可重映射)
     * @param tx   发送引脚
     * @param baud 波特率, 默认 9600 (协议强制)
     */
    void begin(int rx, int tx, uint32_t baud = 9600);

    /**
     * @brief  阻塞式半双工收发一帧
     * @param  tx         发送报文缓冲区
     * @param  tx_len     发送字节数
     * @param  rx         接收缓冲区 (调用方分配)
     * @param  rx_max     接收缓冲区容量
     * @param  timeout_ms 首字节等待超时 (ms)
     * @return >0          成功接收字节数
     *         RS485_ERR_WRITE   发送失败
     *         RS485_ERR_TIMEOUT 接收超时
     */
    int xfer(const uint8_t *tx, int tx_len,
             uint8_t *rx, int rx_max,
             uint32_t timeout_ms);

    /** @brief 获取底层 HardwareSerial 引用 (调试用) */
    HardwareSerial &raw() { return _ser; }

private:
    HardwareSerial &_ser;   // Arduino 串口实例
    int             _de;    // DE/RE 控制 GPIO
};
#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>

#define RS485_ERR_WRITE    (-1)
#define RS485_ERR_TIMEOUT  (-2)

class RS485Bus {
public:
    RS485Bus(HardwareSerial &serial, int de_pin)
        : _ser(serial), _de(de_pin) {}

    void begin(int rx, int tx, uint32_t baud = 9600);

    /**
     * 阻塞式发送 + 接收一帧。
     * @return >0 接收字节数 / RS485_ERR_WRITE / RS485_ERR_TIMEOUT
     */
    int xfer(const uint8_t *tx, int tx_len,
             uint8_t *rx, int rx_max,
             uint32_t timeout_ms);

    HardwareSerial &raw() { return _ser; }

private:
    HardwareSerial &_ser;
    int             _de;
};
