#include "genesis_touch.hpp"

#include <assert.h>

#include "esp_err.h"
#include "esp_log.h"

#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_touch_ft5x06.h"

static const char* TAG = "genesis_touch";

// Waveshare ESP32-S3-Touch-AMOLED-1.8
#define TOUCH_HOST I2C_NUM_0
#define PIN_TOUCH_SCL (GPIO_NUM_14)
#define PIN_TOUCH_SDA (GPIO_NUM_15)
#define PIN_TOUCH_RST (GPIO_NUM_NC)
#define PIN_TOUCH_INT (GPIO_NUM_21)

static esp_lcd_touch_handle_t s_tp = NULL;
static esp_lcd_panel_io_handle_t s_tp_io = NULL;
static lv_indev_t* s_indev = NULL;
static lv_indev_drv_t s_indev_drv;
static bool s_indev_drv_inited = false;
static int s_i2c_read_fail_streak = 0;
static bool s_touch_disabled = false;

namespace genesis_touch {

static void lvgl_touch_cb(lv_indev_drv_t* drv, lv_indev_data_t* data)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)drv->user_data;
    assert(tp);

    uint16_t tp_x = 0;
    uint16_t tp_y = 0;
    uint8_t tp_cnt = 0;
    esp_err_t rd = esp_lcd_touch_read_data(tp);
    bool pressed = false;
    if (rd == ESP_OK) {
        pressed = esp_lcd_touch_get_coordinates(tp, &tp_x, &tp_y, NULL, &tp_cnt, 1);
        s_i2c_read_fail_streak = 0;
    } else {
        s_i2c_read_fail_streak++;
    }

    // If the touch I2C starts failing repeatedly, stop polling it.
    // This prevents log spam and avoids destabilizing early boot UI.
    if (s_i2c_read_fail_streak > 20 && !s_touch_disabled) {
        s_touch_disabled = true;
        if (s_indev) {
            lv_indev_enable(s_indev, false);
        }
        ESP_LOGW(TAG, "Touch disabled due to repeated I2C read failures");
    }
    if (pressed && tp_cnt > 0) {
        data->point.x = tp_x;
        data->point.y = tp_y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

bool init()
{
    // Display init owns bringing up the I2C bus + driver install.
    ESP_LOGI(TAG, "Initializing touch controller (FT5x06)");

    if (!s_tp_io) {
        // Avoid using ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG() here because the managed component's
        // designated initializer order can drift relative to the ESP-IDF struct layout and break
        // under C++ compilation. Populate fields explicitly instead.
        esp_lcd_panel_io_i2c_config_t io_config = {};
        io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS;
        io_config.control_phase_bytes = 1;
        io_config.dc_bit_offset = 0;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        io_config.flags.disable_control_phase = 1;
        // esp_lcd_panel_io_i2c legacy (v1) does not need (and may reject) scl_speed_hz here.
        io_config.scl_speed_hz = 0;
        esp_err_t err = esp_lcd_new_panel_io_i2c((uint32_t)TOUCH_HOST, &io_config, &s_tp_io);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_lcd_new_panel_io_i2c failed: %s", esp_err_to_name(err));
            return false;
        }
    }

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = 368,
        .y_max = 448,
        .rst_gpio_num = PIN_TOUCH_RST,
        .int_gpio_num = PIN_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
        .process_coordinates = NULL,
        .interrupt_callback = NULL,
        .user_data = NULL,
        .driver_data = NULL,
    };

    esp_err_t err = esp_lcd_touch_new_i2c_ft5x06(s_tp_io, &tp_cfg, &s_tp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_touch_new_i2c_ft5x06 failed: %s", esp_err_to_name(err));
        return false;
    }

    // LVGL stores only the pointer to the driver, so it must be static or heap allocated.
    if (!s_indev_drv_inited) {
        lv_indev_drv_init(&s_indev_drv);
        s_indev_drv_inited = true;
    }
    s_indev_drv.type = LV_INDEV_TYPE_POINTER;
    s_indev_drv.disp = lv_disp_get_default();
    s_indev_drv.read_cb = lvgl_touch_cb;
    s_indev_drv.user_data = s_tp;
    s_indev = lv_indev_drv_register(&s_indev_drv);

    if (!s_indev) {
        ESP_LOGE(TAG, "lv_indev_drv_register failed");
        return false;
    }

    ESP_LOGI(TAG, "Touch initialized");
    return true;
}

Point read()
{
    Point p = {0, 0, false};
    if (!s_tp) return p;

    uint16_t tp_x = 0;
    uint16_t tp_y = 0;
    uint8_t tp_cnt = 0;
    esp_lcd_touch_read_data(s_tp);
    bool pressed = esp_lcd_touch_get_coordinates(s_tp, &tp_x, &tp_y, NULL, &tp_cnt, 1);
    if (pressed && tp_cnt > 0) {
        p.x = tp_x;
        p.y = tp_y;
        p.pressed = true;
    }
    return p;
}

lv_indev_t* get_indev()
{
    return s_indev;
}

}
