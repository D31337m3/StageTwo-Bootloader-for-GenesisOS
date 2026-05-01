#include "genesis_touch.hpp"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "genesis_touch";

static lv_indev_t* s_indev = nullptr;
static Point s_last_point = {0, 0, false};

#ifndef GENESIS_TOUCH_I2C_PORT
#define GENESIS_TOUCH_I2C_PORT 0
#endif

#ifndef GENESIS_TOUCH_I2C_SDA
#define GENESIS_TOUCH_I2C_SDA 6
#endif

#ifndef GENESIS_TOUCH_I2C_SCL
#define GENESIS_TOUCH_I2C_SCL 7
#endif

#ifndef GENESIS_TOUCH_I2C_ADDR
#define GENESIS_TOUCH_I2C_ADDR 0x14
#endif

namespace genesis_touch {

static bool read_i2c(uint8_t* data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (GENESIS_TOUCH_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, len, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(GENESIS_TOUCH_I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return err == ESP_OK;
}

static void input_read_callback(lv_indev_t* indev, lv_indev_data_t* data)
{
    Point p = genesis_touch::read();
    data->point.x = p.x;
    data->point.y = p.y;
    data->state = p.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

bool init()
{
    ESP_LOGI(TAG, "Initializing touch input");

    // Initialize I2C for touch controller
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = (gpio_num_t)GENESIS_TOUCH_I2C_SDA;
    conf.scl_io_num = (gpio_num_t)GENESIS_TOUCH_I2C_SCL;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = 400000;
    conf.clk_flags = 0;

    esp_err_t err = i2c_param_config(GENESIS_TOUCH_I2C_PORT, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C config failed: %s", esp_err_to_name(err));
        return false;
    }

    err = i2c_driver_install(GENESIS_TOUCH_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(err));
        return false;
    }

    // Register LVGL input device
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = input_read_callback;

    s_indev = lv_indev_register(&indev_drv);
    if (!s_indev) {
        ESP_LOGE(TAG, "Failed to register LVGL input device");
        return false;
    }

    ESP_LOGI(TAG, "Touch input initialized");
    return true;
}

Point read()
{
    // TODO: Replace with actual touch controller read
    // This is a placeholder that returns no touch
    // Real implementation would read from FT6336U or similar
    uint8_t buf[6] = {0};
    
    if (read_i2c(buf, sizeof(buf))) {
        // Parse touch data based on controller protocol
        // Example for FT6336U:
        // buf[0] = status
        // buf[1] = x low
        // buf[2] = x high
        // buf[3] = y low
        // buf[4] = y high
        if (buf[0] & 0x40) {  // Touch detected
            s_last_point.x = ((buf[2] & 0x0F) << 8) | buf[1];
            s_last_point.y = ((buf[4] & 0x0F) << 8) | buf[3];
            s_last_point.pressed = true;
            return s_last_point;
        }
    }

    s_last_point.pressed = false;
    return s_last_point;
}

lv_indev_t* get_indev()
{
    return s_indev;
}

}
