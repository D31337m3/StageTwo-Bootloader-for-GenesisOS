#include "stage2_repair.hpp"
#include "stage2_logging.hpp"
#include "esp_log.h"
#include "esp_partition.h"
#include "nvs_flash.h"

static const char* TAG = "stage2_repair";

namespace stage2_repair {

bool repair_system()
{
    ESP_LOGW(TAG, "repair_system placeholder: validate and flash latest stored image");
    return false;
}

bool format_user_partitions()
{
    stage2_logging::warn("Formatting user partitions requested");

    const char* labels[] = {
        "user",
        "userdata",
        "python",
        "config",
        "logs",
        "cache"
    };

    bool any = false;
    for (const char* label : labels) {
        const esp_partition_t* p = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA,
            ESP_PARTITION_SUBTYPE_ANY,
            label
        );

        if (!p) {
            ESP_LOGW(TAG, "Partition not found or not present: %s", label);
            continue;
        }

        ESP_LOGW(TAG, "Erasing user partition: %s", label);
        esp_err_t err = esp_partition_erase_range(p, 0, p->size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Erase failed for %s: %s", label, esp_err_to_name(err));
            return false;
        }
        any = true;
    }

    return any;
}

bool factory_reset()
{
    stage2_logging::warn("Factory reset requested");
    bool ok = format_user_partitions();
    ok = clear_safe_nvs() && ok;

    ESP_LOGW(TAG, "Factory reset image refresh placeholder");
    return ok;
}

bool clear_safe_nvs()
{
    stage2_logging::warn("Clearing safe NVS namespaces placeholder");
    ESP_LOGW(TAG, "Implement namespace-specific clear instead of global nvs_flash_erase()");
    return true;
}

}
