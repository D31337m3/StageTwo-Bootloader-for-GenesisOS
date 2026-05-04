idf_component_register(
    SRCS "stage2_ui.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_partition nvs_flash app_update esp_wifi fatfs esp_timer lvgl__lvgl esp_app_format
    PRIV_REQUIRES stage2_input stage2_boot stage2_recovery stage2_ota stage2_repair stage2_logging stage2_wifi stage2_backup stage2_repartition genesis_theme genesis_display genesis_sdcard
)
