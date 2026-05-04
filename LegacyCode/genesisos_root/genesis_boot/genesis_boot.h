#pragma once

#include "esp_err.h"
#include "genesis_boot_menu.h"

esp_err_t genesis_boot_animation_start(genesis_boot_menu_result_t *boot_result);
void genesis_boot_log(const char *fmt, ...);
