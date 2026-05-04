idf_component_register(
    SRCS "genesis_display.cpp" "sh8601_ws_cfg.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_partition nvs_flash app_update esp_wifi fatfs esp_timer lvgl__lvgl esp_lcd esp_lcd_sh8601 esp_io_expander_tca9554
)
