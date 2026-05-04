#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t stage2_wifi_init(void);
esp_err_t stage2_wifi_autoconnect(void);
bool stage2_wifi_is_connected(void);

// Builds a touch UI onto the given LVGL parent.
esp_err_t stage2_wifi_build(lv_obj_t* parent);

#ifdef __cplusplus
}
#endif
