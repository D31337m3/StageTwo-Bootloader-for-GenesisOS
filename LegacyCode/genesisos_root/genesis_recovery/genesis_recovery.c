#include "genesis_recovery.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_image_format.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define TAG "GENESIS_RECOVERY"
#define NVS_NAMESPACE "genesis"
#define NVS_KEY_FLAG  "rec_flag"
#define NVS_KEY_UPDATE "upd_flag"

static bool recovery_flag = false;

esp_err_t genesis_recovery_init(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t flag = 0;
    err = nvs_get_u8(handle, NVS_KEY_FLAG, &flag);
    if (err == ESP_OK) {
        recovery_flag = (flag == 1);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        recovery_flag = false;
    }

    nvs_close(handle);
    return ESP_OK;
}

bool genesis_recovery_needed(void) {
    return recovery_flag;
}

void genesis_recovery_set_flag(void) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, NVS_KEY_FLAG, 1);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

void genesis_recovery_clear_flag(void) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, NVS_KEY_FLAG, 0);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

bool genesis_recovery_has_pending_update(void) {
    nvs_handle_t handle;
    uint8_t flag = 0;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u8(handle, NVS_KEY_UPDATE, &flag);
        nvs_close(handle);
    }
    return (flag == 1);
}

void genesis_recovery_set_pending_update(bool pending) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, NVS_KEY_UPDATE, pending ? 1 : 0);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

esp_err_t genesis_recovery_apply_update(bool major) {
    ESP_LOGI(TAG, "Applying update (major=%d)...", major);
    
    // Find the target factory partition
    const esp_partition_t *factory = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "factory");
    if (!factory) {
        ESP_LOGE(TAG, "Factory partition not found");
        return ESP_FAIL;
    }

    if (major) {
        // MAJOR UPDATE: Flash from /sd/update.bin or /userdata/update.bin
        const char *path = "/sd/update.bin";
        FILE *f = fopen(path, "rb");
        if (!f) {
            path = "/userdata/update.bin";
            f = fopen(path, "rb");
        }
        
        if (!f) {
            ESP_LOGE(TAG, "Update binary not found on SD or UserData");
            return ESP_FAIL;
        }

        ESP_LOGW(TAG, "MAJOR UPDATE: Flashing %s to factory partition...", path);
        
        esp_err_t err = esp_partition_erase_range(factory, 0, factory->size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to erase factory partition: %s", esp_err_to_name(err));
            fclose(f);
            return err;
        }

        uint8_t *buf = malloc(4096);
        if (!buf) {
            fclose(f);
            return ESP_ERR_NO_MEM;
        }

        size_t offset = 0;
        size_t read_bytes;
        while ((read_bytes = fread(buf, 1, 4096, f)) > 0) {
            err = esp_partition_write(factory, offset, buf, read_bytes);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Flash write failed at offset 0x%x: %s", offset, esp_err_to_name(err));
                free(buf);
                fclose(f);
                return err;
            }
            offset += read_bytes;
        }
        
        free(buf);
        fclose(f);
        ESP_LOGI(TAG, "Major update applied successfully. %d bytes flashed.", offset);
    } else {
        // SELECTIVE UPDATE: Copy files from staging
        ESP_LOGI(TAG, "SELECTIVE UPDATE: Syncing /sd/staging -> /userdata...");
        // TODO: Implement file copy logic
    }

    return ESP_OK;
}

esp_err_t genesis_recovery_start(void) {
    ESP_LOGI(TAG, "Starting Recovery/Repair Utility...");
    
    // 1. Find the 'recovery' app partition
    const esp_partition_t *recovery_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "recovery");
    if (!recovery_partition) {
        ESP_LOGE(TAG, "Partition 'recovery' (ota_0) not found!");
        return ESP_ERR_NOT_FOUND;
    }

    // 2. Check if we are already running from it
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == recovery_partition) {
        ESP_LOGI(TAG, "Already running from Recovery Partition. Launching Recovery GUI...");
        // Here we would normally stay in this app and show the Recovery UI.
        // For now, we'll just return and let app_main handle the rest or block.
        return ESP_OK;
    }

    // 3. Verify the image in the recovery partition
    esp_image_header_t header;
    if (esp_partition_read(recovery_partition, 0, &header, sizeof(header)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read recovery partition header");
        return ESP_FAIL;
    }

    if (header.magic != ESP_IMAGE_HEADER_MAGIC) {
        ESP_LOGE(TAG, "Invalid image in recovery partition (Magic: 0x%02x)", header.magic);
        return ESP_ERR_INVALID_STATE;
    }

    // 4. Set boot partition and restart
    ESP_LOGW(TAG, "Valid recovery image detected. Switching boot partition...");
    if (esp_ota_set_boot_partition(recovery_partition) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set boot partition!");
        return ESP_FAIL;
    }

    genesis_recovery_clear_flag();
    ESP_LOGW(TAG, "Rebooting into Recovery Mode...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;
}
