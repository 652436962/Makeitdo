#include "app_config.h"
#include "rs485.h"
#include "modbus.h"
#include "charger_fsm.h"
#include "ui_oled.h"
#include "io_ctrl.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "main"

/* 诊断任务: 每 10s 打印两路 UART 累计统计 */
static void diag_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        const modbus_stats_t *cs = modbus_get_stats(CHG_UART_PORT);
        const modbus_stats_t *ps = modbus_get_stats(PSU_UART_PORT);
        ESP_LOGI(TAG,
            "[DIAG] CHG tx=%lu ok=%lu to=%lu crc=%lu exc=%lu cfail=%lu last=%s | offline=%d",
            (unsigned long)cs->tx, (unsigned long)cs->rx_ok,
            (unsigned long)cs->err_timeout, (unsigned long)cs->err_crc,
            (unsigned long)cs->err_exception,
            (unsigned long)cs->consecutive_fail,
            modbus_err_str(cs->last_err),
            modbus_is_offline(CHG_UART_PORT));
        ESP_LOGI(TAG,
            "[DIAG] PSU tx=%lu ok=%lu to=%lu crc=%lu exc=%lu cfail=%lu last=%s | offline=%d",
            (unsigned long)ps->tx, (unsigned long)ps->rx_ok,
            (unsigned long)ps->err_timeout, (unsigned long)ps->err_crc,
            (unsigned long)ps->err_exception,
            (unsigned long)ps->consecutive_fail,
            modbus_err_str(ps->last_err),
            modbus_is_offline(PSU_UART_PORT));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "===== Charger Dual-RS485 boot =====");

    /* 默认打开 RS485 和 modbus 详细日志, 生产可改 ESP_LOG_INFO */
    esp_log_level_set("rs485",  ESP_LOG_DEBUG);
    esp_log_level_set("modbus", ESP_LOG_DEBUG);

    /* 1. 板载 IO */
    io_init();

    /* 2. 双路 RS485 (UART2 = 充电器, UART1 = 电源) */
    rs485_init(CHG_UART_PORT, CHG_UART_TXD, CHG_UART_RXD, CHG_UART_DE);
    rs485_init(PSU_UART_PORT, PSU_UART_TXD, PSU_UART_RXD, PSU_UART_DE);

    /* 3. UI 子系统 */
    ui_init();

    /* 4. 任务 */
    xTaskCreatePinnedToCore(charger_fsm_task, "fsm",  4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(ui_task,          "ui",   4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(diag_task,        "diag", 3072, NULL, 1, NULL, 0);

    ESP_LOGI(TAG, "tasks started, app_main exiting");
}
