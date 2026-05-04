#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_sh8601.h"

spi_bus_config_t genesis_ws_sh8601_buscfg(gpio_num_t sclk,
                                          gpio_num_t d0,
                                          gpio_num_t d1,
                                          gpio_num_t d2,
                                          gpio_num_t d3,
                                          int max_trans_sz)
{
    const spi_bus_config_t cfg = SH8601_PANEL_BUS_QSPI_CONFIG(sclk, d0, d1, d2, d3, max_trans_sz);
    return cfg;
}

esp_lcd_panel_io_spi_config_t genesis_ws_sh8601_iocfg(gpio_num_t cs,
                                                      esp_lcd_panel_io_color_trans_done_cb_t cb,
                                                      void* cb_ctx)
{
    const esp_lcd_panel_io_spi_config_t cfg = SH8601_PANEL_IO_QSPI_CONFIG(cs, cb, cb_ctx);
    return cfg;
}

