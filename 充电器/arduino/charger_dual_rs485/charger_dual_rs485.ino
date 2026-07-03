/* ============================================================================
 * Charger Dual-RS485 — Arduino-ESP32 入口
 *
 * 板型: ESP32-PicoD4 开源迷你开发板
 * 功能: 一路 RS485 挂载充电器(地址0)+BMS保护板(地址1)
 *       OLED 128x64 显示, 6态自动充电状态机
 *
 * 调度周期 (loop中按时间片轮转, 无RTOS依赖):
 *   50ms   — LED闪烁更新 + 按键扫描 (长按3s触发重启)
 *   1s     — Modbus轮询充电器/BMS + 状态机推进
 *   500ms  — OLED 全缓冲刷新 (2Hz)
 *   10s    — 串口诊断日志 (通信统计+离线状态)
 *
 * 协议参考:
 *   充电器 → 换电柜仓位与充电器通信协议.md (功能码 03/06/0F)
 *   BMS    → 铁塔BMS协议V3.0.md (功能码 01/03, 地址=1)
 * ========================================================================== */
#include <Arduino.h>
#include "app_config.h"
#include "rs485.h"
#include "modbus.h"
#include "charger_fsm.h"
#include "io_ctrl.h"
#include "ui_oled.h"

/* ========== 全局对象 ================================================= */
// RS485 物理层: 充电器用 Serial2, BMS/电源用 Serial1
// Serial1 的 GPIO 默认是 9/10 (与内置 Flash 冲突), 已在 app_config.h
// 中重映射到 GPIO 27/26/14 通过 begin(rx,tx) 参数指定
static RS485Bus     g_bus_chg(Serial2, CHG_UART_DE);  // 充电器 RS485 总线
static RS485Bus     g_bus_psu(Serial1, PSU_UART_DE);  // BMS/电源 RS485 总线

// Modbus 协议层: 绑定各自 RS485 总线并打上日志标签
static ModbusClient g_chg(g_bus_chg, "CHG");  // 充电器客户端
static ModbusClient g_psu(g_bus_psu, "PSU");  // BMS/电源客户端

// 应用层: 充电状态机, 内置电池规格匹配逻辑
static ChargerFsm   g_fsm(g_chg, g_psu);

/* ========== 非阻塞计时器 (ms精度, 基于millis溢出安全) ================ */
static uint32_t g_tick_sec = 0;      // 充电累计秒数 (仅CHARGING态递增)
static uint32_t g_last_poll  = 0;    // 上次 Modbus 轮询时间
static uint32_t g_last_io    = 0;    // 上次 LED/按键更新时间
static uint32_t g_last_ui    = 0;    // 上次 OLED 刷新时间
static uint32_t g_last_diag  = 0;    // 上次诊断日志时间

void setup()
{
    // 初始化调试串口 (USB CDC, 115200 8N1)
    Serial.begin(115200);
    delay(100);  // 等待 CDC 枚举完成
    Serial.println("\n===== Charger Dual-RS485 (Arduino) =====");

    // GPIO 初始化: LED/蜂鸣器/按键/电池检测
    io_init();

    // 双路 RS485 启动: 9600 8N1 半双工, DE由 GPIO 手动控制
    g_bus_chg.begin(CHG_UART_RXD, CHG_UART_TXD, RS485_BAUD);
    g_bus_psu.begin(PSU_UART_RXD, PSU_UART_TXD, RS485_BAUD);
    Serial.printf("UART2 CHG: TX=%d RX=%d DE=%d\n",
                  CHG_UART_TXD, CHG_UART_RXD, CHG_UART_DE);
    Serial.printf("UART1 PSU: TX=%d RX=%d DE=%d\n",
                  PSU_UART_TXD, PSU_UART_RXD, PSU_UART_DE);

    // OLED 初始化: I2C 400kHz, SSD1306 128x64, 显示开机画面
    ui_init();
}

void loop()
{
    uint32_t now = millis();  // 当前毫秒时间戳 (溢出安全)

    // [50ms 节拍] LED 闪烁更新 + 按键状态机扫描
    if (now - g_last_io >= 50) {
        g_last_io = now;
        io_tick();              // 根据当前 LED 模式翻转 GPIO
        KeyEvent k = io_key_scan();  // 按键去抖 + 长短按识别
        if (k == KEY_LONG) {
            // 长按 3s → 软件复位, 重新执行 setup()
            Serial.println("KEY long -> reboot");
            delay(100);
            ESP.restart();
        }
    }

    // [1s 节拍] Modbus 轮询 + 充电状态机推进
    if (now - g_last_poll >= POLL_PERIOD_MS) {
        g_last_poll = now;
        g_fsm.tick();  // 内部依次: 读充电器 → 读BMS → 故障判定 → 状态转移
        // 累计充电时长
        if (g_fsm.state() == ST_CHARGING) g_tick_sec++;
        else if (g_fsm.state() == ST_IDLE) g_tick_sec = 0;  // 空闲归零
    }

    // [500ms 节拍] OLED 全缓冲刷新 (2Hz)
    if (now - g_last_ui >= 500) {
        g_last_ui = now;
        ui_draw(g_fsm, g_tick_sec);
    }

    // [10s 节拍] 串口诊断信息: 两路通信统计 + 离线标志
    if (now - g_last_diag >= 10000) {
        g_last_diag = now;
        const ModbusStats &cs = g_chg.stats();
        const ModbusStats &ps = g_psu.stats();
        Serial.printf("[DIAG] CHG tx=%lu ok=%lu to=%lu crc=%lu cfail=%lu off=%d\n",
                      (unsigned long)cs.tx, (unsigned long)cs.rx_ok,
                      (unsigned long)cs.err_timeout, (unsigned long)cs.err_crc,
                      (unsigned long)cs.consecutive_fail, g_chg.offline());
        Serial.printf("[DIAG] PSU tx=%lu ok=%lu to=%lu crc=%lu cfail=%lu off=%d\n",
                      (unsigned long)ps.tx, (unsigned long)ps.rx_ok,
                      (unsigned long)ps.err_timeout, (unsigned long)ps.err_crc,
                      (unsigned long)ps.consecutive_fail, g_psu.offline());
    }
}
