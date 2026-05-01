idf_component_register(
    SRCS "genesis_theme.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_partition nvs_flash app_update esp_wifi fatfs
)
