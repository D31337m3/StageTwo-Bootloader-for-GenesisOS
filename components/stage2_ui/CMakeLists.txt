idf_component_register(
    SRCS "stage2_ui.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_partition nvs_flash app_update esp_wifi fatfs
)
