#include "stage2_input.hpp"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#ifndef STAGETWO_BOOT_BUTTON_GPIO
#define STAGETWO_BOOT_BUTTON_GPIO GPIO_NUM_0
#endif

static const char* TAG = "stage2_input";

namespace stage2_input {

static bool s_last_pressed = false;
static int64_t s_press_start_us = 0;
static int64_t s_last_release_us = 0;
static int s_tap_count = 0;

void init()
{
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = 1ULL << STAGETWO_BOOT_BUTTON_GPIO;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);
    ESP_LOGI(TAG, "BOOT button GPIO initialized");
}

bool is_boot_button_held()
{
    return gpio_get_level((gpio_num_t)STAGETWO_BOOT_BUTTON_GPIO) == 0;
}

uint32_t held_ms()
{
    if (!is_boot_button_held() || s_press_start_us == 0) return 0;
    return (uint32_t)((esp_timer_get_time() - s_press_start_us) / 1000);
}

ButtonEvent poll_button()
{
    const int64_t now = esp_timer_get_time();
    const bool pressed = is_boot_button_held();

    if (pressed && !s_last_pressed) {
        s_press_start_us = now;
        s_last_pressed = true;
        return ButtonEvent::HoldActive;
    }

    if (pressed && s_last_pressed) {
        if ((now - s_press_start_us) >= 5000000) {
            return ButtonEvent::Hold5s;
        }
        return ButtonEvent::HoldActive;
    }

    if (!pressed && s_last_pressed) {
        const int64_t duration = now - s_press_start_us;
        s_last_pressed = false;
        s_press_start_us = 0;

        if (duration > 800000) {
            return ButtonEvent::LongPress;
        }

        if (now - s_last_release_us < 450000) {
            s_tap_count++;
        } else {
            s_tap_count = 1;
        }
        s_last_release_us = now;

        if (s_tap_count >= 2) {
            s_tap_count = 0;
            return ButtonEvent::DoublePress;
        }

        return ButtonEvent::ShortPress;
    }

    return ButtonEvent::None;
}

}
