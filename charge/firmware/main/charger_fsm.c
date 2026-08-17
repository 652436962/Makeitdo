#include "charger_fsm.h"
#include "modbus.h"
#include "app_config.h"
#include "io_ctrl.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "fsm"

/* ---------- 电池规格表 ---------- */
static const battery_spec_t s_specs[] = {
    /* 48V 铅酸:  10.0~55.0V → 充电 58.4V / 6.0A */
    { .v_min_dV=100,  .v_max_dV=550,  .v_set_dV=584,  .i_set_dA=60, .name="48V" },
    /* 60V:        55.0~68.0V → 73.0V / 6.0A                       */
    { .v_min_dV=550,  .v_max_dV=680,  .v_set_dV=730,  .i_set_dA=60, .name="60V" },
    /* 72V:        68.0~85.0V → 87.6V / 8.0A                       */
    { .v_min_dV=680,  .v_max_dV=850,  .v_set_dV=876,  .i_set_dA=80, .name="72V" },
};

static const battery_spec_t *match_battery(uint16_t v_dV)
{
    for (size_t i = 0; i < sizeof(s_specs)/sizeof(s_specs[0]); i++) {
        if (v_dV >= s_specs[i].v_min_dV && v_dV < s_specs[i].v_max_dV)
            return &s_specs[i];
    }
    return NULL;
}

/* ---------- 全局状态 ---------- */
static charge_state_t s_state    = ST_IDLE;
static dev_info_t     s_chg_info = {0};
static dev_info_t     s_psu_info = {0};
static const battery_spec_t *s_spec = NULL;

charge_state_t  charger_fsm_state    (void) { return s_state; }
const dev_info_t *charger_fsm_chg_info(void) { return &s_chg_info; }
const dev_info_t *charger_fsm_psu_info(void) { return &s_psu_info; }

/* ---------- 单 tick 推进 ---------- */
static void fsm_tick(void)
{
    /* === 1. 双路读取 (心跳, 任意一路失败不阻塞另一路) === */
    bool ok_chg = modbus_read_dev_info(CHG_UART_PORT, MODBUS_ADDR_CHG, &s_chg_info);
    bool ok_psu = modbus_read_dev_info(PSU_UART_PORT, MODBUS_ADDR_PSU, &s_psu_info);

    if (!ok_chg) {
        ESP_LOGW(TAG, "充电器通信失败");
        io_set_led(LED_FAULT);
        /* 连续失败达到离线阈值, 提升为 FAULT 状态 */
        if (modbus_is_offline(CHG_UART_PORT)) {
            ESP_LOGE(TAG, "充电器总线离线, 进入 FAULT");
            s_state = ST_FAULT;
            io_buzzer(true);
        }
        return;     /* 不影响 PSU, 但状态机暂停推进 */
    }
    if (!ok_psu) {
        /* 电源离线仅上报, 不阻断充电 */
        if (modbus_is_offline(PSU_UART_PORT)) {
            ESP_LOGW(TAG, "电源总线离线 (不影响充电)");
        }
    }

    /* === 2. 故障优先 === */
    if (ST_IS_FAULT(s_chg_info.status)) {
        modbus_power_off(CHG_UART_PORT, MODBUS_ADDR_CHG, 0);
        s_state = ST_FAULT;
        io_set_led(LED_FAULT);
        io_buzzer(true);
        return;
    }

    /* === 3. 状态机推进 === */
    switch (s_state) {
    case ST_IDLE:
        io_set_led(LED_IDLE);
        if (s_chg_info.voltage_dV >= BAT_INSERT_THRESHOLD_DV) {
            s_spec = match_battery(s_chg_info.voltage_dV);
            if (s_spec) {
                ESP_LOGI(TAG, "电池接入 %s, Vbat=%.1fV",
                         s_spec->name, s_chg_info.voltage_dV / 10.0f);
                s_state = ST_DETECTED;
            }
        }
        break;

    case ST_DETECTED:
        if (modbus_set_voltage_current(CHG_UART_PORT, MODBUS_ADDR_CHG,
                                       s_spec->v_set_dV, s_spec->i_set_dA)) {
            s_state = ST_CONFIG;
        }
        break;

    case ST_CONFIG:
        if (modbus_power_on(CHG_UART_PORT, MODBUS_ADDR_CHG, 0)) {
            ESP_LOGI(TAG, "下发开机, 进入充电态");
            s_state = ST_CHARGING;
        }
        break;

    case ST_CHARGING:
        io_set_led(LED_CHARGING);
        if (ST_IS_FULL(s_chg_info.status)) {
            modbus_power_off(CHG_UART_PORT, MODBUS_ADDR_CHG, 0);
            ESP_LOGI(TAG, "充满, 关机");
            s_state = ST_FINISH;
        }
        break;

    case ST_FINISH:
        io_set_led(LED_FULL);
        if (s_chg_info.voltage_dV < BAT_INSERT_THRESHOLD_DV) {
            ESP_LOGI(TAG, "电池拔出, 复位 IDLE");
            s_state = ST_IDLE;
            s_spec  = NULL;
        }
        break;

    case ST_FAULT:
        if (s_chg_info.voltage_dV < BAT_INSERT_THRESHOLD_DV) {
            io_buzzer(false);
            s_state = ST_IDLE;
            s_spec  = NULL;
        }
        break;
    }
}

void charger_fsm_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        fsm_tick();
        vTaskDelayUntil(&last, pdMS_TO_TICKS(POLL_PERIOD_MS));
    }
}
