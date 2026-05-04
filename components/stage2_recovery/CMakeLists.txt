idf_component_register(
    SRCS "stage2_recovery.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_partition nvs_flash app_update esp_wifi fatfs lvgl__lvgl
    PRIV_REQUIRES stage2_ui stage2_input stage2_repair stage2_ota stage2_logging genesis_theme genesis_display
)
