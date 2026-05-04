idf_component_register(
    SRCS "genesis_touch.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_partition nvs_flash app_update esp_wifi fatfs lvgl__lvgl esp_lcd_touch_ft5x06
)
