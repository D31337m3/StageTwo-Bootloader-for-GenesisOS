#include "genesis_boot_menu.h"
#include <string.h>
#include <stdio.h>
#include "lvgl.h"
#include "genesis_display.h"
#include "genesis_recovery.h"
#include "genesis_setup.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"

#define TAG "GENESIS_BOOT_MENU"

static lv_obj_t *menu_list;
static bool exit_menu = false;
static genesis_boot_menu_result_t menu_result = GENESIS_BOOT_MENU_NORMAL;

// Single-button navigator (GPIO0)
#define BOOT_BTN_GPIO GPIO_NUM_0
#define BOOT_BTN_DEBOUNCE_MS 40
#define BOOT_BTN_DOUBLE_MS 300

static QueueHandle_t s_boot_btn_queue = NULL;
static lv_group_t *s_boot_menu_group = NULL;
static volatile uint32_t s_last_isr_tick = 0;

static void IRAM_ATTR boot_btn_isr(void *arg) {
    (void)arg;
    const uint32_t now_tick = xTaskGetTickCountFromISR();
    const uint32_t debounce_ticks = pdMS_TO_TICKS(BOOT_BTN_DEBOUNCE_MS);
    if ((now_tick - s_last_isr_tick) < debounce_ticks) {
        return;
    }
    s_last_isr_tick = now_tick;

    if (s_boot_btn_queue) {
        (void)xQueueSendFromISR(s_boot_btn_queue, &now_tick, NULL);
    }
}

static void boot_menu_apply_focus_style(lv_obj_t *btn) {
    if (btn == NULL) return;

    // "Purple Glow" for focused menu button
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 3, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x8A2BE2), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_FOCUSED);

    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn, 18, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x8A2BE2), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_60, LV_PART_MAIN | LV_STATE_FOCUSED);
}

static void btn_event_cb(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_target(e);
    const char * txt = lv_list_get_btn_text(menu_list, obj);

    if (strcmp(txt, "Normal Boot") == 0) {
        ESP_LOGI(TAG, "Menu: Normal Boot Selected");
        menu_result = GENESIS_BOOT_MENU_NORMAL;
        exit_menu = true;
    } else if (strcmp(txt, "MicroPython Recovery") == 0) {
        ESP_LOGI(TAG, "Menu: MicroPython Recovery Selected");
        menu_result = GENESIS_BOOT_MENU_MICROPY_RECOVERY;
        exit_menu = true;
    } else if (strcmp(txt, "Safe Mode") == 0) {
        ESP_LOGI(TAG, "Menu: Safe Mode Selected");
        // In MicroPython, safe mode can be triggered by NVS or a flag.
        // For now, let's just restart with a flag.
        // genesis_safe_mode_set_flag();
        esp_restart();
    } else if (strcmp(txt, "Recovery Mode") == 0) {
        ESP_LOGI(TAG, "Menu: Recovery Selected");
        genesis_recovery_set_flag();
        esp_restart();
    } else if (strcmp(txt, "Health Check") == 0) {
        ESP_LOGI(TAG, "Menu: Health Check Selected");
    } else if (strcmp(txt, "Toggle Dev Mode") == 0) {
        ESP_LOGI(TAG, "Menu: Dev Mode Toggled");
        nvs_handle_t handle;
        if (nvs_open("genesis", NVS_READWRITE, &handle) == ESP_OK) {
            uint8_t mode = 0;
            nvs_get_u8(handle, "dev_mode", &mode);
            nvs_set_u8(handle, "dev_mode", !mode);
            nvs_commit(handle);
            nvs_close(handle);
        }
    } else if (strcmp(txt, "Install Update") == 0) {
        ESP_LOGI(TAG, "Menu: Install Update Selected");
        genesis_recovery_set_pending_update(false);
        genesis_recovery_set_flag();
        esp_restart();
    } else if (strcmp(txt, "Format Drive") == 0) {
        ESP_LOGW(TAG, "Menu: Formatting Mpython...");
        const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "Mpython");
        if (p) {
            esp_partition_erase_range(p, 0, p->size);
            ESP_LOGI(TAG, "Format Complete.");
        }
    }
}

genesis_boot_menu_result_t genesis_boot_menu_show(void) {
    ESP_LOGI(TAG, "Launching Boot Menu...");

    // First, verify identity if PIN is enabled
    if (!genesis_setup_check_pin()) {
        ESP_LOGE(TAG, "Boot Menu Access Denied: Incorrect PIN");
        esp_restart();
    }

    lv_obj_t * scr = lv_scr_act();
    lv_obj_clean(scr);
    
    lv_obj_t * title = lv_label_create(scr);
    lv_label_set_text(title, "Genesis Boot Menu");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    menu_list = lv_list_create(scr);
    lv_obj_set_size(menu_list, 300, 350);
    lv_obj_align(menu_list, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(menu_list, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_grad_color(menu_list, lv_color_hex(0x16213E), 0);
    lv_obj_set_style_bg_grad_dir(menu_list, LV_GRAD_DIR_VER, 0);

    s_boot_menu_group = lv_group_create();
    lv_group_set_wrap(s_boot_menu_group, true);

    lv_obj_t * btn;
    if (genesis_recovery_has_pending_update()) {
        btn = lv_list_add_btn(menu_list, NULL, "Install Update");
        lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);
        // Make the update button prominent
        lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_AMBER), 0);
        boot_menu_apply_focus_style(btn);
        lv_group_add_obj(s_boot_menu_group, btn);
    }

    btn = lv_list_add_btn(menu_list, NULL, "Normal Boot");
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);
    boot_menu_apply_focus_style(btn);
    lv_group_add_obj(s_boot_menu_group, btn);

    btn = lv_list_add_btn(menu_list, NULL, "MicroPython Recovery");
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);
    boot_menu_apply_focus_style(btn);
    lv_group_add_obj(s_boot_menu_group, btn);

    btn = lv_list_add_btn(menu_list, NULL, "Safe Mode");
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);
    boot_menu_apply_focus_style(btn);
    lv_group_add_obj(s_boot_menu_group, btn);

    btn = lv_list_add_btn(menu_list, NULL, "Recovery Mode");
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);
    boot_menu_apply_focus_style(btn);
    lv_group_add_obj(s_boot_menu_group, btn);
    
    btn = lv_list_add_btn(menu_list, NULL, "Update System");
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);
    boot_menu_apply_focus_style(btn);
    lv_group_add_obj(s_boot_menu_group, btn);
    
    btn = lv_list_add_btn(menu_list, NULL, "Format Drive");
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);
    boot_menu_apply_focus_style(btn);
    lv_group_add_obj(s_boot_menu_group, btn);
    lv_group_focus_next(s_boot_menu_group);

    // GPIO0 interrupt + queue for debounced single/double click
    s_boot_btn_queue = xQueueCreate(8, sizeof(uint32_t));
    gpio_set_direction(BOOT_BTN_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BOOT_BTN_GPIO, GPIO_PULLUP_ONLY);
    gpio_set_intr_type(BOOT_BTN_GPIO, GPIO_INTR_NEGEDGE);
    esp_err_t isr_err = gpio_install_isr_service(0);
    if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(isr_err));
    }
    (void)gpio_isr_handler_add(BOOT_BTN_GPIO, boot_btn_isr, NULL);

    // Keep the task alive to handle events
    exit_menu = false;
    menu_result = GENESIS_BOOT_MENU_NORMAL;

    bool pending_single = false;
    uint32_t first_tick = 0;
    while(!exit_menu) {
        uint32_t tick_evt = 0;
        if (s_boot_btn_queue && xQueueReceive(s_boot_btn_queue, &tick_evt, 0) == pdTRUE) {
            if (!pending_single) {
                pending_single = true;
                first_tick = tick_evt;
            } else {
                const uint32_t double_ticks = pdMS_TO_TICKS(BOOT_BTN_DOUBLE_MS);
                if ((tick_evt - first_tick) <= double_ticks) {
                    lv_obj_t *focused = lv_group_get_focused(s_boot_menu_group);
                    if (focused) {
                        lv_obj_send_event(focused, LV_EVENT_CLICKED, NULL);
                    }
                    pending_single = false;
                } else {
                    // Too late: treat as a new first press
                    first_tick = tick_evt;
                }
            }
        }

        if (pending_single) {
            const uint32_t now = xTaskGetTickCount();
            const uint32_t double_ticks = pdMS_TO_TICKS(BOOT_BTN_DOUBLE_MS);
            if ((now - first_tick) > double_ticks) {
                lv_group_focus_next(s_boot_menu_group);
                pending_single = false;
            }
        }

        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Cleanup GPIO0 navigator
    (void)gpio_isr_handler_remove(BOOT_BTN_GPIO);
    gpio_set_intr_type(BOOT_BTN_GPIO, GPIO_INTR_DISABLE);
    if (s_boot_btn_queue) {
        vQueueDelete(s_boot_btn_queue);
        s_boot_btn_queue = NULL;
    }
    if (s_boot_menu_group) {
        lv_group_delete(s_boot_menu_group);
        s_boot_menu_group = NULL;
    }
    
    lv_obj_clean(lv_scr_act());
    ESP_LOGI(TAG, "Exiting Boot Menu.");
    return menu_result;
}
