#pragma once
/**
 * @file    modbus.h
 * @brief   Modbus RTU 协议栈 — CRC16 + 功能码封装 + 通信统计
 *
 * 支持的 Modbus 功能码:
 *   0x03 — 读保持寄存器   (readDevInfo: 读设备的 V/I/状态)
 *   0x06 — 写单寄存器     (powerOn/powerOff: 开关机控制)
 *   0x0F — 写多寄存器     (setVI: 设置电压+电流)
 *
 * 协议参考:
 *   - 换电柜仓位与充电器通信协议.md  (充电器, 地址 0x00)
 *   - 铁塔BMS协议V3.0.md             (BMS保护板, 地址 0x01)
 *
 * 离线检测: 连续失败 MB_OFFLINE_THRESHOLD 次判定总线离线,
 *   恢复时自动打印日志并清零计数。
 */

#include <Arduino.h>
#include "rs485.h"
#include "app_config.h"

/* ===== 设备实时信息 ===================================================
 * 对应充电器协议寄存器 0x0001~0x0003 (读), 大端序
 *
 * 单位说明:
 *   voltage_dV — 0.1V   (寄存器值 480 → 48.0V, 读取写设置均为×10)
 *   current_cA — 0.01A  (寄存器值 600 → 6.00A, **读取时×100**, 设置时×10)
 *   status     — 16bit 状态字, 低4位为主状态, 高12位为告警位
 */
struct DevInfo {
    uint16_t voltage_dV;   /**< 设备输出电压 (0.1V) */
    uint16_t current_cA;   /**< 设备输出电流 (0.01A, 读取时×100) */
    uint16_t status;       /**< 设备状态字 (低4位状态 + 高12位告警) */
};

/* ===== 状态字解析辅助宏 ============================================== */
// 主状态编码 (低4位): 0=空闲, 1=启动, 2=充电中, 3=充满
//                     0xA/B/C/D=故障 (含过流/过压/过温/短路等)
//                     高12位非零 = 有告警位触发
#define ST_LO4(s)        ((s) & 0x000F)    // 提取主状态 (低4位)
#define ST_HI(s)         ((s) & 0xFFF0)    // 提取告警位 (高12位)
#define ST_IS_IDLE(s)    (ST_LO4(s) == 0x0)
#define ST_IS_BOOTING(s) (ST_LO4(s) == 0x1)
#define ST_IS_CHARGING(s)(ST_LO4(s) == 0x2)
#define ST_IS_FULL(s)    (ST_LO4(s) == 0x3)
/** @brief 判定故障态: 低4位为 0xA~0xD 或 高12位有任一告警位 */
inline bool ST_IS_FAULT(uint16_t s) {
    uint8_t c = ST_LO4(s);
    return (c==0xA||c==0xB||c==0xC||c==0xD) || ST_HI(s);
}

/* ===== Modbus 错误码 =================================================
 * 统一返回 int8_t, 负数为错误, 0=成功
 */
enum ModbusErr : int8_t {
    MB_OK          =  0,   /**< 通信成功 */
    MB_ERR_TIMEOUT = -1,   /**< 接收超时 (从机无应答) */
    MB_ERR_WRITE   = -2,   /**< 发送失败 */
    MB_ERR_LEN     = -3,   /**< 应答帧长度不匹配 */
    MB_ERR_ADDR    = -4,   /**< 从机地址不匹配 */
    MB_ERR_FC      = -5,   /**< 功能码不匹配 */
    MB_ERR_CRC     = -6,   /**< CRC 校验失败 */
    MB_ERR_EXCEPT  = -7,   /**< 从机返回异常帧 (功能码|0x80) */
};

/* ===== 通信统计 (每路独立) =========================================== */
struct ModbusStats {
    uint32_t   tx, rx_ok;           // 总发送 / 成功接收次数
    uint32_t   err_timeout;         // 超时次数
    uint32_t   err_write;           // 发送失败次数
    uint32_t   err_len;             // 帧长度异常次数
    uint32_t   err_addr;            // 地址不匹配次数
    uint32_t   err_fc;              // 功能码不匹配次数
    uint32_t   err_crc;             // CRC 校验失败次数
    uint32_t   err_except;          // 从机异常帧次数
    uint32_t   consecutive_fail;    // 连续失败计数 (离线判定用)
    ModbusErr  last_err;            // 最近一次错误码
};

/* ===== Modbus 客户端 =================================================
 * 封装一次 Modbus RTU 交互的完整流程:
 *   构造报文 → RS485收发 → CRC校验 → 字段解析 → 统计记录
 */
class ModbusClient {
public:
    /**
     * @param bus  RS485 物理层实例
     * @param tag 日志标签 (如 "CHG"/"BMS"), 用于串口和 OLED 输出
     */
    explicit ModbusClient(RS485Bus &bus, const char *tag = "?")
        : _bus(bus), _tag(tag), _stats{} {}

    /* ---- Modbus CRC16 算法 (协议标准) ---- */
    /** @brief 计算 Modbus RTU CRC16 (多项式 0xA001, 预置 0xFFFF, 高低字节交换) */
    static uint16_t crc16(const uint8_t *buf, int len);

    /* ---- 读操作 ---- */
    /** @brief 读设备实时信息 (功能码 0x03, 寄存器 0x0001 起 3 个) */
    bool readDevInfo (uint8_t addr, DevInfo &out);

    /* ---- 写操作 ---- */
    /** @brief 设置输出电压+电流 (功能码 0x0F, 寄存器 0x0001 起 2 个)
     *  @param v_dV 电压 0.1V (如 584 → 58.4V)
     *  @param i_dA 电流 0.1A (如 60 → 6.0A, **注意: 设置时×10, 读取时×100**) */
    bool setVI       (uint8_t addr, uint16_t v_dV, uint16_t i_dA);
    /** @brief 开机 (功能码 0x06, 寄存器 0x0000, 写入 0x00FF) */
    bool powerOn     (uint8_t addr, uint8_t channel = 0);
    /** @brief 关机 (功能码 0x06, 寄存器 0x0000, 写入 0x0000) */
    bool powerOff    (uint8_t addr, uint8_t channel = 0);

    /* ---- 诊断接口 ---- */
    const ModbusStats &stats() const { return _stats; }
    /** @brief 连续失败达到 MB_OFFLINE_THRESHOLD 则返回 true */
    bool offline() const { return _stats.consecutive_fail >= MB_OFFLINE_THRESHOLD; }
    /** @brief 错误码转可读字符串 */
    static const char *errStr(ModbusErr e);

private:
    /**
     * @brief  统一收发调度: 构造报文 → RS485发送 → 接收 → 帧校验
     * @param  expect_len 期望应答帧总长度 (用于校验长度/地址/功能码/CRC)
     * @return 0=成功, 负值=错误码
     */
    ModbusErr doXfer(uint8_t addr, uint8_t fc,
                     const uint8_t *tx, int tx_len,
                     uint8_t *rx, int rx_max, int expect_len);
    /** @brief 记录本次通信结果到统计结构, 并在离线边沿打印日志 */
    void recordStat(ModbusErr e);

    RS485Bus    &_bus;    // RS485 物理层引用
    const char  *_tag;    // 日志标签
    ModbusStats  _stats;  // 通信统计数据
};
