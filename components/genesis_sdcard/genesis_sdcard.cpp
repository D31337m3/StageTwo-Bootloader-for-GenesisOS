#include "genesis_sdcard.hpp"

#include <stdio.h>

#include "driver/i2c.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_io_expander.h"
#include "esp_io_expander_tca9554.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char* TAG = "genesis_sdcard";
static bool s_mounted = false;
static sdmmc_card_t* s_card = NULL;
static bool s_spi_bus_ready = false;

// Waveshare ESP32-S3-Touch-AMOLED-1.8 (matches genesis_hw_config.h in GenesisOS)
// SD uses SDSPI on SPI3 (SPI2 is reserved for the QSPI display).
#define SD_HOST SPI3_HOST
#define SD_MOSI GPIO_NUM_1
#define SD_MISO GPIO_NUM_3
#define SD_SCK GPIO_NUM_2

// SD chip-select is routed via TCA9554 IO expander pin 7 on this board.
// GenesisOS patches ESP-IDF's sdspi_host driver to treat this GPIO as a "virtual CS"
// and drive EXIO7 instead.
#ifndef GENESIS_SDSPI_VIRTUAL_CS_GPIO
#define GENESIS_SDSPI_VIRTUAL_CS_GPIO 39
#endif

// Shared I2C bus used for touch + IO expander (same as display init).
#define I2C_HOST I2C_NUM_0
#define I2C_SCL GPIO_NUM_14
#define I2C_SDA GPIO_NUM_15

static esp_io_expander_handle_t s_expander = NULL;

static esp_err_t ensure_expander_ready()
{
    if (s_expander) return ESP_OK;

    // The shared I2C bus is initialized by `genesis_display::init()` (touch + expander).
    // Avoid re-calling `i2c_driver_install()` here: doing so can produce "i2c driver install error"
    // logs and race the display/touch bring-up.
    return esp_io_expander_new_i2c_tca9554(I2C_HOST, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &s_expander);
}

static esp_err_t ensure_spi_bus_ready()
{
    if (s_spi_bus_ready) return ESP_OK;

    // Deselect SD before any traffic.
    esp_err_t err = ensure_expander_ready();
    if (err == ESP_OK) {
        (void)esp_io_expander_set_dir(s_expander, IO_EXPANDER_PIN_NUM_7, IO_EXPANDER_OUTPUT);
        (void)esp_io_expander_set_level(s_expander, IO_EXPANDER_PIN_NUM_7, 1);
    }

    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = SD_MOSI;
    bus_cfg.miso_io_num = SD_MISO;
    bus_cfg.sclk_io_num = SD_SCK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 16 * 1024;

    err = spi_bus_initialize(SD_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }

    s_spi_bus_ready = true;
    return ESP_OK;
}

namespace genesis_sdcard {

bool init_optional()
{
    if (s_mounted) return true;

    esp_err_t err = ensure_spi_bus_ready();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD SPI bus not ready: %s", esp_err_to_name(err));
        s_mounted = false;
        return false;
    }

    sdspi_device_config_t dev_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev_cfg.host_id = SD_HOST;
    dev_cfg.gpio_cs = (gpio_num_t)GENESIS_SDSPI_VIRTUAL_CS_GPIO; // drives EXIO7 via patched sdspi_host.c
    dev_cfg.gpio_cd = SDSPI_SLOT_NO_CD;
    dev_cfg.gpio_wp = SDSPI_SLOT_NO_WP;
    dev_cfg.gpio_int = SDSPI_SLOT_NO_INT;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    esp_vfs_fat_mount_config_t mount_cfg = {};
    mount_cfg.format_if_mount_failed = false;
    mount_cfg.max_files = 8;
    mount_cfg.allocation_unit_size = 16 * 1024;

    err = esp_vfs_fat_sdspi_mount("/sd", &host, &dev_cfg, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to mount /sd: %s", esp_err_to_name(err));
        s_mounted = false;
        return false;
    }

    ESP_LOGI(TAG, "SD mounted at /sd");
    sdmmc_card_print_info(stdout, s_card);
    s_mounted = true;
    return true;
}

bool mounted()
{
    return s_mounted;
}

const char* mount_path()
{
    return "/sd";
}

}
