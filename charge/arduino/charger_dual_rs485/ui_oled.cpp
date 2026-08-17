/**
 * @file    ui_oled.cpp
 * @brief   OLED 显示实现 — U8g2 SSD1306 128×64 全缓冲刷新
 *
 * 显示布局 (10像素行高, u8g2_font_6x10_tf):
 *   y=10:  "CHG  58.4V  6.00A S2"   — 充电器数据
 *   y=22:  "PSU  58.6V  6.05A 0000" — 电源数据
 *   y=34:  "T    00:12:34"          — 累计时长
 *   y=40~48: 进度条 (框+填充)        — 电压→百分比映射
 *   y=62:  "FSM: CHARGING   62%"    — 状态机 + 百分比
 *
 * 电压→百分比映射规则:
 *   48V 电池: 50.0V=0%, 58.4V=100%
 *   60V 电池: 62.0V=0%, 73.0V=100%
 *   72V 电池: 68.0V=0%, 87.6V=100%
 *   根据当前电压落在哪一档自动选择区间。
 */

#include "ui_oled.h"
#include "app_config.h"
#include <U8g2lib.h>
#include <Wire.h>

// U8g2 对象: 硬件 I2C, SSD1306 128×64 非缓冲模式, R0=不旋转
// 使用 HW_I2C 版本, 比 SW_I2C 快 3~5 倍, 2Hz 刷新无闪烁
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C s_u8g2(U8G2_R0, /*reset*/ U8X8_PIN_NONE);

void ui_init(void)
{
    // 启动 I2C 总线: 400kHz 快速模式
    Wire.begin(I2C_SDA_GPIO, I2C_SCL_GPIO, 400000);
    // U8g2 内部 I2C 地址格式: 7位地址左移1位 (0x3C<<1 = 0x78)
    s_u8g2.setI2CAddress(OLED_I2C_ADDR << 1);
    s_u8g2.begin();
    // 绘制开机引导画面
    s_u8g2.clearBuffer();
    s_u8g2.setFont(u8g2_font_6x10_tf);            // 6×10 像素, 等宽英文字体
    s_u8g2.drawStr( 8, 22, "Charger Dual-RS485");
    s_u8g2.drawStr(20, 42, "Booting...");
    s_u8g2.sendBuffer();
    Serial.println("OLED ready (U8g2 SSD1306 128x64)");
}

void ui_draw(const ChargerFsm &fsm, uint32_t tick_sec)
{
    const DevInfo &c = fsm.chgInfo();              // 充电器实时数据
    const DevInfo &p = fsm.psuInfo();              // BMS/电源实时数据
    char line[32];

    s_u8g2.clearBuffer();
    s_u8g2.setFont(u8g2_font_6x10_tf);            // 6×10 等宽字体, 每屏最多 8 行

    // ---- 第1行: 充电器数据 ----
    // 电压: dV→V 除以10, 电流: cA→A 除以100, 状态: 仅显示低4位主状态
    snprintf(line, sizeof(line), "CHG %4.1fV %4.2fA S%X",
             c.voltage_dV / 10.0f, c.current_cA / 100.0f,
             (unsigned)(c.status & 0x0F));
    s_u8g2.drawStr(0, 10, line);

    // ---- 第2行: BMS/电源数据 ----
    snprintf(line, sizeof(line), "PSU %4.1fV %4.2fA %04X",
             p.voltage_dV / 10.0f, p.current_cA / 100.0f, (unsigned)p.status);
    s_u8g2.drawStr(0, 22, line);

    // ---- 第3行: 累计充电时长 HH:MM:SS ----
    uint32_t h = tick_sec / 3600, m = (tick_sec / 60) % 60, s = tick_sec % 60;
    snprintf(line, sizeof(line), "T  %02lu:%02lu:%02lu",
             (unsigned long)h, (unsigned long)m, (unsigned long)s);
    s_u8g2.drawStr(0, 34, line);

    // ---- 第4行: 进度条 (电压→百分比映射) ----
    int pct = 0;
    if (c.voltage_dV > 0) {
        // 按当前电压所在区间选择百分比映射参数 ([lo, hi] → [0%, 100%])
        int lo = 500, hi = 584;                    // 48V 档默认
        if (c.voltage_dV > 680)      { lo = 680; hi = 876; }  // 72V 档
        else if (c.voltage_dV > 550) { lo = 620; hi = 730; }  // 60V 档
        pct = (int)((c.voltage_dV - lo) * 100L / (hi - lo));
        if (pct < 0) pct = 0;                      // 下限钳位
        else if (pct > 100) pct = 100;              // 上限钳位
    }
    // 绘制 M x N 边框 + 内部填充矩形
    s_u8g2.drawFrame(0, 40, 128, 8);               // 外框 128×8
    s_u8g2.drawBox  (1, 41, (126 * pct) / 100, 6); // 填充 (1px内边距)

    // ---- 第5行: 状态机名称 + 百分比 ----
    snprintf(line, sizeof(line), "FSM: %-8s  %d%%", fsm.stateName(), pct);
    s_u8g2.drawStr(0, 62, line);

    s_u8g2.sendBuffer();                           // 一次性刷新整个屏幕
}
#include "ui_oled.h"
#include "app_config.h"
#include <U8g2lib.h>
#include <Wire.h>

/* SSD1306 128x64 I2C, 全缓冲, 软件 I2C 也可换 HW I2C 构造 */
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C s_u8g2(U8G2_R0, /*reset*/ U8X8_PIN_NONE);

void ui_init(void)
{
    Wire.begin(I2C_SDA_GPIO, I2C_SCL_GPIO, 400000);
    s_u8g2.setI2CAddress(OLED_I2C_ADDR << 1);
    s_u8g2.begin();
    s_u8g2.clearBuffer();
    s_u8g2.setFont(u8g2_font_6x10_tf);
    s_u8g2.drawStr( 8, 22, "Charger Dual-RS485");
    s_u8g2.drawStr(20, 42, "Booting...");
    s_u8g2.sendBuffer();
    Serial.println("OLED ready (U8g2 SSD1306 128x64)");
}

void ui_draw(const ChargerFsm &fsm, uint32_t tick_sec)
{
    const DevInfo &c = fsm.chgInfo();
    const DevInfo &p = fsm.psuInfo();
    char line[32];

    s_u8g2.clearBuffer();
    s_u8g2.setFont(u8g2_font_6x10_tf);

    snprintf(line, sizeof(line), "CHG %4.1fV %4.2fA S%X",
             c.voltage_dV / 10.0f, c.current_cA / 100.0f,
             (unsigned)(c.status & 0x0F));
    s_u8g2.drawStr(0, 10, line);

    snprintf(line, sizeof(line), "PSU %4.1fV %4.2fA %04X",
             p.voltage_dV / 10.0f, p.current_cA / 100.0f, (unsigned)p.status);
    s_u8g2.drawStr(0, 22, line);

    uint32_t h = tick_sec / 3600, m = (tick_sec / 60) % 60, s = tick_sec % 60;
    snprintf(line, sizeof(line), "T  %02lu:%02lu:%02lu",
             (unsigned long)h, (unsigned long)m, (unsigned long)s);
    s_u8g2.drawStr(0, 34, line);

    int pct = 0;
    if (c.voltage_dV > 0) {
        int lo = 500, hi = 584;
        if (c.voltage_dV > 680)      { lo = 680; hi = 876; }
        else if (c.voltage_dV > 550) { lo = 620; hi = 730; }
        pct = (int)((c.voltage_dV - lo) * 100L / (hi - lo));
        if (pct < 0) pct = 0; else if (pct > 100) pct = 100;
    }
    s_u8g2.drawFrame(0, 40, 128, 8);
    s_u8g2.drawBox  (1, 41, (126 * pct) / 100, 6);

    snprintf(line, sizeof(line), "FSM: %-8s  %d%%", fsm.stateName(), pct);
    s_u8g2.drawStr(0, 62, line);

    s_u8g2.sendBuffer();
}
