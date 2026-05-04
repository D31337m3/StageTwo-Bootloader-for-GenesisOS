idf_component_register(
    SRCS "stage2_repartition.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_partition spi_flash bootloader_support
    PRIV_REQUIRES stage2_logging genesis_sdcard
)

