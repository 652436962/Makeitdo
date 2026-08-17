/* ========================================================================
 *  OLED UI - 基于 u8g2 + u8g2-hal-esp-idf 组件, SSD1306 128x64 @ I2C
 *
 *  数据来源: charger_fsm.c 导出的实时结构, 每 500ms 重绘全屏
 *  布局 (128x64, 6x10 英文小字体):
 *   Y  0-11  CHG 48.0V 5.50A  ST:CHG
 *   Y 12-23  PSU 53.2V 6.00A  ST:0x00
 *   Y 24-35  T : 00:23:14  SOC:62%
 *   Y 36-47  [========      ]  进度条
 *   Y 48-63  FSM: CHARGING  48V
 * ====================================================================== */
#include "ui_oled.h"
#include "charger_fsm.h"
#include "modbus.h"
#include "app_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "u8g2.h"
#include "u8g2_esp32_hal.h"
#include <stdio.h>
#include <string.h>

#define TAG "ui"

static u8g2_t s_u8g2;
static uint32_t s_tick_sec = 0;     /* 累计秒数 (充电计时) */

static const char *state_name(charge_state_t s)
{
    switch (s) {
    case ST_IDLE:     return "IDLE";
    case ST_DETECTED: return "DETECTED";
    case ST_CONFIG:   return "CONFIG";
    case ST_CHARGING: return "CHARGING";
    case ST_FINISH:   return "FULL";
    case ST_FAULT:    return "FAULT";
    }
    return "?";
}

/* ---------- 初始化 ---------- */
void ui_init(void)
{
    /* 1. u8g2 HAL: 指定 I2C 引脚 */
    u8g2_esp32_hal_t hal = U8G2_ESP32_HAL_DEFAULT;
    hal.bus.i2c.sda = I2C_SDA_GPIO;
    hal.bus.i2c.scl = I2C_SCL_GPIO;
    u8g2_esp32_hal_init(hal);

    /* 2. 驱动: SSD1306 128x64 无名板, 全缓冲 */
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        &s_u8g2,
        U8G2_R0,
        u8g2_esp32_i2c_byte_cb,
        u8g2_esp32_gpio_and_delay_cb);

    /* u8g2 要求 I2C 地址左移 1 位 (R/W bit) */
    u8x8_SetI2CAddress(&s_u8g2.u8x8, OLED_I2C_ADDR << 1);

    u8g2_InitDisplay(&s_u8g2);
    u8g2_SetPowerSave(&s_u8g2, 0);

    /* 3. 开机画面 */
    u8g2_ClearBuffer(&s_u8g2);
    u8g2_SetFont(&s_u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(&s_u8g2, 8,  22, "Charger Dual-RS485");
    u8g2_DrawStr(&s_u8g2, 20, 42, "Booting...");
    u8g2_SendBuffer(&s_u8g2);

    ESP_LOGI(TAG, "OLED ready (u8g2, SSD1306 128x64 @0x%02X)", OLED_I2C_ADDR);
}

/* ---------- 一帧绘制 ---------- */
static void ui_draw_once(void)
{
    const dev_info_t *c = charger_fsm_chg_info();
    const dev_info_t *p = charger_fsm_psu_info();
    charge_state_t    st = charger_fsm_state();

    char line[32];

    u8g2_ClearBuffer(&s_u8g2);
    u8g2_SetFont(&s_u8g2, u8g2_font_6x10_tf);

    /* 行 1: 充电器 V/A/状态低 4 位 */
    snprintf(line, sizeof(line), "CHG %4.1fV %4.2fA S%X",
             c->voltage_dV / 10.0f,
             c->current_cA / 100.0f,
             (unsigned)(c->status & 0x0F));
    u8g2_DrawStr(&s_u8g2, 0, 10, line);

    /* 行 2: 电源 V/A/状态字高字节 */
    snprintf(line, sizeof(line), "PSU %4.1fV %4.2fA %04X",
             p->voltage_dV / 10.0f,
             p->current_cA / 100.0f,
             (unsigned)p->status);
    u8g2_DrawStr(&s_u8g2, 0, 22, line);

    /* 行 3: 累计时长 HH:MM:SS */
    uint32_t h = s_tick_sec / 3600;
    uint32_t m = (s_tick_sec / 60) % 60;
    uint32_t s = s_tick_sec % 60;
    snprintf(line, sizeof(line), "T  %02lu:%02lu:%02lu",
             (unsigned long)h, (unsigned long)m, (unsigned long)s);
    u8g2_DrawStr(&s_u8g2, 0, 34, line);

    /* 行 4: 进度条 (按电压线性近似 SOC, 仅可视化) */
    int pct = 0;
    if (c->voltage_dV > 0) {
        /* 简化: 48V 电池 50.0V(500) ~ 58.4V(584) 映射 0~100% */
        int lo = 500, hi = 584;
        if (c->voltage_dV > 680) { lo = 680; hi = 876; }       /* 72V */
        else if (c->voltage_dV > 550) { lo = 620; hi = 730; }   /* 60V */
        pct = (int)((c->voltage_dV - lo) * 100 / (hi - lo));
        if (pct < 0)   pct = 0;
        if (pct > 100) pct = 100;
    }
    u8g2_DrawFrame(&s_u8g2, 0, 40, 128, 8);
    u8g2_DrawBox  (&s_u8g2, 1, 41, (126 * pct) / 100, 6);

    /* 行 5: 状态机文字 */
    snprintf(line, sizeof(line), "FSM: %-8s  %d%%", state_name(st), pct);
    u8g2_DrawStr(&s_u8g2, 0, 62, line);

    u8g2_SendBuffer(&s_u8g2);
}

/* ---------- UI 任务 ---------- */
void ui_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        /* 仅在充电态累加计时 */
        if (charger_fsm_state() == ST_CHARGING) s_tick_sec++;
        else if (charger_fsm_state() == ST_IDLE) s_tick_sec = 0;

        ui_draw_once();
        vTaskDelayUntil(&last, pdMS_TO_TICKS(500));     /* 2Hz 刷新 */
    }
}
