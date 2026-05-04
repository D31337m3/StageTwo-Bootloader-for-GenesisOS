#include "stage2_ui.hpp"
#include "stage2_input.hpp"
#include "stage2_boot.hpp"
#include "stage2_recovery.hpp"
#include "stage2_ota.hpp"
#include "stage2_repair.hpp"
#include "stage2_logging.hpp"
#include "genesis_theme.hpp"
#include "genesis_display.hpp"
#include "genesis_sdcard.hpp"
#include "stage2_backup.hpp"
#include "stage2_repartition.hpp"
#include "stage2_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include <math.h>

#ifndef STAGETWO_BUILD_ID
#define STAGETWO_BUILD_ID "dev-000003"
#endif

static const char* TAG = "stage2_ui";

namespace stage2_ui {

static lv_obj_t* s_current_screen = nullptr;
static lv_obj_t* s_screen_to_delete = nullptr;
static lv_obj_t* s_progress_bar = nullptr;
static lv_obj_t* s_countdown_label = nullptr;
static bool s_confirmation_result = false;
static lv_obj_t* s_splash_genesis = nullptr;
static lv_obj_t* s_splash_progress = nullptr;
static lv_obj_t* s_menu_stage = nullptr;

static void show_backup_user_to_sd();
static void show_restore_user_from_sd();
static void show_repartition_from_sd();
static void show_about_modal();
static void show_message_wait(const char* title_text, const char* body_text, uint32_t body_color = 0x888899);

static uint32_t lerp_color(uint32_t a, uint32_t b, float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    const uint8_t ar = (a >> 16) & 0xFF;
    const uint8_t ag = (a >> 8) & 0xFF;
    const uint8_t ab = (a >> 0) & 0xFF;
    const uint8_t br = (b >> 16) & 0xFF;
    const uint8_t bg = (b >> 8) & 0xFF;
    const uint8_t bb = (b >> 0) & 0xFF;
    const uint8_t r = (uint8_t)(ar + (br - ar) * t);
    const uint8_t g = (uint8_t)(ag + (bg - ag) * t);
    const uint8_t b2 = (uint8_t)(ab + (bb - ab) * t);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b2;
}

static uint32_t brand_pulse_color(float phase01)
{
    static constexpr uint32_t kPalette[] = {
        0x42D9FF, // blue
        0x39FF14, // neon green
        0xFF4FD8, // pink
        0x9B5CFF, // purple
    };
    const int n = (int)(sizeof(kPalette) / sizeof(kPalette[0]));
    if (phase01 < 0.0f) phase01 = 0.0f;
    phase01 = phase01 - floorf(phase01);
    const float scaled = phase01 * n;
    const int idx = (int)scaled;
    const float t = scaled - idx;
    const uint32_t a = kPalette[idx % n];
    const uint32_t b = kPalette[(idx + 1) % n];
    return lerp_color(a, b, t);
}

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
    if (!btn) return;
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
    if (!btn) return;
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
    // Store the old screen for async deletion. Don't delete synchronously - it causes
    // LVGL internal state corruption when a new screen is loaded immediately after.
    // Async deletion allows LVGL to finish any pending operations first.
    if (s_screen_to_delete) {
        lv_obj_del_async(s_screen_to_delete);
    }
    s_screen_to_delete = s_current_screen;
    s_current_screen = nullptr;
}

void create_splash_screen()
{
    destroy_current_screen();
    s_splash_genesis = nullptr;
    s_splash_progress = nullptr;
    s_current_screen = lv_obj_create(NULL);
    if (!s_current_screen) {
        ESP_LOGE(TAG, "LVGL OOM creating splash screen");
        return;
    }
    lv_obj_set_size(s_current_screen, LV_PCT(100), LV_PCT(100));
    style_screen(s_current_screen);
    lv_scr_load(s_current_screen);

    // Branding: "GENESISOS" centered, with "OS" fixed purple and "GENESIS" pulsing.
    lv_obj_t* brand = lv_obj_create(s_current_screen);
    if (!brand) {
        ESP_LOGE(TAG, "LVGL OOM creating splash brand container");
        return;
    }
    lv_obj_set_size(brand, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(brand, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(brand, 0, 0);
    lv_obj_set_style_pad_all(brand, 0, 0);
    lv_obj_clear_flag(brand, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(brand, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brand, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(brand, LV_ALIGN_CENTER, 0, -70);

    s_splash_genesis = lv_label_create(brand);
    if (!s_splash_genesis) {
        ESP_LOGE(TAG, "LVGL OOM creating splash GENESIS label");
        return;
    }
    lv_label_set_text(s_splash_genesis, "GENESIS");
    lv_obj_set_style_text_font(s_splash_genesis, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_splash_genesis, lv_color_hex(0x42D9FF), 0);

    lv_obj_t* os = lv_label_create(brand);
    if (!os) {
        ESP_LOGE(TAG, "LVGL OOM creating splash OS label");
        return;
    }
    lv_label_set_text(os, "OS");
    lv_obj_set_style_text_font(os, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(os, lv_color_hex(0x9B5CFF), 0);

    // 5s countdown progress bar (color shifts with brand pulse)
    s_splash_progress = lv_bar_create(s_current_screen);
    if (!s_splash_progress) {
        ESP_LOGE(TAG, "LVGL OOM creating splash progress bar");
        return;
    }
    lv_obj_set_size(s_splash_progress, 288, 14);
    lv_obj_align(s_splash_progress, LV_ALIGN_CENTER, 0, 10);
    lv_bar_set_range(s_splash_progress, 0, 100);
    lv_bar_set_value(s_splash_progress, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_splash_progress, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_opa(s_splash_progress, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_splash_progress, 7, 0);
    lv_obj_set_style_bg_color(s_splash_progress, lv_color_hex(0x42D9FF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_splash_progress, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_splash_progress, 7, LV_PART_INDICATOR);

    // Version label
    lv_obj_t* version = lv_label_create(s_current_screen);
    if (!version) {
        ESP_LOGE(TAG, "LVGL OOM creating splash version label");
        return;
    }
    lv_label_set_text_fmt(version, "Build: %s", STAGETWO_BUILD_ID);
    lv_obj_set_style_text_font(version, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(version, lv_color_hex(0x888899), 0);
    lv_obj_align(version, LV_ALIGN_CENTER, 0, 40);
}

SplashResult show_splash_and_detect_boot_request()
{
    ESP_LOGI(TAG, "Showing splash. Build=%s", STAGETWO_BUILD_ID);

    create_splash_screen();

    const int64_t start = esp_timer_get_time();
    static constexpr int64_t kSplashUs = 5000000;

    while ((esp_timer_get_time() - start) < kSplashUs) {
        auto ev = stage2_input::poll_button();

        if (ev == stage2_input::ButtonEvent::ShortPress) {
            ESP_LOGI(TAG, "BOOT short press detected: opening StageTwo menu");
            return SplashResult::OpenBootMenu;
        }

        // Brand + progress animation
        const int64_t elapsed_us = esp_timer_get_time() - start;
        const float phase01 = (float)elapsed_us / 900000.0f; // ~0.9s per palette cycle
        const uint32_t pulse = brand_pulse_color(phase01);
        if (s_splash_genesis) {
            lv_obj_set_style_text_color(s_splash_genesis, lv_color_hex(pulse), 0);
        }
        if (s_splash_progress) {
            const int percent = (int)((elapsed_us * 100) / kSplashUs);
            lv_bar_set_value(s_splash_progress, percent > 100 ? 100 : percent, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(s_splash_progress, lv_color_hex(pulse), LV_PART_INDICATOR);
        }

        tick();
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    return SplashResult::BootGenesisOS;
}

void create_boot_menu()
{
    destroy_current_screen();
    s_menu_stage = nullptr;
    s_current_screen = lv_obj_create(NULL);
    if (!s_current_screen) {
        ESP_LOGE(TAG, "LVGL OOM creating boot menu screen");
        return;
    }
    lv_obj_set_size(s_current_screen, LV_PCT(100), LV_PCT(100));
    style_screen(s_current_screen);
    lv_scr_load(s_current_screen);

    // Header: StageTwo (top-right). "Stage" uses the brand pulse, "Two" is fixed purple.
    lv_obj_t* header = lv_obj_create(s_current_screen);
    if (!header) {
        ESP_LOGE(TAG, "LVGL OOM creating menu header");
        return;
    }
    lv_obj_set_size(header, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(header, LV_ALIGN_TOP_RIGHT, -16, 14);

    s_menu_stage = lv_label_create(header);
    if (!s_menu_stage) {
        ESP_LOGE(TAG, "LVGL OOM creating menu Stage label");
        return;
    }
    lv_label_set_text(s_menu_stage, "Stage");
    lv_obj_set_style_text_font(s_menu_stage, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_menu_stage, lv_color_hex(0x42D9FF), 0);

    lv_obj_t* title_two = lv_label_create(header);
    if (!title_two) {
        ESP_LOGE(TAG, "LVGL OOM creating menu Two label");
        return;
    }
    lv_label_set_text(title_two, "Two");
    lv_obj_set_style_text_font(title_two, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title_two, lv_color_hex(0x9B5CFF), 0);

    // Build version (top-left)
    lv_obj_t* build_label = lv_label_create(s_current_screen);
    if (!build_label) {
        ESP_LOGE(TAG, "LVGL OOM creating menu build label");
        return;
    }
    lv_label_set_text_fmt(build_label, "Build: %s", STAGETWO_BUILD_ID);
    lv_obj_align(build_label, LV_ALIGN_TOP_LEFT, 16, 14);
    lv_obj_set_style_text_font(build_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(build_label, lv_color_hex(0x888899), 0);
}

static void show_about_modal()
{
    lv_obj_t* overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* panel = lv_obj_create(overlay);
    lv_obj_set_size(panel, 328, 360);
    lv_obj_set_pos(panel, 20, 60);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x0B0B18), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 14, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x2A2A4E), 0);
    lv_obj_set_style_pad_all(panel, 12, 0);

    lv_obj_t* title = lv_label_create(panel);
    lv_label_set_text(title, "About StageTwo");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF4F7FF), 0);
    lv_obj_set_pos(title, 0, 0);

    const esp_app_desc_t* desc = esp_app_get_description();

    lv_obj_t* info = lv_label_create(panel);
    lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(info, 304);
    lv_obj_set_pos(info, 0, 32);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(info, lv_color_hex(0x888899), 0);
    lv_label_set_text_fmt(
        info,
        "Build ID: %s\n"
        "Version: %s\n"
        "IDF: %s\n"
        "Built: %s %s\n"
        "\n"
        "Credits:\n"
        "- Waveshare ESP32-S3-Touch-AMOLED-1.8\n"
        "- ESP-IDF + LVGL\n"
        "- GenesisOS project\n",
        STAGETWO_BUILD_ID,
        desc ? desc->version : "?",
        desc ? desc->idf_ver : "?",
        desc ? desc->date : "?",
        desc ? desc->time : "?");

    lv_obj_t* hint = lv_label_create(panel);
    lv_label_set_text(hint, "Hold BOOT to close");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x42D9FF), 0);
    lv_obj_set_pos(hint, 0, 290);

    // Marquee dedication inside the modal.
    lv_obj_t* marquee = lv_label_create(panel);
    lv_obj_set_width(marquee, 304);
    lv_obj_set_pos(marquee, 0, 316);
    lv_obj_set_style_text_font(marquee, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(marquee, lv_color_hex(0x9B5CFF), 0);
    lv_label_set_long_mode(marquee, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(
        marquee,
        "This project is Dedicated to My Two Sons, TheoZerah and ZaiyEden, and their beautiful Mother Tiara \xE2\x9D\xA4");

    while (true) {
        auto ev = stage2_input::poll_button();
        if (ev == stage2_input::ButtonEvent::Hold5s || ev == stage2_input::ButtonEvent::LongPress) {
            break;
        }
        tick();
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    lv_obj_del(overlay);
}

void show_boot_menu()
{
    ESP_LOGI(TAG, "StageTwo menu");

    while (true) {
        create_boot_menu();
        if (!s_current_screen) {
            // If we OOM'd, just wait and retry rather than crashing.
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        const char* items[] = {
            "Boot GenesisOS",
            "Recovery Mode",
            "Install Saved Update",
            "Download Update",
            "Advanced",
            "About"
        };

        int selected = 0;
        const int64_t start = esp_timer_get_time();
        bool touch_clicked = false;

        // Create menu item buttons
        constexpr int item_count = (int)(sizeof(items) / sizeof(items[0]));
        lv_obj_t* buttons[item_count] = {nullptr};
        for (int i = 0; i < item_count; i++) {
            buttons[i] = lv_btn_create(s_current_screen);
            if (!buttons[i]) {
                ESP_LOGE(TAG, "LVGL OOM creating menu button %d/%d", i + 1, item_count);
                break;
            }
            lv_obj_set_pos(buttons[i], 40, 70 + i * 52);
            lv_obj_set_size(buttons[i], 288, 44);
            lv_obj_set_user_data(buttons[i], (void*)(intptr_t)i);

            lv_obj_t* lbl = lv_label_create(buttons[i]);
            if (!lbl) {
                ESP_LOGE(TAG, "LVGL OOM creating menu label %d/%d", i + 1, item_count);
                break;
            }
            lv_label_set_text(lbl, items[i]);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xF4F7FF), 0);
            lv_obj_set_style_align(lbl, LV_ALIGN_CENTER, 0);

            style_button(buttons[i], 0x1A1A2E);
        }

        // Highlight selected
        auto update_selection = [&]() {
            for (int i = 0; i < item_count; i++) {
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

        struct MenuClickCtx {
            int* selected_ptr;
            bool* execute_ptr;
        };
        MenuClickCtx click_ctx = {&selected, &touch_clicked};

        auto on_menu_click = [](lv_event_t* e) {
            auto* ctx = (MenuClickCtx*)lv_event_get_user_data(e);
            lv_obj_t* target = lv_event_get_target(e);
            if (!ctx || !ctx->selected_ptr || !ctx->execute_ptr || !target) return;
            *ctx->selected_ptr = (int)(intptr_t)lv_obj_get_user_data(target);
            *ctx->execute_ptr = true;
        };

        for (int i = 0; i < item_count; i++) {
            if (buttons[i]) {
                lv_obj_add_event_cb(buttons[i], on_menu_click, LV_EVENT_CLICKED, &click_ctx);
            }
        }

        while ((esp_timer_get_time() - start) < 10000000) {
            auto ev = stage2_input::poll_button();

            if (ev == stage2_input::ButtonEvent::ShortPress) {
                selected = (selected + 1) % item_count;
                update_selection();
                ESP_LOGI(TAG, "Selected: %s", items[selected]);
            }

            if (ev == stage2_input::ButtonEvent::LongPress) {
                break;
            }

            // Animate header "Stage" using the same brand pulse as the splash.
            if (s_menu_stage) {
                const float phase01 = (float)(esp_timer_get_time() - start) / 900000.0f;
                const uint32_t pulse = brand_pulse_color(phase01);
                lv_obj_set_style_text_color(s_menu_stage, lv_color_hex(pulse), 0);
            }

            tick();

            if (touch_clicked) {
                break;
            }

            vTaskDelay(pdMS_TO_TICKS(30));
        }

        ESP_LOGI(TAG, "Executing menu item: %s", items[selected]);

        switch (selected) {
            case 0:
                if (!stage2_boot::has_bootable_genesis_partition()) {
                    show_message_wait(
                        "Boot Target Missing",
                        "No bootable GenesisOS partition found.\n\nRouting to Recovery...",
                        0xFF3B3B
                    );
                    stage2_recovery::run();
                    return;
                }
                if (!stage2_boot::boot_genesisos()) {
                    show_message_wait(
                        "Boot Failed",
                        "GenesisOS boot target check failed.\n\nRouting to Recovery...",
                        0xFF3B3B
                    );
                    stage2_recovery::run();
                    return;
                }
                return;
            case 1: stage2_recovery::run(); return;
            case 2: stage2_ota::install_saved_update(); return;
            case 3: stage2_ota::download_update_flow(); return;
            case 4: show_advanced_menu(); break; // return to main menu afterward
            case 5: show_about_modal(); break;   // return to main menu afterward
        }
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
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
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
        "Wi-Fi Setup",
        "Backup User -> SD",
        "Restore User <- SD",
        "Repartition (SD)",
        "Reset",
        "Back"
    };

    int selected = 0;
    bool touch_clicked = false;

    // Create menu item buttons
    constexpr int item_count = (int)(sizeof(items) / sizeof(items[0]));
    lv_obj_t* buttons[item_count] = {nullptr};
    for (int i = 0; i < item_count; i++) {
        buttons[i] = lv_btn_create(s_current_screen);
        lv_obj_set_pos(buttons[i], 40, 60 + i * 42);
        lv_obj_set_size(buttons[i], 288, 36);
        lv_obj_set_user_data(buttons[i], (void*)(intptr_t)i);

        lv_obj_t* lbl = lv_label_create(buttons[i]);
        lv_label_set_text(lbl, items[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xF4F7FF), 0);
        lv_obj_set_style_align(lbl, LV_ALIGN_CENTER, 0);

        style_button(buttons[i], 0x1A1A2E);
    }

    auto update_selection = [&]() {
        for (int i = 0; i < item_count; i++) {
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

    struct AdvancedClickCtx {
        int* selected_ptr;
        bool* execute_ptr;
    };
    AdvancedClickCtx click_ctx = {&selected, &touch_clicked};

    auto on_advanced_click = [](lv_event_t* e) {
        auto* ctx = (AdvancedClickCtx*)lv_event_get_user_data(e);
        lv_obj_t* target = lv_event_get_target(e);
        if (!ctx || !ctx->selected_ptr || !ctx->execute_ptr || !target) return;
        *ctx->selected_ptr = (int)(intptr_t)lv_obj_get_user_data(target);
        *ctx->execute_ptr = true;
    };

    for (int i = 0; i < item_count; i++) {
        if (buttons[i]) {
            lv_obj_add_event_cb(buttons[i], on_advanced_click, LV_EVENT_CLICKED, &click_ctx);
        }
    }

    while (true) {
        auto ev = stage2_input::poll_button();

        if (ev == stage2_input::ButtonEvent::ShortPress) {
            selected = (selected + 1) % item_count;
            update_selection();
            ESP_LOGI(TAG, "Advanced selected: %s", items[selected]);
        }

        if (ev == stage2_input::ButtonEvent::LongPress || touch_clicked) {
            touch_clicked = false;
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
                    show_wifi_setup();
                    break;

                case 4:
                    show_backup_user_to_sd();
                    break;

                case 5:
                    show_restore_user_from_sd();
                    break;

                case 6:
                    show_repartition_from_sd();
                    break;

                case 7:
                    esp_restart();
                    break;

                case 8:
                    return;
            }

            // Recreate menu after action
            create_advanced_menu();
            for (int i = 0; i < item_count; i++) {
                buttons[i] = lv_btn_create(s_current_screen);
                lv_obj_set_pos(buttons[i], 40, 60 + i * 42);
                lv_obj_set_size(buttons[i], 288, 36);
                lv_obj_set_user_data(buttons[i], (void*)(intptr_t)i);

                lv_obj_t* lbl = lv_label_create(buttons[i]);
                lv_label_set_text(lbl, items[i]);
                lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
                lv_obj_set_style_text_color(lbl, lv_color_hex(0xF4F7FF), 0);
                lv_obj_set_style_align(lbl, LV_ALIGN_CENTER, 0);

                style_button(buttons[i], 0x1A1A2E);
                lv_obj_add_event_cb(buttons[i], on_advanced_click, LV_EVENT_CLICKED, &click_ctx);
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

void show_wifi_setup()
{
    destroy_current_screen();
    s_current_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_current_screen, LV_PCT(100), LV_PCT(100));
    style_screen(s_current_screen);
    lv_scr_load(s_current_screen);

    lv_obj_t* title = lv_label_create(s_current_screen);
    lv_label_set_text(title, "Wi-Fi Setup");
    lv_obj_set_pos(title, 20, 20);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF4F7FF), 0);

    lv_obj_t* hint = lv_label_create(s_current_screen);
    lv_label_set_text(hint, "Hold BOOT to return");
    lv_obj_set_pos(hint, 20, 45);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888899), 0);

    (void)stage2_wifi_build(s_current_screen);

    while (true) {
        auto ev = stage2_input::poll_button();
        if (ev == stage2_input::ButtonEvent::LongPress) {
            break;
        }
        tick();
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

static void show_message_wait(const char* title_text, const char* body_text, uint32_t body_color)
{
    // Create screen and block until user holds BOOT.
    destroy_current_screen();
    s_current_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_current_screen, LV_PCT(100), LV_PCT(100));
    style_screen(s_current_screen);
    lv_scr_load(s_current_screen);

    lv_obj_t* title = lv_label_create(s_current_screen);
    lv_label_set_text(title, title_text);
    lv_obj_set_pos(title, 20, 20);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF4F7FF), 0);

    lv_obj_t* body = lv_label_create(s_current_screen);
    lv_label_set_text(body, body_text);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, 328);
    lv_obj_set_pos(body, 20, 70);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(body, lv_color_hex(body_color), 0);

    lv_obj_t* hint = lv_label_create(s_current_screen);
    lv_label_set_text(hint, "Hold BOOT to return");
    lv_obj_set_pos(hint, 20, 420);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666677), 0);

    while (true) {
        auto ev = stage2_input::poll_button();
        if (ev == stage2_input::ButtonEvent::LongPress) {
            break;
        }
        tick();
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

static void show_message(const char* title_text, const char* body_text, uint32_t body_color = 0x888899)
{
    // Create screen but do not block.
    destroy_current_screen();
    s_current_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_current_screen, LV_PCT(100), LV_PCT(100));
    style_screen(s_current_screen);
    lv_scr_load(s_current_screen);

    lv_obj_t* title = lv_label_create(s_current_screen);
    lv_label_set_text(title, title_text);
    lv_obj_set_pos(title, 20, 20);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF4F7FF), 0);

    lv_obj_t* body = lv_label_create(s_current_screen);
    lv_label_set_text(body, body_text);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, 328);
    lv_obj_set_pos(body, 20, 70);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(body, lv_color_hex(body_color), 0);
}

static void show_backup_user_to_sd()
{
    if (!genesis_sdcard::mounted()) {
        (void)genesis_sdcard::init_optional();
    }
    if (!genesis_sdcard::mounted()) {
        show_message_wait("Backup User -> SD", "No SD card mounted.\n\nInsert SD card and retry.", 0xFFCC33);
        return;
    }

    const char* path = "/sd/stage2/backups/user_backup.gosar";
    show_message("Backup User -> SD", "Backing up...\n\nFile:\n/sd/stage2/backups/user_backup.gosar");

    esp_err_t err = stage2_backup::backup_user_to_sd(path);
    if (err == ESP_OK) {
        show_message_wait("Backup Complete", "Backup written to:\n/sd/stage2/backups/user_backup.gosar", 0x20D676);
    } else {
        show_message_wait("Backup Failed", "Backup failed.\n\nCheck logs and SD card filesystem.", 0xFF3B3B);
    }
}

static void show_restore_user_from_sd()
{
    if (!genesis_sdcard::mounted()) {
        (void)genesis_sdcard::init_optional();
    }
    if (!genesis_sdcard::mounted()) {
        show_message_wait("Restore User <- SD", "No SD card mounted.\n\nInsert SD card and retry.", 0xFFCC33);
        return;
    }

    const char* path = "/sd/stage2/backups/user_backup.gosar";
    if (!confirm_destructive_action(
            "Restore /user from SD",
            "This will erase /user and restore from SD backup.",
            "Source: /sd/stage2/backups/user_backup.gosar")) {
        return;
    }

    show_message("Restore User <- SD", "Restoring...\n\nDestination:\n/user");
    esp_err_t err = stage2_backup::restore_user_from_sd(path);
    if (err == ESP_OK) {
        show_message_wait("Restore Complete", "Restore finished.\n\n/user has been replaced from SD backup.", 0x20D676);
    } else {
        show_message_wait("Restore Failed", "Restore failed.\n\nCheck backup file and logs.", 0xFF3B3B);
    }
}

static void show_repartition_from_sd()
{
    if (!genesis_sdcard::mounted()) {
        (void)genesis_sdcard::init_optional();
    }
    if (!genesis_sdcard::mounted()) {
        show_message_wait("Repartition (SD)", "No SD card mounted.\n\nInsert SD card and retry.", 0xFFCC33);
        return;
    }

    if (!confirm_destructive_action(
            "Repartition + Reflash",
            "This will overwrite GenesisOS slots and likely erase /user and /mpython.",
            "Tip: Run 'Backup User -> SD' first.\nPackage: /sd/stage2/repartition")) {
        return;
    }

    show_message(
        "Repartition (SD)",
        "Applying repartition package:\n"
        "/sd/stage2/repartition\n\n"
        "Required files:\n"
        "- partition-table.bin\n"
        "- genesisos.bin\n\n"
        "Do not power off.");

    esp_err_t err = stage2_repartition::apply_from_sd("/sd/stage2/repartition");
    if (err == ESP_OK) {
        show_message_wait("Repartition Complete", "Repartition + flash complete.\n\nRebooting now...", 0x20D676);
        esp_restart();
    } else {
        show_message_wait("Repartition Failed", "Repartition failed.\n\nCheck package files + logs.", 0xFF3B3B);
    }
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
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFF3B3B), 0);
    lv_obj_set_style_align(title_label, LV_ALIGN_CENTER, 0);

    // Description
    lv_obj_t* desc_label = lv_label_create(s_current_screen);
    lv_label_set_text(desc_label, desc);
    lv_obj_set_pos(desc_label, 0, 110);
    lv_obj_set_style_text_font(desc_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(desc_label, lv_color_hex(0xF4F7FF), 0);
    lv_obj_set_style_align(desc_label, LV_ALIGN_CENTER, 0);

    // Details
    lv_obj_t* details_label = lv_label_create(s_current_screen);
    lv_label_set_text(details_label, details);
    lv_obj_set_pos(details_label, 0, 150);
    lv_obj_set_style_text_font(details_label, &lv_font_montserrat_14, 0);
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
    lv_obj_set_style_text_font(s_countdown_label, &lv_font_montserrat_14, 0);
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
