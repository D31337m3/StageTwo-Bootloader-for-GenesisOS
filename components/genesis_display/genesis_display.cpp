#include "genesis_display.hpp"

#include <assert.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/i2c.h"
#include "driver/spi_master.h"

#include "esp_bit_defs.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_io_expander_tca9554.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_sh8601.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "genesis_display";

extern "C" spi_bus_config_t genesis_ws_sh8601_buscfg(gpio_num_t sclk,
                                                     gpio_num_t d0,
                                                     gpio_num_t d1,
                                                     gpio_num_t d2,
                                                     gpio_num_t d3,
                                                     int max_trans_sz);
extern "C" esp_lcd_panel_io_spi_config_t genesis_ws_sh8601_iocfg(gpio_num_t cs,
                                                                 esp_lcd_panel_io_color_trans_done_cb_t cb,
                                                                 void* cb_ctx);

// AXP2101 PMU (shared I2C bus)
#define AXP2101_ADDR_PRIMARY 0x34
#define AXP2101_ADDR_ALT     0x35

// AXP2101 registers (subset needed for early rail enable)
#define AXP_REG_STATUS1     0x00
#define AXP_REG_STATUS2     0x01
#define AXP_REG_ADC_CTRL    0x30
#define AXP_REG_TS_CTRL     0x50
#define AXP_REG_BAT_DET     0x68
#define AXP_REG_DC_ONOFF    0x80
#define AXP_REG_DC_VOL0     0x82
#define AXP_REG_LDO_ONOFF0  0x90
#define AXP_REG_LDO_VOL0    0x92
#define AXP_REG_FUEL_CTRL   0xA2

// Waveshare ESP32-S3-Touch-AMOLED-1.8 (manufacturer example wiring)
#define LCD_HOST SPI2_HOST
#define TOUCH_HOST I2C_NUM_0

#define PIN_LCD_CS (GPIO_NUM_12)
#define PIN_LCD_PCLK (GPIO_NUM_11)
#define PIN_LCD_DATA0 (GPIO_NUM_4)
#define PIN_LCD_DATA1 (GPIO_NUM_5)
#define PIN_LCD_DATA2 (GPIO_NUM_6)
#define PIN_LCD_DATA3 (GPIO_NUM_7)
#define PIN_LCD_RST (-1)

#define PIN_TOUCH_SCL (GPIO_NUM_14)
#define PIN_TOUCH_SDA (GPIO_NUM_15)

#define LCD_H_RES 368
#define LCD_V_RES 448

#if CONFIG_LV_COLOR_DEPTH == 32
#define LCD_BIT_PER_PIXEL (24)
#elif CONFIG_LV_COLOR_DEPTH == 16
#define LCD_BIT_PER_PIXEL (16)
#else
#error "Unsupported LV_COLOR_DEPTH"
#endif

#define LVGL_BUF_HEIGHT (LCD_V_RES / 4)
#define LVGL_TICK_PERIOD_MS 2

static lv_disp_draw_buf_t s_disp_buf;
static lv_disp_t* s_disp = nullptr;
static esp_lcd_panel_handle_t s_panel = NULL;
static esp_timer_handle_t s_lvgl_tick_timer = NULL;

static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x44, (uint8_t[]){0x01, 0xD1}, 2, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x51, (uint8_t[]){0x00}, 1, 10},
    {0x29, (uint8_t[]){0x00}, 0, 10},
    // Brightness: set explicitly after reset/startup. Board requires a non-max value to be visible.
    // 60% of 0xFF ~= 0x99.
    {0x51, (uint8_t[]){0x99}, 1, 0},
};

static const sh8601_vendor_config_t sh8601_vendor_config = {
    .init_cmds = lcd_init_cmds,
    .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
    .flags =
        {
            .use_qspi_interface = 1,
        },
};

static void increase_lvgl_tick(void* arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t* edata, void* user_ctx)
{
    (void)panel_io;
    (void)edata;
    lv_disp_drv_t* disp_driver = (lv_disp_drv_t*)user_ctx;
    lv_disp_flush_ready(disp_driver);
    return false;
}

static void lvgl_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)drv->user_data;
    const int offsetx1 = area->x1;
    const int offsetx2 = area->x2;
    const int offsety1 = area->y1;
    const int offsety2 = area->y2;

#if LCD_BIT_PER_PIXEL == 24
    // LVGL color -> panel BGR byte order fixup (matches manufacturer example).
    uint8_t* to = (uint8_t*)color_map;
    uint8_t temp = 0;
    uint16_t pixel_num = (offsetx2 - offsetx1 + 1) * (offsety2 - offsety1 + 1);

    temp = color_map[0].ch.blue;
    *to++ = color_map[0].ch.red;
    *to++ = color_map[0].ch.green;
    *to++ = temp;
    for (int i = 1; i < pixel_num; i++) {
        *to++ = color_map[i].ch.red;
        *to++ = color_map[i].ch.green;
        *to++ = color_map[i].ch.blue;
    }
#endif

    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
}

static void lvgl_update_cb(lv_disp_drv_t* drv)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)drv->user_data;
    switch (drv->rotated) {
        case LV_DISP_ROT_NONE:
            esp_lcd_panel_swap_xy(panel_handle, false);
            esp_lcd_panel_mirror(panel_handle, true, false);
            break;
        case LV_DISP_ROT_90:
            esp_lcd_panel_swap_xy(panel_handle, true);
            esp_lcd_panel_mirror(panel_handle, true, true);
            break;
        case LV_DISP_ROT_180:
            esp_lcd_panel_swap_xy(panel_handle, false);
            esp_lcd_panel_mirror(panel_handle, false, true);
            break;
        case LV_DISP_ROT_270:
            esp_lcd_panel_swap_xy(panel_handle, true);
            esp_lcd_panel_mirror(panel_handle, false, false);
            break;
    }
}

static void lvgl_rounder_cb(lv_disp_drv_t* disp_drv, lv_area_t* area)
{
    (void)disp_drv;
    uint16_t x1 = area->x1;
    uint16_t x2 = area->x2;
    uint16_t y1 = area->y1;
    uint16_t y2 = area->y2;
    area->x1 = (x1 >> 1) << 1;
    area->y1 = (y1 >> 1) << 1;
    area->x2 = ((x2 >> 1) << 1) + 1;
    area->y2 = ((y2 >> 1) << 1) + 1;
}

namespace genesis_display {

static esp_err_t axp_write(uint8_t axp_addr, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_write_to_device(TOUCH_HOST, axp_addr, buf, sizeof(buf), pdMS_TO_TICKS(100));
}

static esp_err_t axp_read(uint8_t axp_addr, uint8_t reg, uint8_t* out_val)
{
    if (!out_val) return ESP_ERR_INVALID_ARG;
    return i2c_master_write_read_device(TOUCH_HOST, axp_addr, &reg, 1, out_val, 1, pdMS_TO_TICKS(100));
}

static esp_err_t axp_set_bit(uint8_t axp_addr, uint8_t reg, uint8_t bit)
{
    uint8_t v = 0xFF;
    esp_err_t err = axp_read(axp_addr, reg, &v);
    if (err != ESP_OK) return err;
    v = (uint8_t)(v | (uint8_t)(1u << bit));
    return axp_write(axp_addr, reg, v);
}

static uint8_t detect_axp2101_addr()
{
    // Prefer a simple register read as the probe.
    uint8_t tmp = 0;
    if (axp_read(AXP2101_ADDR_PRIMARY, AXP_REG_STATUS1, &tmp) == ESP_OK) return AXP2101_ADDR_PRIMARY;
    if (axp_read(AXP2101_ADDR_ALT, AXP_REG_STATUS1, &tmp) == ESP_OK) return AXP2101_ADDR_ALT;
    return 0;
}

static void pmu_early_rails_init()
{
    const uint8_t axp_addr = detect_axp2101_addr();
    if (axp_addr == 0) {
        ESP_LOGW(TAG, "AXP2101 not detected; skipping PMU rail bring-up");
        return;
    }

    uint8_t s1 = 0xFF, s2 = 0xFF;
    (void)axp_read(axp_addr, AXP_REG_STATUS1, &s1);
    (void)axp_read(axp_addr, AXP_REG_STATUS2, &s2);
    ESP_LOGI(TAG, "AXP2101 detected (0x%02X) ST1=0x%02X ST2=0x%02X; enabling early rails", axp_addr, s1, s2);

    // Keep this minimal and board-proven: enable measurement + required rails.
    (void)axp_write(axp_addr, AXP_REG_ADC_CTRL, 0x0F);
    (void)axp_set_bit(axp_addr, AXP_REG_BAT_DET, 0);
    (void)axp_write(axp_addr, AXP_REG_TS_CTRL, 0x00);
    (void)axp_set_bit(axp_addr, AXP_REG_FUEL_CTRL, 3);
    (void)axp_set_bit(axp_addr, AXP_REG_FUEL_CTRL, 1);

    // ALDO1 = 1.8V (display analog / codec analog)
    (void)axp_write(axp_addr, AXP_REG_LDO_VOL0, 0x0D);
    (void)axp_set_bit(axp_addr, AXP_REG_LDO_ONOFF0, 0);

    // DLDO1 = 3.3V (peripherals)
    (void)axp_write(axp_addr, 0x93, 0x0F);
    (void)axp_set_bit(axp_addr, AXP_REG_LDO_ONOFF0, 1);

    // DLDO2 = 3.3V
    (void)axp_write(axp_addr, 0x94, 0x1A);
    (void)axp_set_bit(axp_addr, AXP_REG_LDO_ONOFF0, 2);

    // DLDO4 = 3.3V
    (void)axp_write(axp_addr, 0x96, 0x05);
    (void)axp_set_bit(axp_addr, AXP_REG_LDO_ONOFF0, 4);

    // DC1 = 3.3V
    (void)axp_write(axp_addr, AXP_REG_DC_VOL0, 0x1A);
    (void)axp_set_bit(axp_addr, AXP_REG_DC_ONOFF, 0);

    vTaskDelay(pdMS_TO_TICKS(20));
}

bool init()
{
    ESP_LOGI(TAG, "Display init (SH8601 QSPI + LVGL)");

    static lv_disp_drv_t disp_drv;

    // I2C for touch + TCA9554 expander
    i2c_config_t i2c_conf = {};
    i2c_conf.mode = I2C_MODE_MASTER;
    i2c_conf.sda_io_num = PIN_TOUCH_SDA;
    i2c_conf.scl_io_num = PIN_TOUCH_SCL;
    i2c_conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.master.clk_speed = 200 * 1000;
    i2c_conf.clk_flags = 0;
    ESP_ERROR_CHECK(i2c_param_config(TOUCH_HOST, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(TOUCH_HOST, i2c_conf.mode, 0, 0, 0));

    // Power rails must be enabled before expander-gated panel/touch enables.
    pmu_early_rails_init();

    esp_io_expander_handle_t io_expander = NULL;
    ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca9554(TOUCH_HOST, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &io_expander));

    // Power/reset gating (TCA9554 "EXIO" nets) must be driven in a safe order.
    // This mirrors the working flow captured in the legacy Genesis OS power-tree init.
    //
    // Notes (board-specific):
    // - EXIO7: SD CS (must be HIGH/inactive to avoid bus contention)
    // - EXIO0..2: reset/enable lines used during early boot (panel/touch power)
    ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_0, IO_EXPANDER_OUTPUT));
    ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_1, IO_EXPANDER_OUTPUT));
    ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_2, IO_EXPANDER_OUTPUT));
    ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_7, IO_EXPANDER_OUTPUT));

    // Assert safe state: hold resets/enables low, keep SD CS high.
    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_0, 0));
    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_1, 0));
    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_2, 0));
    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_7, 1));
    vTaskDelay(pdMS_TO_TICKS(200));

    // Release resets / enable panel power.
    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_0, 1));
    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_1, 1));
    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_2, 1));
    vTaskDelay(pdMS_TO_TICKS(120));

    // Initialize SPI bus (match Waveshare SH8601 QSPI bring-up macros)
    const spi_bus_config_t buscfg =
        genesis_ws_sh8601_buscfg(PIN_LCD_PCLK,
                                 PIN_LCD_DATA0,
                                 PIN_LCD_DATA1,
                                 PIN_LCD_DATA2,
                                 PIN_LCD_DATA3,
                                 LCD_H_RES * LCD_V_RES * LCD_BIT_PER_PIXEL / 8);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_config =
        genesis_ws_sh8601_iocfg(PIN_LCD_CS, notify_lvgl_flush_ready, &disp_drv);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    ESP_LOGI(TAG, "Install SH8601 panel driver");
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = (void*)&sh8601_vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io_handle, &panel_config, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    // LVGL
    lv_init();

    const esp_timer_create_args_t tick_args = {
        .callback = &increase_lvgl_tick,
        .name = "lvgl_tick",
    };
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &s_lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    // Follow the manufacturer example: draw buffers must be DMA-capable for the SPI panel IO path.
    lv_color_t* buf1 = (lv_color_t*)heap_caps_malloc(LCD_H_RES * LVGL_BUF_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_DMA);
    lv_color_t* buf2 = (lv_color_t*)heap_caps_malloc(LCD_H_RES * LVGL_BUF_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_DMA);
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "LVGL buffers alloc failed");
        return false;
    }
    lv_disp_draw_buf_init(&s_disp_buf, buf1, buf2, LCD_H_RES * LVGL_BUF_HEIGHT);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;
    disp_drv.ver_res = LCD_V_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.rounder_cb = lvgl_rounder_cb;
    disp_drv.drv_update_cb = lvgl_update_cb;
    disp_drv.draw_buf = &s_disp_buf;
    disp_drv.user_data = s_panel;
    s_disp = lv_disp_drv_register(&disp_drv);
    if (!s_disp) {
        ESP_LOGE(TAG, "lv_disp_drv_register failed");
        return false;
    }

    // Keep EXIO lines in the enabled state after LVGL comes up.

    ESP_LOGI(TAG, "Display ready");
    return true;
}

int width() { return LCD_H_RES; }
int height() { return LCD_V_RES; }
lv_disp_t* get_disp() { return s_disp; }

void flush()
{
    (void)lv_timer_handler();
}

}
