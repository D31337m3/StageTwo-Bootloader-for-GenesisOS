#pragma once
#include <stdbool.h>

typedef enum {
    GENESIS_BOOT_MENU_NORMAL = 0,
    GENESIS_BOOT_MENU_MICROPY_RECOVERY = 1,
} genesis_boot_menu_result_t;

genesis_boot_menu_result_t genesis_boot_menu_show(void);

// Get Python auto-start state from NVS
bool genesis_python_get_auto_start_state(void);
