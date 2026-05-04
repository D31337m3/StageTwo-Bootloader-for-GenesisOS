idf_component_register(
    SRCS "../../main/genesis_boot.c" "../../main/genesis_boot_menu.c"
    INCLUDE_DIRS "." "../../main"
    REQUIRES genesis_drivers nvs_flash lvgl genesis_recovery genesis_setup genesis_screensaver genesis_settings
)
