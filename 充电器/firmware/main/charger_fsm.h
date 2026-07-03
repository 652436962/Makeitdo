#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "modbus.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ST_IDLE,        /* 等待电池接入                */
    ST_DETECTED,    /* 已识别电池电压等级          */
    ST_CONFIG,      /* 已下发电压电流              */
    ST_CHARGING,    /* 充电中                      */
    ST_FINISH,      /* 充满                        */
    ST_FAULT,       /* 故障                        */
} charge_state_t;

/* 电池规格表条目 */
typedef struct {
    uint16_t v_min_dV;   /* 电压识别窗口下限 (0.1V) */
    uint16_t v_max_dV;   /* 上限                    */
    uint16_t v_set_dV;   /* 设定充电电压            */
    uint16_t i_set_dA;   /* 设定充电电流 (0.1A)     */
    const char *name;
} battery_spec_t;

/* 业务任务 - 启动后内部驻留, 不返回 */
void charger_fsm_task(void *arg);

/* 供 UI 读取当前状态 */
charge_state_t  charger_fsm_state(void);
const dev_info_t *charger_fsm_chg_info(void);
const dev_info_t *charger_fsm_psu_info(void);

#ifdef __cplusplus
}
#endif
