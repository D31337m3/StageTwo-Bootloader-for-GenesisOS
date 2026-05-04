#include "stage2_boot.hpp"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_log.h"

static const char* TAG = "stage2_boot";

namespace stage2_boot {

const esp_partition_t* find_partition(const char* label)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, label);
}

bool is_partition_valid(const char* label)
{
    const esp_partition_t* part = find_partition(label);
    if (!part) return false;

    esp_app_desc_t desc = {};
    esp_err_t err = esp_ota_get_partition_description(part, &desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Partition %s invalid: %s", label, esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Partition %s valid: app=%s version=%s", label, desc.project_name, desc.version);
    return true;
}

bool mark_partition_for_next_boot(const char* label)
{
    const esp_partition_t* part = find_partition(label);
    if (!part) {
        ESP_LOGE(TAG, "Partition not found: %s", label);
        return false;
    }

    if (!is_partition_valid(label)) {
        ESP_LOGE(TAG, "Refusing to boot invalid partition: %s", label);
        return false;
    }

    esp_err_t err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed setting boot partition %s: %s", label, esp_err_to_name(err));
        return false;
    }

    return true;
}

bool boot_partition(const char* label)
{
    if (!mark_partition_for_next_boot(label)) return false;
    reboot();
    return true;
}

bool boot_genesisos()
{
    // Primary boot target is the stable GenesisOS slot A
    if (boot_partition("genesis_a")) return true;

    // Fallback: boot slot B if it contains a valid image
    if (boot_partition("genesis_b")) return true;

    ESP_LOGE(TAG, "No valid GenesisOS image partition found");
    return false;
}

void reboot()
{
    ESP_LOGI(TAG, "Restarting");
    esp_restart();
}

}
