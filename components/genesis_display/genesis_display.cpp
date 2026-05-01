#include "genesis_display.hpp"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "genesis_display";

static lv_disp_t* s_disp = nullptr;
static lv_disp_draw_buf_t s_draw_buf;
static color_t s_fb[2][368 * 448] __attribute__((aligned(16)));

namespace genesis_display {

static void flush_callback(lv_disp_t* disp, const lv_area_t* area, lv_color_t* color_p)
{
    // TODO: Replace with actual LCD panel flush
    // For now, just mark the flush as complete
    lv_disp_flush_ready(disp);
}

bool init()
{
    ESP_LOGI(TAG, "Initializing LVGL display");

    // Initialize LVGL
    lv_init();

    // Configure draw buffer
    lv_disp_draw_buf_init(&s_draw_buf, s_fb[0], s_fb[1], 368 * 448);

    // Configure display driver
    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 368;
    disp_drv.ver_res = 448;
    disp_drv.draw_buf = &s_draw_buf;
    disp_drv.flush_cb = flush_callback;
    disp_drv.direct_mode = false;
    disp_drv.full_refresh = false;
    disp_drv.sw_rotate = false;

    s_disp = lv_disp_register(&disp_drv);
    if (!s_disp) {
        ESP_LOGE(TAG, "Failed to register display");
        return false;
    }

    ESP_LOGI(TAG, "LVGL display initialized: %dx%d", 368, 448);
    return true;
}

int width() { return 368; }
int height() { return 448; }
lv_disp_t* get_disp() { return s_disp; }

void flush()
{
    // Called from tick to process LVGL rendering
    if (s_disp) {
        lv_disp_t* disp = s_disp;
        lv_timer_handler();
    }
}

}
