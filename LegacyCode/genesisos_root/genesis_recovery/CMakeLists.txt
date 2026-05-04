idf_component_register(
    SRCS "genesis_recovery.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash fatfs app_update
)
