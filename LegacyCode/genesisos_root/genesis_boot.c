#include "genesis_boot.h"
#include "genesis_display.h"
#include "genesis_boot_menu.h"
#include "genesis_setup.h"
#include "genesis_audio.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_random.h"

#define TAG "GENESIS_BOOT"

// External access to the display handle initialized in main.c
// We'll pass it in or use a singleton.
extern genesis_display_t *genesis_get_display(void);

#include "genesis_screensaver.h"
#include "genesis_settings.h"
#include <time.h>
#include <math.h>
#include <stdarg.h>

static lv_obj_t * boot_log_cont = NULL;

static void anim_opa_cb(void * var, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void anim_x_cb(void * var, int32_t v) {
    lv_obj_set_x((lv_obj_t *)var, (lv_coord_t)v);
}

void genesis_boot_log(const char *fmt, ...) {
    static bool in_log = false;
    if (in_log || boot_log_cont == NULL) return;
    
    in_log = true;
    if (!genesis_settings_is_dev_mode()) {
        in_log = false;
        return;
    }

    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    lv_obj_t * lbl = lv_label_create(boot_log_cont);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x00FF00), 0); // Matrix Green
    lv_obj_scroll_to_view(lbl, LV_ANIM_OFF);
    lv_refr_now(NULL);
    
    in_log = false;
}

esp_err_t genesis_boot_animation_start(genesis_boot_menu_result_t *boot_result) {
    if (boot_result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *boot_result = GENESIS_BOOT_MENU_NORMAL;

    lv_obj_t * scr = lv_scr_act();
    if (scr == NULL) {
        ESP_LOGE(TAG, "LVGL active screen is NULL. Display/driver likely not initialized yet.");
        return ESP_ERR_INVALID_STATE;
    }
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    ESP_LOGI(TAG, "Starting LVGL Boot Splash...");

    // 1. Check for Matrix Easter Egg (Dev Mode)
    if (genesis_settings_is_dev_mode()) {
        genesis_screensaver_start(SS_TYPE_MATRIX);
        for(int i=0; i<60; i++) { // ~3 seconds of matrix
            genesis_screensaver_update();
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        genesis_screensaver_stop();
        lv_obj_clean(scr); 

        // Create Verbose Log Container
        boot_log_cont = lv_obj_create(scr);
        lv_obj_set_size(boot_log_cont, 300, 150);
        lv_obj_align(boot_log_cont, LV_ALIGN_TOP_LEFT, 10, 10);
        lv_obj_set_style_bg_opa(boot_log_cont, LV_OPA_60, 0);
        lv_obj_set_style_bg_color(boot_log_cont, lv_color_hex(0x000000), 0);
        lv_obj_set_style_border_width(boot_log_cont, 1, 0);
        lv_obj_set_style_border_color(boot_log_cont, lv_color_hex(0x00FF00), 0);
        lv_obj_set_flex_flow(boot_log_cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(boot_log_cont, 5, 0);
    }

    // 1. Logo Text: "GenesisOS"
    lv_obj_t * logo_cont = lv_obj_create(scr);
    lv_obj_set_size(logo_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(logo_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(logo_cont, 0, 0);
    lv_obj_set_style_pad_all(logo_cont, 0, 0);
    lv_obj_center(logo_cont);
    lv_obj_set_flex_flow(logo_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_opa(logo_cont, 0, 0);

    lv_obj_t * genesis_lbl = lv_label_create(logo_cont);
    lv_label_set_text(genesis_lbl, "Genesis");
    lv_obj_set_style_text_font(genesis_lbl, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(genesis_lbl, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t * os_lbl = lv_label_create(logo_cont);
    lv_label_set_text(os_lbl, "OS");
    lv_obj_set_style_text_font(os_lbl, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(os_lbl, lv_color_hex(0xA020F0), 0);

    // 1.1 "Hybrid Development" Tagline (Starts hidden)
    lv_obj_t * hybrid_lbl = lv_label_create(scr);
    lv_label_set_text(hybrid_lbl, "Hybrid Development");
    lv_obj_set_style_text_font(hybrid_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hybrid_lbl, lv_color_hex(0x301934), 0); // Deep Purple
    lv_obj_set_style_opa(hybrid_lbl, 0, 0);
    // Align relative to logo_cont: below and offset right
    lv_obj_align_to(hybrid_lbl, logo_cont, LV_ALIGN_OUT_BOTTOM_RIGHT, -20, 5);

    // Ensure it's on top and visible
    lv_obj_move_to_index(logo_cont, -1);
    lv_obj_set_style_opa(logo_cont, 0, 0);

    // 1.5 Quick Brightness Ramp to ensure visibility
    genesis_display_t *dev = genesis_get_display();
    for (int b = 30; b <= 180; b += 10) {
        uint8_t val = (uint8_t)b;
        genesis_display_send_cmd(dev, 0x51, &val, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // 2. Main Splash Sequence (6 seconds of dynamic animation)
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, logo_cont);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_time(&a, 2000);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)anim_opa_cb);
    lv_anim_start(&a);

    lv_obj_t * pulse_bar = NULL;

    // Random accent color for this boot's pulse
    lv_color_t target_accent = lv_color_make(100 + (esp_random() % 155), 50 + (esp_random() % 100), 200 + (esp_random() % 55));
    lv_color_t base_white = lv_color_hex(0xFFFFFF);
    lv_color_t deep_purple = lv_color_hex(0x301934);
    lv_color_t bright_red = lv_color_hex(0xFF0000);

    // Startup audio
    genesis_audio_play_beep(880, 100);
    genesis_audio_play_mp3("/mpython/startup.mp3");

    for(int i=0; i<300; i++) {
        lv_timer_handler();

        // Breathing Effect for "Genesis" (Pulsing between White and Random Accent)
        // Frequency: ~2 seconds per breath
        float pulse = (sinf(i * 0.1f) + 1.0f) / 2.0f; 
        lv_color_t current_g = lv_color_mix(target_accent, base_white, (uint8_t)(pulse * 255));
        lv_obj_set_style_text_color(genesis_lbl, current_g, 0);

        // "Hybrid Development" sequence
        // Appear halfway through (at i=120), fade out by i=240
        if (i >= 120 && i < 240) {
            int progress = i - 120; // 0 to 120
            if (progress < 40) { // Fade in (Deep Purple)
                lv_obj_set_style_opa(hybrid_lbl, (progress * 255) / 40, 0);
                lv_obj_set_style_text_color(hybrid_lbl, deep_purple, 0);
            } else if (progress < 80) { // Stay visible and transition Purple -> Red
                lv_obj_set_style_opa(hybrid_lbl, 255, 0);
                int color_prog = (progress - 40) * 255 / 40;
                lv_obj_set_style_text_color(hybrid_lbl, lv_color_mix(bright_red, deep_purple, color_prog), 0);
            } else { // Fade out (Red)
                lv_obj_set_style_opa(hybrid_lbl, 255 - ((progress - 80) * 255 / 40), 0);
                lv_obj_set_style_text_color(hybrid_lbl, bright_red, 0);
            }
        } else {
            lv_obj_set_style_opa(hybrid_lbl, 0, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
        
        // Pulse bar visibility
        if (i == 50) {
            // Digital Pulse Bar at bottom
            pulse_bar = lv_obj_create(scr);
            lv_obj_set_size(pulse_bar, 10, 4);
            lv_obj_align(pulse_bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
            lv_obj_set_style_bg_color(pulse_bar, lv_color_hex(0x8A2BE2), 0);
            lv_obj_set_style_bg_opa(pulse_bar, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(pulse_bar, 0, 0);
            
            lv_anim_t p;
            lv_anim_init(&p);
            lv_anim_set_var(&p, pulse_bar);
            lv_anim_set_values(&p, 0, 358);
            lv_anim_set_time(&p, 3000);
            lv_anim_set_exec_cb(&p, (lv_anim_exec_xcb_t)anim_x_cb);
            lv_anim_set_repeat_count(&p, LV_ANIM_REPEAT_INFINITE);
            lv_anim_start(&p);
        }
    }

    if (pulse_bar) lv_obj_del(pulse_bar);

    // Final Snap to Clean Black / Main GUI
    vTaskDelay(pdMS_TO_TICKS(500));

    // 2.5 Dynamic Welcome Screen (Sub-splash)
    if (!genesis_setup_needed()) {
        lv_obj_clean(scr);
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
        
        char greeting[128];
        char username[32];
        genesis_settings_get_str("username", username, sizeof(username), "User");
        
        // Dynamic Greeting based on time/date
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        
        if (timeinfo.tm_mon == 0 && timeinfo.tm_mday == 1) {
            snprintf(greeting, sizeof(greeting), "Happy New Year, %s!", username);
        } else if (timeinfo.tm_hour < 12) {
            snprintf(greeting, sizeof(greeting), "Good Morning, %s", username);
        } else if (timeinfo.tm_hour < 18) {
            snprintf(greeting, sizeof(greeting), "Good Afternoon, %s", username);
        } else {
            snprintf(greeting, sizeof(greeting), "Good Evening, %s", username);
        }

        lv_obj_t * welcome_lbl = lv_label_create(scr);
        lv_label_set_text(welcome_lbl, greeting);
        lv_obj_set_style_text_font(welcome_lbl, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(welcome_lbl, lv_color_hex(0x8A2BE2), 0); // Purple
        lv_obj_center(welcome_lbl);
        
        lv_obj_set_style_opa(welcome_lbl, 0, 0);
        for(int i=0; i<20; i++) {
            lv_obj_set_style_opa(welcome_lbl, (i * 12), 0);
            lv_refr_now(NULL);
            vTaskDelay(pdMS_TO_TICKS(40));
        }
        vTaskDelay(pdMS_TO_TICKS(1000)); // Show for 1s
    }

    // 3. Build ID (Bottom Right)
    lv_obj_t * build_id = lv_label_create(scr);
    lv_label_set_text(build_id, "Build: 114 (v0.1) Rusty Knife");
    lv_obj_set_style_text_font(build_id, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(build_id, lv_color_hex(0x444444), 0);
    lv_obj_align(build_id, LV_ALIGN_BOTTOM_RIGHT, -45, -35); // Moved inward for rounded corners

    // Create Verbose Log Container (if dev_mode)
    if (genesis_settings_is_dev_mode()) {
        boot_log_cont = lv_obj_create(scr);
        lv_obj_set_size(boot_log_cont, 300, 150);
        lv_obj_align(boot_log_cont, LV_ALIGN_TOP_LEFT, 10, 10);
        lv_obj_set_style_bg_opa(boot_log_cont, LV_OPA_60, 0);
        lv_obj_set_style_bg_color(boot_log_cont, lv_color_hex(0x000000), 0);
        lv_obj_set_style_border_width(boot_log_cont, 1, 0);
        lv_obj_set_style_border_color(boot_log_cont, lv_color_hex(0x00FF00), 0);
        lv_obj_set_flex_flow(boot_log_cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(boot_log_cont, 5, 0);
    }

    lv_refr_now(NULL);

    // 4. Final Display Brightness check
    uint8_t final_b = 200;
    genesis_display_send_cmd(dev, 0x51, &final_b, 1);
    
    uint32_t start_ticks = xTaskGetTickCount();
    bool menu_triggered = false;

    if (menu_triggered) {
        *boot_result = genesis_boot_menu_show();
    } else {
        ESP_LOGI(TAG, "Waiting 5s for boot menu trigger (GPIO0)...");
        while ((xTaskGetTickCount() - start_ticks) < pdMS_TO_TICKS(5000)) {
            if (gpio_get_level(GPIO_NUM_0) == 0) {
                *boot_result = genesis_boot_menu_show();
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    // Handover to main.c flow: setup and PIN check happen there.

    ESP_LOGI(TAG, "Boot animation complete. Handing over to VM.");
    return ESP_OK;
}
