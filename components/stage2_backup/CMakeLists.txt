idf_component_register(
    SRCS "stage2_backup.cpp"
    INCLUDE_DIRS "include"
    REQUIRES vfs fatfs
    PRIV_REQUIRES stage2_logging genesis_sdcard
)

