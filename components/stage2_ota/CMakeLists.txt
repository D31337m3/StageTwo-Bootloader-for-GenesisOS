idf_component_register(
    SRCS "stage2_ota.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_partition nvs_flash app_update esp_wifi fatfs esp_http_client esp_https_ota mbedtls
    PRIV_REQUIRES stage2_ui stage2_logging stage2_nvs stage2_wifi genesis_theme genesis_display
)
