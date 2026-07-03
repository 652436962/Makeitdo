/**
 * @file    modbus.cpp
 * @brief   Modbus RTU 协议实现 — CRC16 / 功能码封装 / 统计 / 离线检测
 *
 * 报文格式 (所有功能码共用):
 *   请求: [地址 1B][功能码 1B][数据 N B][CRC16 2B]
 *   应答: [地址 1B][功能码 1B][数据 M B][CRC16 2B]
 *   异常: [地址 1B][功能码|0x80 1B][异常码 1B][CRC16 2B]
 *
 * 数据均为大端序 (Big-Endian), 先发高位字节。
 */

#include "modbus.h"
#include "app_config.h"

/* ========== CRC16 算法 (Modbus RTU 标准) ==============================
 * 多项式: 0xA001 (x^16 + x^15 + x^13 + 1)
 * 预置值: 0xFFFF
 * 输出:   高低字节交换 (先发 CRC 高字节, 再发低字节)
 * 此算法与铁塔BMS协议V3.0 §3.4.2 和 充电器协议 完全一致
 */
uint16_t ModbusClient::crc16(const uint8_t *buf, int len)
{
    uint16_t c = 0xFFFF;                      // 步骤1: 预置 0xFFFF
    for (int k = 0; k < len; k++) {
        uint16_t b = buf[k];                  // 步骤2: 字节与 CRC 低8位异或
        for (int i = 0; i < 8; i++) {
            // 步骤3: 右移1位, LSB=1 则异或 0xA001
            c = ((b ^ c) & 1) ? (c >> 1) ^ 0xA001 : (c >> 1);
            b >>= 1;                          // 步骤4: 单字节循环8次
        }
    }
    // 步骤5: 高低字节交换后输出
    return (uint16_t)((c << 8) | (c >> 8));
}

/** @brief 在报文末尾追加 CRC16 (2字节, 高字节在前) */
static int append_crc(uint8_t *buf, int len)
{
    uint16_t crc = ModbusClient::crc16(buf, len);
    buf[len]     = (uint8_t)(crc >> 8);       // CRC 高字节
    buf[len + 1] = (uint8_t)(crc & 0xFF);     // CRC 低字节
    return len + 2;
}

/** @brief 验证接收报文的 CRC16 */
static bool verify_crc(const uint8_t *buf, int len)
{
    if (len < 4) return false;               // 最少: 地址+功能码+CRC2 = 4字节
    uint16_t calc = ModbusClient::crc16(buf, len - 2);
    uint16_t recv = ((uint16_t)buf[len - 2] << 8) | buf[len - 1];
    return calc == recv;
}

/* ========== 错误码字符串映射 ========================================== */
const char *ModbusClient::errStr(ModbusErr e)
{
    switch (e) {
    case MB_OK:          return "OK";
    case MB_ERR_TIMEOUT: return "TIMEOUT";
    case MB_ERR_WRITE:   return "WRITE";
    case MB_ERR_LEN:     return "LEN";
    case MB_ERR_ADDR:    return "ADDR";
    case MB_ERR_FC:      return "FC";
    case MB_ERR_CRC:     return "CRC";
    case MB_ERR_EXCEPT:  return "EXCEPT";
    }
    return "?";
}

/* ========== 通信统计记录 ==============================================
 * 每次通信后调用, 更新统计字段并检测离线边沿:
 *   - 成功: 清零连续失败计数, 若之前为离线状态则打印恢复日志
 *   - 失败: 递增对应错误类型计数 + 连续失败计数,
 *           达到 MB_OFFLINE_THRESHOLD 时打印离线告警
 */
void ModbusClient::recordStat(ModbusErr e)
{
    _stats.tx++;                              // 每次调用计数一次发送
    _stats.last_err = e;
    if (e == MB_OK) {
        // 从离线恢复到正常, 打印日志
        if (_stats.consecutive_fail >= MB_OFFLINE_THRESHOLD) {
            Serial.printf("[%s] bus recovered (was offline %lu)\n",
                          _tag, (unsigned long)_stats.consecutive_fail);
        }
        _stats.rx_ok++;
        _stats.consecutive_fail = 0;          // 清零连续失败计数
        return;
    }
    // 失败: 按错误类型分别计数
    switch (e) {
    case MB_ERR_TIMEOUT: _stats.err_timeout++; break;
    case MB_ERR_WRITE:   _stats.err_write++;   break;
    case MB_ERR_LEN:     _stats.err_len++;     break;
    case MB_ERR_ADDR:    _stats.err_addr++;    break;
    case MB_ERR_FC:      _stats.err_fc++;      break;
    case MB_ERR_CRC:     _stats.err_crc++;     break;
    case MB_ERR_EXCEPT:  _stats.err_except++;  break;
    default: break;
    }
    _stats.consecutive_fail++;
    // 连续失败刚好达到阈值的边沿, 打印离线告警 (仅一次, 不重复)
    if (_stats.consecutive_fail == MB_OFFLINE_THRESHOLD) {
        Serial.printf("[%s] *** BUS OFFLINE *** last=%s\n", _tag, errStr(e));
    }
}

/* ========== 统一收发调度 ==============================================
 * 流程: RS485 xfer → 长度校验 → 异常帧检测 → 地址/功能码/CRC校验
 *
 * expect_len 参考值:
 *   功能码 0x03: 读3个寄存器 → 11 (地址+fc+6+CRC=1+1+1+6+2)
 *   功能码 0x06: 写单寄存器 → 8  (回声)
 *   功能码 0x0F: 写多寄存器 → 8
 *   异常帧:     任何功能码  → 5  (地址+fc|0x80+code+CRC)
 */
ModbusErr ModbusClient::doXfer(uint8_t addr, uint8_t fc,
                               const uint8_t *tx, int tx_len,
                               uint8_t *rx, int rx_max, int expect_len)
{
    int n = _bus.xfer(tx, tx_len, rx, rx_max, RS485_TIMEOUT_MS);
    if (n == RS485_ERR_WRITE)   return MB_ERR_WRITE;
    if (n == RS485_ERR_TIMEOUT || n <= 0) return MB_ERR_TIMEOUT;

    // 异常帧识别: 标准 Modbus 异常帧固定5字节
    //   [地址][功能码|0x80][异常码][CRC16]
    if (n == 5 && rx[0] == addr && rx[1] == (fc | 0x80) && verify_crc(rx, n)) {
        Serial.printf("[%s] exception fc=0x%02X code=0x%02X\n", _tag, fc, rx[2]);
        return MB_ERR_EXCEPT;
    }
    // 逐项校验应答帧
    if (n != expect_len)    return MB_ERR_LEN;    // 长度不符
    if (rx[0] != addr)      return MB_ERR_ADDR;   // 从地址不对
    if (rx[1] != fc)        return MB_ERR_FC;     // 功能码回显错误
    if (!verify_crc(rx, n)) return MB_ERR_CRC;    // CRC 校验失败
    return MB_OK;
}

/* ========== 1. 读设备信息 (功能码 0x03) ================================
 * 请求: [addr][03][0001][0003][CRC]
 *       起始寄存器 0x0001, 读取 3 个 (电压 0x0001, 电流 0x0002, 状态 0x0003)
 * 应答: [addr][03][06][VH VL][IH IL][SH SL][CRC]     (11 字节)
 *       数据字节数=6 (3个寄存器×2字节)
 */
bool ModbusClient::readDevInfo(uint8_t addr, DevInfo &out)
{
    uint8_t tx[8];
    tx[0] = addr; tx[1] = 0x03;              // 地址 + 功能码
    tx[2] = 0x00; tx[3] = 0x01;              // 起始寄存器 0x0001
    tx[4] = 0x00; tx[5] = 0x03;              // 寄存器数量 3
    int tx_len = append_crc(tx, 6);           // 追加 CRC

    uint8_t rx[16];
    ModbusErr e = doXfer(addr, 0x03, tx, tx_len, rx, sizeof(rx), 11);
    // 额外校验: 应答数据字节数必须为 6 (3个寄存器 × 2字节)
    if (e == MB_OK && rx[2] != 0x06) e = MB_ERR_LEN;
    recordStat(e);
    if (e != MB_OK) {
        Serial.printf("[%s-%02X] read fail: %s (cfail=%lu)\n",
                      _tag, addr, errStr(e),
                      (unsigned long)_stats.consecutive_fail);
        return false;
    }
    // 提取大端序字段
    out.voltage_dV = ((uint16_t)rx[3] << 8) | rx[4];   // 电压 (0.1V)
    out.current_cA = ((uint16_t)rx[5] << 8) | rx[6];   // 电流 (0.01A)
    out.status     = ((uint16_t)rx[7] << 8) | rx[8];   // 状态字
    return true;
}

/* ========== 2. 设置电压/电流 (功能码 0x0F) =============================
 * 请求: [addr][0F][0001][0002][04][VH VL][IH IL][CRC]
 *       起始寄存器 0x0001, 写 2 个, 数据字节数=4, 大端序
 * 应答: [addr][0F][0001][0002][CRC]                      (8 字节, 回声)
 *
 * ⚠️ 电流单位注意:
 *   设定时 ×10 (0.1A 分辨率):  60 → 6.0A
 *   读取时 ×100 (0.01A 分辨率): 600 → 6.00A (见 readDevInfo)
 */
bool ModbusClient::setVI(uint8_t addr, uint16_t v_dV, uint16_t i_dA)
{
    uint8_t tx[13];
    tx[0]  = addr; tx[1] = 0x0F;             // 地址 + 功能码 0x0F
    tx[2]  = 0x00; tx[3] = 0x01;             // 起始寄存器 0x0001
    tx[4]  = 0x00; tx[5] = 0x02;             // 写 2 个寄存器
    tx[6]  = 0x04;                           // 数据字节数 = 4
    // 大端序: 高字节在前
    tx[7]  = (uint8_t)(v_dV >> 8); tx[8]  = (uint8_t)(v_dV & 0xFF);
    tx[9]  = (uint8_t)(i_dA >> 8); tx[10] = (uint8_t)(i_dA & 0xFF);
    int tx_len = append_crc(tx, 11);

    uint8_t rx[8];
    ModbusErr e = doXfer(addr, 0x0F, tx, tx_len, rx, sizeof(rx), 8);
    recordStat(e);
    if (e != MB_OK) {
        Serial.printf("[%s-%02X] set V/I fail: %s\n", _tag, addr, errStr(e));
        return false;
    }
    Serial.printf("[%s-%02X] set V=%.1fV I=%.1fA OK\n",
                  _tag, addr, v_dV / 10.0f, i_dA / 10.0f);
    return true;
}

/* ========== 3. 开关机控制 (功能码 0x06, 寄存器 0x0000) =================
 * 通电: 写入 0x00FF (通道0), 0x10FF (通道1)
 * 断电: 写入 0x0000 (通道0), 0x1000 (通道1)
 * 应答为报文回声 (8 字节)
 */
bool ModbusClient::powerOn(uint8_t addr, uint8_t channel)
{
    // 通道0: 0x00FF, 通道1: 0x10FF
    uint16_t value = channel ? 0x10FF : 0x00FF;
    Serial.printf("[%s-%02X] POWER ON ch=%d\n", _tag, addr, channel);

    uint8_t tx[8];
    tx[0] = addr; tx[1] = 0x06;              // 地址 + 功能码 0x06
    tx[2] = 0x00; tx[3] = 0x00;              // 寄存器地址 0x0000
    tx[4] = (uint8_t)(value >> 8); tx[5] = (uint8_t)(value & 0xFF);
    int tx_len = append_crc(tx, 6);

    uint8_t rx[8];
    ModbusErr e = doXfer(addr, 0x06, tx, tx_len, rx, sizeof(rx), 8);
    recordStat(e);
    return e == MB_OK;
}

bool ModbusClient::powerOff(uint8_t addr, uint8_t channel)
{
    // 通道0: 0x0000, 通道1: 0x1000
    uint16_t value = channel ? 0x1000 : 0x0000;
    Serial.printf("[%s-%02X] POWER OFF ch=%d\n", _tag, addr, channel);

    uint8_t tx[8];
    tx[0] = addr; tx[1] = 0x06;
    tx[2] = 0x00; tx[3] = 0x00;
    tx[4] = (uint8_t)(value >> 8); tx[5] = (uint8_t)(value & 0xFF);
    int tx_len = append_crc(tx, 6);

    uint8_t rx[8];
    ModbusErr e = doXfer(addr, 0x06, tx, tx_len, rx, sizeof(rx), 8);
    recordStat(e);
    return e == MB_OK;
}
#include "modbus.h"
#include "app_config.h"

/* ---------- CRC16 (协议原文) ---------- */
uint16_t ModbusClient::crc16(const uint8_t *buf, int len)
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
    uint16_t crc = ModbusClient::crc16(buf, len);
    buf[len]     = (uint8_t)(crc >> 8);
    buf[len + 1] = (uint8_t)(crc & 0xFF);
    return len + 2;
}

static bool verify_crc(const uint8_t *buf, int len)
{
    if (len < 4) return false;
    uint16_t calc = ModbusClient::crc16(buf, len - 2);
    uint16_t recv = ((uint16_t)buf[len - 2] << 8) | buf[len - 1];
    return calc == recv;
}

const char *ModbusClient::errStr(ModbusErr e)
{
    switch (e) {
    case MB_OK: return "OK";
    case MB_ERR_TIMEOUT: return "TIMEOUT";
    case MB_ERR_WRITE:   return "WRITE";
    case MB_ERR_LEN:     return "LEN";
    case MB_ERR_ADDR:    return "ADDR";
    case MB_ERR_FC:      return "FC";
    case MB_ERR_CRC:     return "CRC";
    case MB_ERR_EXCEPT:  return "EXCEPT";
    }
    return "?";
}

void ModbusClient::recordStat(ModbusErr e)
{
    _stats.tx++;
    _stats.last_err = e;
    if (e == MB_OK) {
        if (_stats.consecutive_fail >= MB_OFFLINE_THRESHOLD) {
            Serial.printf("[%s] bus recovered (was offline %lu)\n",
                          _tag, (unsigned long)_stats.consecutive_fail);
        }
        _stats.rx_ok++;
        _stats.consecutive_fail = 0;
        return;
    }
    switch (e) {
    case MB_ERR_TIMEOUT: _stats.err_timeout++; break;
    case MB_ERR_WRITE:   _stats.err_write++;   break;
    case MB_ERR_LEN:     _stats.err_len++;     break;
    case MB_ERR_ADDR:    _stats.err_addr++;    break;
    case MB_ERR_FC:      _stats.err_fc++;      break;
    case MB_ERR_CRC:     _stats.err_crc++;     break;
    case MB_ERR_EXCEPT:  _stats.err_except++;  break;
    default: break;
    }
    _stats.consecutive_fail++;
    if (_stats.consecutive_fail == MB_OFFLINE_THRESHOLD) {
        Serial.printf("[%s] *** BUS OFFLINE *** last=%s\n", _tag, errStr(e));
    }
}

ModbusErr ModbusClient::doXfer(uint8_t addr, uint8_t fc,
                               const uint8_t *tx, int tx_len,
                               uint8_t *rx, int rx_max, int expect_len)
{
    int n = _bus.xfer(tx, tx_len, rx, rx_max, RS485_TIMEOUT_MS);
    if (n == RS485_ERR_WRITE)   return MB_ERR_WRITE;
    if (n == RS485_ERR_TIMEOUT || n <= 0) return MB_ERR_TIMEOUT;

    /* 异常帧: 5 字节 addr + (fc|0x80) + code + CRC2 */
    if (n == 5 && rx[0] == addr && rx[1] == (fc | 0x80) && verify_crc(rx, n)) {
        Serial.printf("[%s] exception fc=0x%02X code=0x%02X\n", _tag, fc, rx[2]);
        return MB_ERR_EXCEPT;
    }
    if (n != expect_len)    return MB_ERR_LEN;
    if (rx[0] != addr)      return MB_ERR_ADDR;
    if (rx[1] != fc)        return MB_ERR_FC;
    if (!verify_crc(rx, n)) return MB_ERR_CRC;
    return MB_OK;
}

/* ============ 1. 读 V/I/状态 (0x03 读 0x0001 起 3 个) ============ */
bool ModbusClient::readDevInfo(uint8_t addr, DevInfo &out)
{
    uint8_t tx[8];
    tx[0] = addr; tx[1] = 0x03;
    tx[2] = 0x00; tx[3] = 0x01;
    tx[4] = 0x00; tx[5] = 0x03;
    int tx_len = append_crc(tx, 6);

    uint8_t rx[16];
    ModbusErr e = doXfer(addr, 0x03, tx, tx_len, rx, sizeof(rx), 11);
    if (e == MB_OK && rx[2] != 0x06) e = MB_ERR_LEN;
    recordStat(e);
    if (e != MB_OK) {
        Serial.printf("[%s-%02X] read fail: %s (cfail=%lu)\n",
                      _tag, addr, errStr(e),
                      (unsigned long)_stats.consecutive_fail);
        return false;
    }
    out.voltage_dV = ((uint16_t)rx[3] << 8) | rx[4];
    out.current_cA = ((uint16_t)rx[5] << 8) | rx[6];
    out.status     = ((uint16_t)rx[7] << 8) | rx[8];
    return true;
}

/* ============ 2. 设置 V/I (0x0F 写 0x0001 起 2 个) ============ */
bool ModbusClient::setVI(uint8_t addr, uint16_t v_dV, uint16_t i_dA)
{
    uint8_t tx[13];
    tx[0]  = addr; tx[1] = 0x0F;
    tx[2]  = 0x00; tx[3] = 0x01;
    tx[4]  = 0x00; tx[5] = 0x02;
    tx[6]  = 0x04;
    tx[7]  = (uint8_t)(v_dV >> 8); tx[8]  = (uint8_t)(v_dV & 0xFF);
    tx[9]  = (uint8_t)(i_dA >> 8); tx[10] = (uint8_t)(i_dA & 0xFF);
    int tx_len = append_crc(tx, 11);

    uint8_t rx[8];
    ModbusErr e = doXfer(addr, 0x0F, tx, tx_len, rx, sizeof(rx), 8);
    recordStat(e);
    if (e != MB_OK) {
        Serial.printf("[%s-%02X] set V/I fail: %s\n", _tag, addr, errStr(e));
        return false;
    }
    Serial.printf("[%s-%02X] set V=%.1fV I=%.1fA OK\n",
                  _tag, addr, v_dV / 10.0f, i_dA / 10.0f);
    return true;
}

/* ============ 3. 开关机 (0x06 寄存器 0x0000) ============ */
bool ModbusClient::powerOn(uint8_t addr, uint8_t channel)
{
    uint16_t value = channel ? 0x10FF : 0x00FF;
    Serial.printf("[%s-%02X] POWER ON ch=%d\n", _tag, addr, channel);

    uint8_t tx[8];
    tx[0] = addr; tx[1] = 0x06;
    tx[2] = 0x00; tx[3] = 0x00;
    tx[4] = (uint8_t)(value >> 8); tx[5] = (uint8_t)(value & 0xFF);
    int tx_len = append_crc(tx, 6);

    uint8_t rx[8];
    ModbusErr e = doXfer(addr, 0x06, tx, tx_len, rx, sizeof(rx), 8);
    recordStat(e);
    return e == MB_OK;
}

bool ModbusClient::powerOff(uint8_t addr, uint8_t channel)
{
    uint16_t value = channel ? 0x1000 : 0x0000;
    Serial.printf("[%s-%02X] POWER OFF ch=%d\n", _tag, addr, channel);

    uint8_t tx[8];
    tx[0] = addr; tx[1] = 0x06;
    tx[2] = 0x00; tx[3] = 0x00;
    tx[4] = (uint8_t)(value >> 8); tx[5] = (uint8_t)(value & 0xFF);
    int tx_len = append_crc(tx, 6);

    uint8_t rx[8];
    ModbusErr e = doXfer(addr, 0x06, tx, tx_len, rx, sizeof(rx), 8);
    recordStat(e);
    return e == MB_OK;
}
