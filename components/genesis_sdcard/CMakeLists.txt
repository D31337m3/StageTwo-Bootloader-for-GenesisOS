idf_component_register(
    SRCS "genesis_sdcard.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver fatfs sdmmc esp_io_expander esp_io_expander_tca9554
)
