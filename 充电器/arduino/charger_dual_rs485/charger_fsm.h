#pragma once
/**
 * @file    charger_fsm.h
 * @brief   6态充电状态机 — 电池识别 → 参数配置 → 充电 → 充满/故障
 *
 * 状态转移图:
 * @code
 *   IDLE ──(电池接入+电压匹配)──→ DETECTED ──(设置V/I成功)──→ CONFIG
 *     ↑                                                         │
 *     │                                              (开机成功)  │
 *     │                                                         ▼
 *   FINISH ←──(充满,关机)── CHARGING ←──────────────────────────┘
 *     │                         │
 *     │                         │ (离线/故障)
 *     │                         ▼
 *     └──────(电池拔出)────── FAULT ──(电池拔出)──→ IDLE
 * @endcode
 *
 * 电池规格匹配: 根据电压范围自动识别 48V/60V/72V 电池,
 *   查表获得对应的充电截止电压和充电电流。
 */

#include <Arduino.h>
#include "modbus.h"

/** @brief 充电状态枚举 — 6种状态覆盖全生命周期 */
enum ChargeState : uint8_t {
    ST_IDLE,      /**< 空闲, 无电池接入, 持续检测电压 */
    ST_DETECTED,  /**< 检测到电池, 正在下发充电参数 (V/I) */
    ST_CONFIG,    /**< 参数已设置, 等待开机指令 */
    ST_CHARGING,  /**< 充电进行中, 持续监测充满/故障条件 */
    ST_FINISH,    /**< 充电完成, 等待用户拔出电池 */
    ST_FAULT,     /**< 故障态, 蜂鸣告警, 待电池拔出后复位 */
};

/** @brief 电池规格模板 — 用于电压匹配和充电参数设定 */
struct BatterySpec {
    uint16_t v_min_dV;   /**< 匹配电压下限 (0.1V) */
    uint16_t v_max_dV;   /**< 匹配电压上限 (0.1V) */
    uint16_t v_set_dV;   /**< 充电截止电压 (0.1V), 如 584→58.4V */
    uint16_t i_set_dA;   /**< 充电电流 (0.1A),    如 60→6.0A */
    const char *name;    /**< 规格名称 (如 "48V"/"60V"/"72V") */
};

class ChargerFsm {
public:
    /**
     * @param chg 充电器 Modbus 客户端
     * @param psu BMS/电源 Modbus 客户端
     */
    ChargerFsm(ModbusClient &chg, ModbusClient &psu)
        : _chg(chg), _psu(psu) {}

    /**
     * @brief 单次状态机推进 (由主循环按 POLL_PERIOD_MS 触发)
     *
     * 执行顺序:
     *   1. 双路 Modbus 读取 (_chg + _psu)
     *   2. 充电器离线/故障判定 (优先级最高)
     *   3. 根据当前状态执行转移逻辑
     */
    void tick();

    /* ---- 状态查询 ---- */
    ChargeState     state() const     { return _state; }
    const DevInfo  &chgInfo() const   { return _chg_info; }  /**< 充电器最新遥测 */
    const DevInfo  &psuInfo() const   { return _psu_info; }  /**< BMS/电源最新遥测 */
    const char     *stateName() const;                        /**< 状态名转字符串 */

private:
    /** @brief 根据当前电压匹配电池规格表, 无匹配返回 nullptr */
    const BatterySpec *match(uint16_t v_dV);

    ModbusClient &_chg;                    // 充电器 Modbus 客户端
    ModbusClient &_psu;                    // BMS/电源 Modbus 客户端
    ChargeState   _state    = ST_IDLE;     // 当前状态
    DevInfo       _chg_info = {0, 0, 0};  // 充电器实时数据缓存
    DevInfo       _psu_info = {0, 0, 0};  // BMS/电源实时数据缓存
    const BatterySpec *_spec = nullptr;    // 当前匹配到的电池规格
};
#pragma once
#include <Arduino.h>
#include "modbus.h"

enum ChargeState : uint8_t {
    ST_IDLE,
    ST_DETECTED,
    ST_CONFIG,
    ST_CHARGING,
    ST_FINISH,
    ST_FAULT,
};

struct BatterySpec {
    uint16_t v_min_dV, v_max_dV;
    uint16_t v_set_dV, i_set_dA;
    const char *name;
};

class ChargerFsm {
public:
    ChargerFsm(ModbusClient &chg, ModbusClient &psu)
        : _chg(chg), _psu(psu) {}

    /** 单次 tick, 由调用方按 POLL_PERIOD_MS 触发 */
    void tick();

    ChargeState     state() const     { return _state; }
    const DevInfo  &chgInfo() const   { return _chg_info; }
    const DevInfo  &psuInfo() const   { return _psu_info; }
    const char     *stateName() const;

private:
    const BatterySpec *match(uint16_t v_dV);

    ModbusClient &_chg;
    ModbusClient &_psu;
    ChargeState   _state    = ST_IDLE;
    DevInfo       _chg_info = {0, 0, 0};
    DevInfo       _psu_info = {0, 0, 0};
    const BatterySpec *_spec = nullptr;
};
