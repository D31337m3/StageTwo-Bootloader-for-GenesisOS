idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        stage2_boot
        stage2_input
        stage2_ui
        stage2_recovery
        stage2_nvs
        stage2_logging
        genesis_display
        genesis_touch
        genesis_sdcard
        genesis_theme
)
