idf_component_register(
    SRCS
        "main.c"
        "rs485.c"
        "modbus.c"
        "charger_fsm.c"
        "ui_oled.c"
        "io_ctrl.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_adc esp_timer
    PRIV_REQUIRES u8g2-hal-esp-idf
)
