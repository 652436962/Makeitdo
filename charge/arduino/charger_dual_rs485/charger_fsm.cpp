/**
 * @file    charger_fsm.cpp
 * @brief   充电状态机实现 — 电池匹配 + 6态转移逻辑
 *
 * 状态转移条件:
 *   IDLE→DETECTED: 电池电压 >= 10.0V 且能匹配到已知电池规格
 *   DETECTED→CONFIG: 向充电器下发 V/I 参数成功
 *   CONFIG→CHARGING: 向充电器下发开机指令成功
 *   CHARGING→FINISH: 充电器状态字低4位 = 0x3 (充满) 或 电流逼近0
 *   FINISH→IDLE: 电池拔出 (电压降至10V以下)
 *   CHARGING→FAULT: 充电器离线/故障
 *   FAULT→IDLE: 电池拔出 (解除蜂鸣)
 *
 * 电池规格表 (与充电器协议匹配):
 *   48V 电池: 恒压 58.4V, 恒流 6.0A
 *   60V 电池: 恒压 73.0V, 恒流 6.0A
 *   72V 电池: 恒压 87.6V, 恒流 8.0A
 */

#include "charger_fsm.h"
#include "app_config.h"
#include "io_ctrl.h"

// 电池规格查找表 — 按电压区间线性匹配
// 单位: v_min/v_max/v_set = 0.1V, i_set = 0.1A
static const BatterySpec kSpecs[] = {
    // -------- 匹配区间 ------  --设定值--  名称
    //   最低    最高    截止V    充电I
    {   100,    550,    584,    60,  "48V" },  // 10.0~55.0V → 58.4V/6.0A
    {   550,    680,    730,    60,  "60V" },  // 55.0~68.0V → 73.0V/6.0A
    {   680,    850,    876,    80,  "72V" },  // 68.0~85.0V → 87.6V/8.0A
};

const BatterySpec *ChargerFsm::match(uint16_t v_dV)
{
    for (auto &s : kSpecs) {
        // 电压落在 [v_min, v_max) 区间内
        if (v_dV >= s.v_min_dV && v_dV < s.v_max_dV) return &s;
    }
    return nullptr;  // 无匹配 (如锂电裸电芯 3.7V 等非标电压)
}

const char *ChargerFsm::stateName() const
{
    switch (_state) {
    case ST_IDLE:     return "IDLE";
    case ST_DETECTED: return "DETECTED";
    case ST_CONFIG:   return "CONFIG";
    case ST_CHARGING: return "CHARGING";
    case ST_FINISH:   return "FULL";        // 显示为 FULL, 更直观
    case ST_FAULT:    return "FAULT";
    }
    return "?";
}

void ChargerFsm::tick()
{
    /* ================================================================
     * 第一层: 双路 Modbus 读取 (每次 tick 都会执行)
     *   - 充电器: 读电压/电流/状态, 用于充电决策
     *   - BMS/电源: 读遥测数据, 用于监控和协议解析 (不影响充电流程)
     * ================================================================ */
    bool ok_chg = _chg.readDevInfo(MODBUS_ADDR_CHG, _chg_info);
    bool ok_psu = _psu.readDevInfo(MODBUS_ADDR_PSU, _psu_info);

    // 充电器通信失败: 亮故障灯, 若连续失败达到阈值则进 FAULT
    if (!ok_chg) {
        io_set_led(LED_FAULT);
        if (_chg.offline()) {
            Serial.println("充电器总线离线, 进入 FAULT");
            _state = ST_FAULT;
            io_buzzer(true);          // 长鸣告警
        }
        return;                       // 本轮不继续推进状态机
    }
    // BMS 离线: 仅打印日志, 不影响充电流程 (充电器独立工作)
    if (!ok_psu && _psu.offline()) {
        Serial.println("电源总线离线 (不影响充电)");
    }

    /* ================================================================
     * 第二层: 故障优先判定 (任何状态都可能触发)
     *   充电器自身报告故障 (如过流/过压/过温/短路)
     *   立即关机 + 蜂鸣告警, 等待电池拔出后复位
     * ================================================================ */
    if (ST_IS_FAULT(_chg_info.status)) {
        _chg.powerOff(MODBUS_ADDR_CHG, 0);      // 紧急关机
        _state = ST_FAULT;
        io_set_led(LED_FAULT);
        io_buzzer(true);
        return;
    }

    /* ================================================================
     * 第三层: 正常状态转移 (按当前状态执行不同逻辑)
     * ================================================================ */
    switch (_state) {

    // ---- IDLE: 空闲态, 持续检测电池接入 ----
    case ST_IDLE:
        io_set_led(LED_IDLE);                      // 1Hz 慢闪
        if (_chg_info.voltage_dV >= BAT_INSERT_THRESHOLD_DV) {
            _spec = match(_chg_info.voltage_dV);    // 匹配电池规格
            if (_spec) {
                Serial.printf("电池接入 %s, Vbat=%.1fV\n",
                              _spec->name, _chg_info.voltage_dV / 10.0f);
                _state = ST_DETECTED;
            }
            // 未匹配到规格: 保持 IDLE, 不充电 (防止误充非标电池)
        }
        break;

    // ---- DETECTED: 检测到电池, 尝试下发充电参数 ----
    case ST_DETECTED:
        if (_chg.setVI(MODBUS_ADDR_CHG, _spec->v_set_dV, _spec->i_set_dA))
            _state = ST_CONFIG;
        // 失败则下一 tick 重试 (由超时离线检测兜底)
        break;

    // ---- CONFIG: 参数已设, 等待开机 ----
    case ST_CONFIG:
        if (_chg.powerOn(MODBUS_ADDR_CHG, 0)) {
            Serial.println("下发开机, 进入充电态");
            _state = ST_CHARGING;
        }
        break;

    // ---- CHARGING: 充电进行中, 监测充满/故障 ----
    case ST_CHARGING:
        io_set_led(LED_CHARGING);                  // 5Hz 快闪
        if (ST_IS_FULL(_chg_info.status)) {
            _chg.powerOff(MODBUS_ADDR_CHG, 0);     // 充满后自动关机
            Serial.println("充满, 关机");
            _state = ST_FINISH;
        }
        break;

    // ---- FINISH: 充满待取, 等待电池拔出 ----
    case ST_FINISH:
        io_set_led(LED_FULL);                      // 常亮
        if (_chg_info.voltage_dV < BAT_INSERT_THRESHOLD_DV) {
            Serial.println("电池拔出, 复位 IDLE");
            _state = ST_IDLE;
            _spec  = nullptr;                      // 清除规格引用
        }
        break;

    // ---- FAULT: 故障态, 等待电池拔出后复位 ----
    case ST_FAULT:
        // 电池拔出后解除蜂鸣告警, 回到 IDLE
        if (_chg_info.voltage_dV < BAT_INSERT_THRESHOLD_DV) {
            io_buzzer(false);
            _state = ST_IDLE;
            _spec  = nullptr;
        }
        break;
    }
}
#include "charger_fsm.h"
#include "app_config.h"
#include "io_ctrl.h"

static const BatterySpec kSpecs[] = {
    /* 48V: 10~55V → 58.4V/6.0A */
    { 100, 550, 584, 60, "48V" },
    /* 60V: 55~68V → 73.0V/6.0A */
    { 550, 680, 730, 60, "60V" },
    /* 72V: 68~85V → 87.6V/8.0A */
    { 680, 850, 876, 80, "72V" },
};

const BatterySpec *ChargerFsm::match(uint16_t v_dV)
{
    for (auto &s : kSpecs) {
        if (v_dV >= s.v_min_dV && v_dV < s.v_max_dV) return &s;
    }
    return nullptr;
}

const char *ChargerFsm::stateName() const
{
    switch (_state) {
    case ST_IDLE:     return "IDLE";
    case ST_DETECTED: return "DETECTED";
    case ST_CONFIG:   return "CONFIG";
    case ST_CHARGING: return "CHARGING";
    case ST_FINISH:   return "FULL";
    case ST_FAULT:    return "FAULT";
    }
    return "?";
}

void ChargerFsm::tick()
{
    /* === 1. 双路读取 === */
    bool ok_chg = _chg.readDevInfo(MODBUS_ADDR_CHG, _chg_info);
    bool ok_psu = _psu.readDevInfo(MODBUS_ADDR_PSU, _psu_info);

    if (!ok_chg) {
        io_set_led(LED_FAULT);
        if (_chg.offline()) {
            Serial.println("充电器总线离线, 进入 FAULT");
            _state = ST_FAULT;
            io_buzzer(true);
        }
        return;
    }
    if (!ok_psu && _psu.offline()) {
        Serial.println("电源总线离线 (不影响充电)");
    }

    /* === 2. 故障优先 === */
    if (ST_IS_FAULT(_chg_info.status)) {
        _chg.powerOff(MODBUS_ADDR_CHG, 0);
        _state = ST_FAULT;
        io_set_led(LED_FAULT);
        io_buzzer(true);
        return;
    }

    /* === 3. 状态机推进 === */
    switch (_state) {
    case ST_IDLE:
        io_set_led(LED_IDLE);
        if (_chg_info.voltage_dV >= BAT_INSERT_THRESHOLD_DV) {
            _spec = match(_chg_info.voltage_dV);
            if (_spec) {
                Serial.printf("电池接入 %s, Vbat=%.1fV\n",
                              _spec->name, _chg_info.voltage_dV / 10.0f);
                _state = ST_DETECTED;
            }
        }
        break;
    case ST_DETECTED:
        if (_chg.setVI(MODBUS_ADDR_CHG, _spec->v_set_dV, _spec->i_set_dA))
            _state = ST_CONFIG;
        break;
    case ST_CONFIG:
        if (_chg.powerOn(MODBUS_ADDR_CHG, 0)) {
            Serial.println("下发开机, 进入充电态");
            _state = ST_CHARGING;
        }
        break;
    case ST_CHARGING:
        io_set_led(LED_CHARGING);
        if (ST_IS_FULL(_chg_info.status)) {
            _chg.powerOff(MODBUS_ADDR_CHG, 0);
            Serial.println("充满, 关机");
            _state = ST_FINISH;
        }
        break;
    case ST_FINISH:
        io_set_led(LED_FULL);
        if (_chg_info.voltage_dV < BAT_INSERT_THRESHOLD_DV) {
            Serial.println("电池拔出, 复位 IDLE");
            _state = ST_IDLE;
            _spec  = nullptr;
        }
        break;
    case ST_FAULT:
        if (_chg_info.voltage_dV < BAT_INSERT_THRESHOLD_DV) {
            io_buzzer(false);
            _state = ST_IDLE;
            _spec  = nullptr;
        }
        break;
    }
}
