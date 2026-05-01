#include "stage2_recovery.hpp"
#include "stage2_ui.hpp"
#include "stage2_repair.hpp"
#include "stage2_ota.hpp"
#include "stage2_logging.hpp"
#include "genesis_theme.hpp"
#include "genesis_display.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"

static const char* TAG = "stage2_recovery";

namespace stage2_recovery {

static lv_obj_t* s_recovery_screen = nullptr;

static void style_screen(lv_obj_t* screen)
{
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x050512), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
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

void create_recovery_menu()
{
    if (s_recovery_screen) {
        lv_obj_del(s_recovery_screen);
        s_recovery_screen = nullptr;
    }

    s_recovery_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_recovery_screen, LV_PCT(100), LV_PCT(100));
    style_screen(s_recovery_screen);
    lv_scr_load(s_recovery_screen);

    // Title
    lv_obj_t* title = lv_label_create(s_recovery_screen);
    lv_label_set_text(title, "Recovery Mode");
    lv_obj_set_pos(title, 20, 20);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFF3B3B), 0);

    // Subtitle
    lv_obj_t* subtitle = lv_label_create(s_recovery_screen);
    lv_label_set_text(subtitle, "StageTwo Recovery Console");
    lv_obj_set_pos(subtitle, 20, 55);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x888899), 0);
}

bool system_scan()
{
    ESP_LOGI(TAG, "Running system scan...");

    // Scan partitions
    const esp_partition_t* parts[10];
    esp_partition_iterator_t it = esp_partition_create(ESP_PARTITION_TYPE_ANY, NULL, NULL);

    int count = 0;
    while (it && count < 10) {
        const esp_partition_t* part = esp_partition_get(it);
        if (part) {
            parts[count++] = part;
            ESP_LOGI(TAG, "  Partition: %s, size: %zu, offset: 0x%x",
                     part->label, part->size, part->address);
        }
        it = esp_partition_next(it);
    }

    if (it) {
        esp_partition_iterator_release(it);
    }

    // Check OTA partitions
    bool ota_0_valid = false;
    bool ota_1_valid = false;

    const esp_partition_t* ota_0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    const esp_partition_t* ota_1 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);

    if (ota_0) {
        esp_app_desc_t desc;
        if (esp_ota_get_partition_description(ota_0, &desc) == ESP_OK) {
            ota_0_valid = true;
            ESP_LOGI(TAG, "  OTA_0: %s v%s", desc.project_name, desc.version);
        }
    }

    if (ota_1) {
        esp_app_desc_t desc;
        if (esp_ota_get_partition_description(ota_1, &desc) == ESP_OK) {
            ota_1_valid = true;
            ESP_LOGI(TAG, "  OTA_1: %s v%s", desc.project_name, desc.version);
        }
    }

    // Check for saved update
    bool has_update = stage2_ota::has_saved_update();
    if (has_update) {
        stage2_ota::UpdateManifest manifest;
        if (stage2_ota::get_saved_update_info(&manifest)) {
            ESP_LOGI(TAG, "  Saved update: %s", manifest.version.c_str());
        }
    }

    ESP_LOGI(TAG, "System scan complete: ota_0=%d, ota_1=%d, has_update=%d",
             ota_0_valid, ota_1_valid, has_update);

    return (ota_0_valid || ota_1_valid);
}

void run()
{
    stage2_logging::warn("Recovery Mode started");
    create_recovery_menu();

    const char* items[] = {
        "System Scan",
        "Repair System",
        "Install Saved Update",
        "Download Update",
        "Factory Reset",
        "Reboot to StageTwo"
    };

    int selected = 0;

    // Create menu item buttons
    lv_obj_t* buttons[6] = {nullptr};
    for (int i = 0; i < 6; i++) {
        buttons[i] = lv_btn_create(s_recovery_screen);
        lv_obj_set_pos(buttons[i], 40, 100 + i * 50);
        lv_obj_set_size(buttons[i], 288, 40);

        lv_obj_t* lbl = lv_label_create(buttons[i]);
        lv_label_set_text(lbl, items[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xF4F7FF), 0);
        lv_obj_set_style_align(lbl, LV_ALIGN_CENTER, 0);

        style_button(buttons[i], 0x1A1A2E);
    }

    auto update_selection = [&]() {
        for (int i = 0; i < 6; i++) {
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
            selected = (selected + 1) % 6;
            update_selection();
            ESP_LOGI(TAG, "Recovery selected: %s", items[selected]);
        }

        if (ev == stage2_input::ButtonEvent::LongPress) {
            ESP_LOGI(TAG, "Executing recovery item: %s", items[selected]);

            switch (selected) {
                case 0: // System Scan
                    system_scan();
                    stage2_ui::show_progress("System Scan", 100);
                    break;

                case 1: // Repair System
                    if (stage2_ui::confirm_destructive_action(
                        "Repair System",
                        "This will flash a verified GenesisOS image to an app partition.",
                        "Affected: inactive GenesisOS OTA slot"
                    )) {
                        stage2_repair::repair_system();
                    }
                    break;

                case 2: // Install Saved Update
                    stage2_ota::install_saved_update();
                    break;

                case 3: // Download Update
                    stage2_ota::download_update_flow();
                    break;

                case 4: // Factory Reset
                    if (stage2_ui::confirm_destructive_action(
                        "Factory Reset",
                        "This clears user data and restores the latest stored GenesisOS image.",
                        "Affected: userdata, python, config, logs, safe NVS namespaces"
                    )) {
                        stage2_repair::factory_reset();
                    }
                    break;

                case 5: // Reboot to StageTwo
                    esp_restart();
                    break;
            }

            // Recreate menu after action
            create_recovery_menu();
            for (int i = 0; i < 6; i++) {
                buttons[i] = lv_btn_create(s_recovery_screen);
                lv_obj_set_pos(buttons[i], 40, 100 + i * 50);
                lv_obj_set_size(buttons[i], 288, 40);

                lv_obj_t* lbl = lv_label_create(buttons[i]);
                lv_label_set_text(lbl, items[i]);
                lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
                lv_obj_set_style_text_color(lbl, lv_color_hex(0xF4F7FF), 0);
                lv_obj_set_style_align(lbl, LV_ALIGN_CENTER, 0);

                style_button(buttons[i], 0x1A1A2E);
            }
            update_selection();
        }

        stage2_ui::tick();
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

bool repair_system()
{
    if (!stage2_ui::confirm_destructive_action(
        "Repair System",
        "This will flash a verified GenesisOS image to an app partition.",
        "Affected: inactive GenesisOS OTA slot"
    )) {
        return false;
    }

    return stage2_repair::repair_system();
}

bool backup_firmware()
{
    ESP_LOGI(TAG, "Backup firmware placeholder");
    return true;
}

bool backup_user_data()
{
    ESP_LOGI(TAG, "Backup user data placeholder");
    return true;
}

}
    ESP_LOGI(TAG, "Backup user data placeholder");
    return true;
}

}
