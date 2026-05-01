#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "stage2_boot.hpp"
#include "stage2_input.hpp"
#include "stage2_ui.hpp"
#include "stage2_recovery.hpp"
#include "stage2_nvs.hpp"
#include "stage2_logging.hpp"
#include "genesis_display.hpp"
#include "genesis_touch.hpp"
#include "genesis_sdcard.hpp"
#include "genesis_theme.hpp"

extern "C" void app_main(void)
{
    stage2_logging::init();
    stage2_logging::info("GenesisOS StageTwo starting");

    stage2_nvs::init();
    genesis_theme::init();

    genesis_display::init();
    genesis_touch::init();
    genesis_sdcard::init_optional();

    stage2_input::init();
    stage2_ui::init();

    const bool nvs_recovery = stage2_nvs::recovery_requested();

    stage2_ui::SplashResult splash_result = stage2_ui::show_splash_and_detect_boot_request();

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
