/*
 * genesis_ota.c
 *
 * Automated Update & Recovery Manager for Genesis OS
 */

#include "genesis_ota.h"
#include "genesis_wifi.h"
#include "genesis_settings.h"
#include "genesis_notifications.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_partition.h"
#include "esp_flash.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <time.h>
#include <stdio.h>

static const char *TAG = "GENESIS_OTA";

static genesis_ota_state_t g_ota_state = OTA_IDLE;
static int g_ota_progress = 0;
static time_t g_last_check_time = 0;

void genesis_ota_init(void) {
    g_ota_state = OTA_IDLE;
    g_last_check_time = time(NULL);
    ESP_LOGI(TAG, "OTA Manager Initialized");
}

static bool should_check_now(void) {
    int auto_update = genesis_settings_get_int("ota_auto", 0);
    if (!auto_update) return false;
    time_t now = time(NULL);
    if (difftime(now, g_last_check_time) > 86400) { // Check every 24h as per follow-up question
        return true;
    }
    return false;
}

void genesis_ota_update_tick(void) {
    if (g_ota_state == OTA_IDLE && should_check_now()) {
        genesis_ota_start_check();
    }
}
#define GITHUB_VERSION_URL "https://raw.githubusercontent.com/d31337m3/genesisos/main/firmware/ota/version.txt"
#define GITHUB_BIN_BASE_URL "https://raw.githubusercontent.com/d31337m3/genesisos/main/firmware/ota/"

esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // Buffer data if needed
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t genesis_ota_start_check(void) {
    if (!genesis_wifi_is_connected()) {
        ESP_LOGW(TAG, "WiFi not connected, skipping OTA check.");
        return ESP_ERR_INVALID_STATE;
    }

    g_ota_state = OTA_CHECKING;
    g_last_check_time = time(NULL);
    ESP_LOGI(TAG, "Checking for updates at %s...", GITHUB_VERSION_URL);

    esp_http_client_config_t config = {
        .url = GITHUB_VERSION_URL,
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200) {
            char version_str[16] = {0};
            int content_len = esp_http_client_read_response(client, version_str, sizeof(version_str)-1);
            if (content_len > 0) {
                int remote_version = atoi(version_str);
                ESP_LOGI(TAG, "Remote Version: %d, Local Build: %d", remote_version, BUILD_NUMBER);
                if (remote_version > BUILD_NUMBER) {
                    ESP_LOGI(TAG, "New version available! Starting download...");
                    genesis_ui_add_notification("OTA", "New update found, downloading...", 0);
                    genesis_ota_download_and_stage(remote_version);
                } else {
                    ESP_LOGI(TAG, "System is up to date.");
                }
            }
        }
    } else {
        ESP_LOGE(TAG, "HTTP GET version failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    
    if (g_ota_state == OTA_CHECKING) g_ota_state = OTA_IDLE;
    return err;
}

esp_err_t genesis_ota_download_and_stage(int version) {
    char url[256];
    snprintf(url, sizeof(url), "%s%d.bin", GITHUB_BIN_BASE_URL, version);
    
    g_ota_state = OTA_DOWNLOADING;
    ESP_LOGI(TAG, "Downloading firmware from %s...", url);

    const esp_partition_t *recovery = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (!recovery) {
        ESP_LOGE(TAG, "Recovery partition not found!");
        return ESP_ERR_NOT_FOUND;
    }

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    if (esp_http_client_open(client, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length <= 0) {
        ESP_LOGE(TAG, "Invalid content length");
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    esp_ota_handle_t update_handle = 0;
    esp_ota_begin(recovery, OTA_SIZE_UNKNOWN, &update_handle);

    char *buffer = malloc(4096);
    int binary_len = 0;
    while (1) {
        int data_read = esp_http_client_read(client, buffer, 4096);
        if (data_read == 0) {
            break;
        } else if (data_read < 0) {
            ESP_LOGE(TAG, "Error: SSL data read error");
            break;
        }
        esp_ota_write(update_handle, buffer, data_read);
        binary_len += data_read;
        g_ota_progress = (binary_len * 100) / content_length;
    }

    free(buffer);
    esp_ota_end(update_handle);
    esp_http_client_cleanup(client);
    
    ESP_LOGI(TAG, "OTA Download Complete (%d bytes). Staged to recovery partition.", binary_len);
    genesis_ui_add_notification("OTA", "Update staged to recovery.", 0);
    g_ota_state = OTA_IDLE;

    return ESP_OK;
}

// Low-level Factory Partition Flasher
esp_err_t genesis_factory_flash_from_file(const char* path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open flash source: %s", path);
        return ESP_FAIL;
    }

    const esp_partition_t *factory = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (!factory) {
        fclose(file);
        return ESP_ERR_NOT_FOUND;
    }

    g_ota_state = OTA_FLASHING_FACTORY;
    ESP_LOGI(TAG, "Flashing Factory Partition from %s...", path);

    // Erase
    esp_partition_erase_range(factory, 0, factory->size);

    uint8_t *buffer = malloc(4096);
    size_t offset = 0;
    if (!buffer) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    
    while (1) {
        size_t br = fread(buffer, 1, 4096, file);
        if (br == 0) {
            break;
        }
        esp_partition_write(factory, offset, buffer, br);
        offset += br;
        g_ota_progress = (offset * 100) / factory->size;
    }

    free(buffer);
    fclose(file);
    
    g_ota_state = OTA_COMPLETE;
    ESP_LOGI(TAG, "Factory Flash Complete. Restarting...");
    
    genesis_notifications_push("System Updated", "Factory Reset Complete", 0x00FF00);
    
    esp_restart();
    return ESP_OK;
}

genesis_ota_state_t genesis_ota_get_state(void) {
    return g_ota_state;
}

int genesis_ota_get_progress(void) {
    return g_ota_progress;
}
