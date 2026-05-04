#include "stage2_repair.hpp"
#include "stage2_logging.hpp"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include <string.h>

static const char* TAG = "stage2_repair";

namespace stage2_repair {

static const char* kSlotA = "genesis_a";
static const char* kSlotB = "genesis_b";
static const char* kGolden = "genesis_gold";

static const char* pick_inactive_slot_label()
{
    const esp_partition_t* boot = esp_ota_get_boot_partition();
    if (boot) {
        if (strcmp(boot->label, kSlotA) == 0) return kSlotB;
        if (strcmp(boot->label, kSlotB) == 0) return kSlotA;
    }

    // If we don't know the current GenesisOS slot, default to Slot A.
    return kSlotA;
}

bool repair_system()
{
    stage2_logging::warn("Repair system requested (restore GenesisOS from factory slot)");

    const esp_partition_t* src = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_ANY,
        kGolden
    );

    const char* dst_label = pick_inactive_slot_label();
    const esp_partition_t* dst = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, dst_label);

    if (!src || !dst) {
        ESP_LOGE(TAG, "Missing partitions: src(%s)=%p dst(%s)=%p", kGolden, src, dst_label, dst);
        return false;
    }

    esp_app_desc_t src_desc = {};
    if (esp_ota_get_partition_description(src, &src_desc) != ESP_OK) {
        ESP_LOGE(TAG, "Source partition '%s' does not contain a valid app image", kGolden);
        return false;
    }

    ESP_LOGW(TAG, "Restoring GenesisOS from %s -> %s (src v%s)", kGolden, dst_label, src_desc.version);

    // We only need to guarantee the destination can boot a valid image.
    // Copying dst->size bytes ensures the destination partition is fully overwritten.
    // The source partition may be larger (that's OK); we never write past dst->size.
    const size_t copy_len = dst->size;
    const size_t chunk = 4096;

    esp_err_t err = esp_partition_erase_range(dst, 0, dst->size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erase failed for %s: %s", dst_label, esp_err_to_name(err));
        return false;
    }

    uint8_t* buf = (uint8_t*)heap_caps_malloc(chunk, MALLOC_CAP_DEFAULT);
    if (!buf) {
        ESP_LOGE(TAG, "Out of memory allocating copy buffer");
        return false;
    }

    for (size_t off = 0; off < copy_len; off += chunk) {
        size_t n = chunk;
        if (off + n > copy_len) n = copy_len - off;

        err = esp_partition_read(src, off, buf, n);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Read failed from %s at 0x%x: %s", kGolden, (unsigned)off, esp_err_to_name(err));
            heap_caps_free(buf);
            return false;
        }

        err = esp_partition_write(dst, off, buf, n);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Write failed to %s at 0x%x: %s", dst_label, (unsigned)off, esp_err_to_name(err));
            heap_caps_free(buf);
            return false;
        }
    }

    heap_caps_free(buf);

    esp_app_desc_t dst_desc = {};
    if (esp_ota_get_partition_description(dst, &dst_desc) != ESP_OK) {
        ESP_LOGE(TAG, "Restore completed but destination image is not valid");
        return false;
    }

    ESP_LOGW(TAG, "Restore complete: %s now v%s", dst_label, dst_desc.version);

    // Make the restored GenesisOS the next boot target.
    err = esp_ota_set_boot_partition(dst);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set boot partition to %s: %s", dst_label, esp_err_to_name(err));
        return false;
    }

    return true;
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

    // Restore GenesisOS app partition from the factory slot after wiping user data.
    if (!repair_system()) {
        ESP_LOGW(TAG, "Factory reset completed, but firmware restore failed");
        return false;
    }

    return ok;
}

bool clear_safe_nvs()
{
    stage2_logging::warn("Clearing safe NVS namespaces placeholder");
    ESP_LOGW(TAG, "Implement namespace-specific clear instead of global nvs_flash_erase()");
    return true;
}

}
