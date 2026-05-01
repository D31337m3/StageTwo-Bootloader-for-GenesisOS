#include "stage2_ui.hpp"
#include "stage2_input.hpp"
#include "stage2_boot.hpp"
#include "stage2_recovery.hpp"
#include "stage2_ota.hpp"
#include "stage2_repair.hpp"
#include "stage2_logging.hpp"
#include "genesis_theme.hpp"
#include "genesis_display.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"

#ifndef STAGETWO_BUILD_ID
#define STAGETWO_BUILD_ID "dev-000001"
#endif

static const char* TAG = "stage2_ui";

namespace stage2_ui {

static lv_obj_t* s_current_screen = nullptr;
static lv_obj_t* s_confirmation_label = nullptr;
static lv_obj_t* s_progress_bar = nullptr;
static lv_obj_t* s_countdown_label = nullptr;
static bool s_confirmation_result = false;
static bool s_in_confirmation = false;

static void style_screen(lv_obj_t* screen)
{
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x050512), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
}

static void style_label(lv_obj_t* label, uint32_t color, lv_font_t* font)
{
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_align(label, LV_ALIGN_CENTER, 0);
}

static void style_button(lv_obj_t* btn, uint32_t bg_color)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_color), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_hor(btn, 20, 0);
    lv_obj_set_style_pad_ver(btn, 12, 0);
}

static void style_button_selected(lv_obj_t* btn)
{
    lv_obj_set_style_border_color(btn, lv_color_hex(0x42D9FF), 0);
    lv_obj_set_style_border_width(btn, 2, 0);
}

void init()
{
    ESP_LOGI(TAG, "Initializing StageTwo UI");
    style_screen(lv_scr_act());
}

void tick()
{
    genesis_display::flush();
}

void destroy_current_screen()
{
    if (s_current_screen) {
        lv_obj_del(s_current_screen);
        s_current_screen = nullptr;
    }
}

void create_splash_screen()
{
    destroy_current_screen();
    s_current_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_current_screen, LV_PCT(100), LV_PCT(100));
    style_screen(s_current_screen);
    lv_scr_load(s_current_screen);

    // Animated "GenesisOS" title only
    lv_obj_t* title = lv_label_create(s_current_screen);
    lv_label_set_text(title, "GenesisOS");
    lv_obj_set_pos(title, 80, 160);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x42D9FF), 0);
    lv_obj_set_style_align(title, LV_ALIGN_CENTER, 0);

    // Version label
    lv_obj_t* version = lv_label_create(s_current_screen);
    lv_label_set_text_fmt(version, "Build: %s", STAGETWO_BUILD_ID);
    lv_obj_set_pos(version, 80, 250);
    lv_obj_set_style_text_font(version, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(version, lv_color_hex(0x888899), 0);
    lv_obj_set_style_align(version, LV_ALIGN_CENTER, 0);
}

SplashResult show_splash_and_detect_boot_request()
{
    ESP_LOGI(TAG, "Showing splash. Build=%s", STAGETWO_BUILD_ID);

    create_splash_screen();

    const int64_t start = esp_timer_get_time();
    bool hold5_fired = false;
    int color_phase = 0;

    while ((esp_timer_get_time() - start) < 5500000) {
        auto ev = stage2_input::poll_button();

        if (ev == stage2_input::ButtonEvent::DoublePress) {
            ESP_LOGI(TAG, "Double tap detected: opening StageTwo menu");
            return SplashResult::OpenBootMenu;
        }

        if (ev == stage2_input::ButtonEvent::Hold5s && !hold5_fired) {
            hold5_fired = true;
            ESP_LOGI(TAG, "5s hold detected: opening recovery");
            return SplashResult::OpenRecovery;
        }

        // Animate title color
        if (s_current_screen) {
            lv_obj_t* title = lv_obj_get_child(s_current_screen, 0);
            if (title) {
                color_phase = (color_phase + 1) % 360;
                uint8_t r = 0x42 + (sinf(color_phase * 3.14159f / 180) * 0x20);
                uint8_t g = 0xD9 + (sinf(color_phase * 3.14159f / 180) * 0x20);
                uint8_t b = 0xFF;
                lv_obj_set_style_text_color(title, lv_color_hex((r << 16) | (g << 8) | b), 0);
            }
        }

        tick();
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    return SplashResult::BootGenesisOS;
}

void create_boot_menu()
{
    destroy_current_screen();
    s_current_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_current_screen, LV_PCT(100), LV_PCT(100));
    style_screen(s_current_screen);
    lv_scr_load(s_current_screen);

    // Title "StageTwo" - left aligned
    lv_obj_t* title_stage = lv_label_create(s_current_screen);
    lv_label_set_text(title_stage, "Stage");
    lv_obj_set_pos(title_stage, 20, 20);
    lv_obj_set_style_text_font(title_stage, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title_stage, lv_color_hex(0x42D9FF), 0);

    // "Two" in purple
    lv_obj_t* title_two = lv_label_create(s_current_screen);
    lv_label_set_text(title_two, "Two");
    lv_obj_set_pos(title_two, 110, 20);
    lv_obj_set_style_text_font(title_two, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title_two, lv_color_hex(0x9B5CFF), 0);

    // Build version - right aligned
    lv_obj_t* build_label = lv_label_create(s_current_screen);
    lv_label_set_text_fmt(build_label, "Build: %s", STAGETWO_BUILD_ID);
    lv_obj_set_pos(build_label, 280, 20);
    lv_obj_set_style_text_font(build_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(build_label, lv_color_hex(0x888899), 0);
    lv_obj_set_style_align(build_label, LV_ALIGN_TOP_RIGHT, 0);
}

void show_boot_menu()
{
    ESP_LOGI(TAG, "StageTwo menu");
    create_boot_menu();

    const char* items[] = {
        "Boot GenesisOS",
        "Recovery Mode",
        "Install Saved Update",
        "Download Update",
        "Advanced"
    };

    int selected = 0;
    const int64_t start = esp_timer_get_time();

    // Create menu item buttons
    lv_obj_t* buttons[5] = {nullptr};
    for (int i = 0; i < 5; i++) {
        buttons[i] = lv_btn_create(s_current_screen);
        lv_obj_set_pos(buttons[i], 40, 80 + i * 60);
        lv_obj_set_size(buttons[i], 288, 50);

        lv_obj_t* lbl = lv_label_create(buttons[i]);
        lv_label_set_text(lbl, items[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xF4F7FF), 0);
        lv_obj_set_style_align(lbl, LV_ALIGN_CENTER, 0);

        style_button(buttons[i], 0x1A1A2E);
    }

    // Highlight selected
    auto update_selection = [&]() {
        for (int i = 0; i < 5; i++) {
            if (i == selected) {
                style_button_selected(buttons[i]);
                lv_obj_set_style_bg_color(buttons[i], lv_color_hex(0x2A2A4E), 0);
            } else {
                lv_obj_set_style_border_width(buttons[i], 0, 0);
                lv_obj_set_style_bg_color(buttons[i], lv_color_hex(0x1A1A2E), 0);
            }
        }
    };
    update_selection();

    while ((esp_timer_get_time() - start) < 10000000) {
        auto ev = stage2_input::poll_button();

        if (ev == stage2_input::ButtonEvent::ShortPress) {
            selected = (selected + 1) % 5;
            update_selection();
            ESP_LOGI(TAG, "Selected: %s", items[selected]);
        }

        if (ev == stage2_input::ButtonEvent::LongPress) {
            break;
        }

        tick();
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    ESP_LOGI(TAG, "Executing menu item: %s", items[selected]);

    switch (selected) {
        case 0: stage2_boot::boot_genesisos(); break;
        case 1: stage2_recovery::run(); break;
        case 2: stage2_ota::install_saved_update(); break;
        case 3: stage2_ota::download_update_flow(); break;
        case 4: show_advanced_menu(); break;
    }
}

void create_advanced_menu()
{
    destroy_current_screen();
    s_current_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_current_screen, LV_PCT(100), LV_PCT(100));
    style_screen(s_current_screen);
    lv_scr_load(s_current_screen);

    // Title
    lv_obj_t* title = lv_label_create(s_current_screen);
    lv_label_set_text(title, "StageTwo Advanced");
    lv_obj_set_pos(title, 20, 20);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF4F7FF), 0);
}

void show_advanced_menu()
{
    ESP_LOGI(TAG, "StageTwo Advanced. Build=%s", STAGETWO_BUILD_ID);
    create_advanced_menu();

    const char* items[] = {
        "Format User Partitions",
        "Factory Reset",
        "Clear NVS",
        "Reset",
        "Back"
    };

    int selected = 0;

    // Create menu item buttons
    lv_obj_t* buttons[5] = {nullptr};
    for (int i = 0; i < 5; i++) {
        buttons[i] = lv_btn_create(s_current_screen);
        lv_obj_set_pos(buttons[i], 40, 80 + i * 60);
        lv_obj_set_size(buttons[i], 288, 50);

        lv_obj_t* lbl = lv_label_create(buttons[i]);
        lv_label_set_text(lbl, items[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xF4F7FF), 0);
        lv_obj_set_style_align(lbl, LV_ALIGN_CENTER, 0);

        style_button(buttons[i], 0x1A1A2E);
    }

    auto update_selection = [&]() {
        for (int i = 0; i < 5; i++) {
            if (i == selected) {
                style_button_selected(buttons[i]);
                lv_obj_set_style_bg_color(buttons[i], lv_color_hex(0x2A2A4E), 0);
            } else {
                lv_obj_set_style_border_width(buttons[i], 0, 0);
                lv_obj_set_style_bg_color(buttons[i], lv_color_hex(0x1A1A2E), 0);
            }
        }
    };
    update_selection();

    while (true) {
        auto ev = stage2_input::poll_button();

        if (ev == stage2_input::ButtonEvent::ShortPress) {
            selected = (selected + 1) % 5;
            update_selection();
            ESP_LOGI(TAG, "Advanced selected: %s", items[selected]);
        }

        if (ev == stage2_input::ButtonEvent::LongPress) {
            ESP_LOGI(TAG, "Executing advanced item: %s", items[selected]);

            switch (selected) {
                case 0:
                    if (confirm_destructive_action(
                        "Format User Partitions",
                        "This will erase user-writable storage.",
                        "Affected: user, userdata, python, config, logs, cache"
                    )) {
                        stage2_repair::format_user_partitions();
                    }
                    break;

                case 1:
                    if (confirm_destructive_action(
                        "Factory Reset",
                        "This clears user data and restores the latest stored GenesisOS image.",
                        "Affected: userdata, python, config, logs, safe NVS namespaces"
                    )) {
                        stage2_repair::factory_reset();
                    }
                    break;

                case 2:
                    if (confirm_destructive_action(
                        "Clear NVS",
                        "This clears safe GenesisOS runtime settings.",
                        "Namespaces: genesisos, genesis_runtime, wifi, user_config, app_state"
                    )) {
                        stage2_repair::clear_safe_nvs();
                    }
                    break;

                case 3:
                    esp_restart();
                    break;

                case 4:
                    show_boot_menu();
                    return;
            }

            // Recreate menu after action
            create_advanced_menu();
            for (int i = 0; i < 5; i++) {
                buttons[i] = lv_btn_create(s_current_screen);
                lv_obj_set_pos(buttons[i], 40, 80 + i * 60);
                lv_obj_set_size(buttons[i], 288, 50);

                lv_obj_t* lbl = lv_label_create(buttons[i]);
                lv_label_set_text(lbl, items[i]);
                lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
                lv_obj_set_style_text_color(lbl, lv_color_hex(0xF4F7FF), 0);
                lv_obj_set_style_align(lbl, LV_ALIGN_CENTER, 0);

                style_button(buttons[i], 0x1A1A2E);
            }
            update_selection();
        }

        tick();
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

void show_progress(const char* title, int percent)
{
    ESP_LOGI(TAG, "%s: %d%%", title, percent);
}

void show_error(const char* message)
{
    ESP_LOGE(TAG, "UI error: %s", message);
}

bool confirm_destructive_action(
    const char* title,
    const char* desc,
    const char* details
)
{
    ESP_LOGW(TAG, "Destructive action requested: %s", title);
    ESP_LOGW(TAG, "%s", desc);
    ESP_LOGW(TAG, "%s", details);
    ESP_LOGW(TAG, "Hold BOOT for 8 seconds to confirm. Release/no input cancels.");

    // Create confirmation dialog
    destroy_current_screen();
    s_current_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_current_screen, LV_PCT(100), LV_PCT(100));
    style_screen(s_current_screen);
    lv_scr_load(s_current_screen);

    // Title
    lv_obj_t* title_label = lv_label_create(s_current_screen);
    lv_label_set_text(title_label, title);
    lv_obj_set_pos(title_label, 0, 60);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFF3B3B), 0);
    lv_obj_set_style_align(title_label, LV_ALIGN_CENTER, 0);

    // Description
    lv_obj_t* desc_label = lv_label_create(s_current_screen);
    lv_label_set_text(desc_label, desc);
    lv_obj_set_pos(desc_label, 0, 110);
    lv_obj_set_style_text_font(desc_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(desc_label, lv_color_hex(0xF4F7FF), 0);
    lv_obj_set_style_align(desc_label, LV_ALIGN_CENTER, 0);

    // Details
    lv_obj_t* details_label = lv_label_create(s_current_screen);
    lv_label_set_text(details_label, details);
    lv_obj_set_pos(details_label, 0, 150);
    lv_obj_set_style_text_font(details_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(details_label, lv_color_hex(0x888899), 0);
    lv_obj_set_style_align(details_label, LV_ALIGN_CENTER, 0);

    // Progress bar
    s_progress_bar = lv_bar_create(s_current_screen);
    lv_obj_set_pos(s_progress_bar, 40, 220);
    lv_obj_set_size(s_progress_bar, 288, 20);
    lv_bar_set_range(s_progress_bar, 0, 100);
    lv_bar_set_value(s_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_opa(s_progress_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_progress_bar, 10, 0);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(0x2A2A4E), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);

    // Countdown label
    s_countdown_label = lv_label_create(s_current_screen);
    lv_label_set_text(s_countdown_label, "Hold BOOT: 8");
    lv_obj_set_pos(s_countdown_label, 0, 260);
    lv_obj_set_style_text_font(s_countdown_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_countdown_label, lv_color_hex(0xFFCC33), 0);
    lv_obj_set_style_align(s_countdown_label, LV_ALIGN_CENTER, 0);

    // Instruction
    lv_obj_t* instr_label = lv_label_create(s_current_screen);
    lv_label_set_text(instr_label, "Release to cancel");
    lv_obj_set_pos(instr_label, 0, 320);
    lv_obj_set_style_text_font(instr_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(instr_label, lv_color_hex(0x666677), 0);
    lv_obj_set_style_align(instr_label, LV_ALIGN_CENTER, 0);

    const int64_t idle_start = esp_timer_get_time();
    int64_t hold_start = 0;
    s_confirmation_result = false;

    while ((esp_timer_get_time() - idle_start) < 15000000) {
        const bool held = stage2_input::is_boot_button_held();

        if (held && hold_start == 0) {
            hold_start = esp_timer_get_time();
            stage2_logging::info("Destructive hold started");
        }

        if (!held && hold_start != 0) {
            stage2_logging::warn("Destructive hold interrupted; action aborted");
            s_confirmation_result = false;
            break;
        }

        if (held && hold_start != 0) {
            const int64_t elapsed = esp_timer_get_time() - hold_start;
            int percent = (int)((elapsed * 100) / 8000000);
            if (percent > 100) percent = 100;

            int remaining = 8 - (int)(elapsed / 1000000);
            if (remaining < 0) remaining = 0;

            lv_bar_set_value(s_progress_bar, percent, LV_ANIM_OFF);
            lv_label_set_text_fmt(s_countdown_label, "Hold BOOT: %d", remaining);

            if (elapsed >= 8000000) {
                stage2_logging::info("Destructive hold completed; action confirmed");
                s_confirmation_result = true;
                break;
            }
        }

        tick();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (!hold_start) {
        stage2_logging::warn("Destructive confirmation timed out; action aborted");
        s_confirmation_result = false;
    }

    return s_confirmation_result;
}

}
