#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "stage2_boot.hpp"
#include "stage2_input.hpp"
#include "stage2_ui.hpp"
#include "stage2_recovery.hpp"
#include "stage2_nvs.hpp"
#include "stage2_logging.hpp"
#include "stage2_fs.hpp"
#include "genesis_display.hpp"
#include "genesis_touch.hpp"
#include "genesis_sdcard.hpp"
#include "genesis_theme.hpp"
#include "esp_log.h"

extern "C" void app_main(void)
{
    stage2_logging::init();
    stage2_logging::info("GenesisOS StageTwo starting");

    // Reduce noisy peripheral logs during early boot UI; StageTwo handles failures explicitly.
    esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);
    esp_log_level_set("FT5x06", ESP_LOG_NONE);

    stage2_nvs::init();
    (void)stage2_fs::mount_internal();
    genesis_theme::init();

    genesis_display::init();
    genesis_touch::init();

    stage2_input::init();
    stage2_ui::init();

    const bool nvs_recovery = stage2_nvs::recovery_requested();

    stage2_ui::SplashResult splash_result = stage2_ui::show_splash_and_detect_boot_request();

    // SD card is optional; defer mount until after the splash window so any SD/MMC allocation or
    // bus bring-up issues can't destabilize the splash/boot-menu interaction.
    genesis_sdcard::init_optional();

    if (nvs_recovery) {
        stage2_logging::warn("Recovery requested by NVS flag");
        stage2_nvs::clear_recovery_requested();
        stage2_recovery::run();
    }

    switch (splash_result) {
        case stage2_ui::SplashResult::OpenBootMenu:
            stage2_ui::show_boot_menu();
            break;

        case stage2_ui::SplashResult::OpenRecovery:
            stage2_recovery::run();
            break;

        case stage2_ui::SplashResult::BootGenesisOS:
        default:
            stage2_boot::boot_genesisos();
            break;
    }

    while (true) {
        stage2_ui::tick();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
