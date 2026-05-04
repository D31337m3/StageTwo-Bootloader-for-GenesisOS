idf_component_register(
    SRCS "stage2_wifi.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_event esp_netif esp_wifi freertos
    PRIV_REQUIRES lvgl__lvgl stage2_logging
)

