idf_component_register(
    SRCS "stage2_fs.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_partition fatfs wear_levelling esp_system
)
