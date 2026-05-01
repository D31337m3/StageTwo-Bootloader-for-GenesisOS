#include "stage2_image.hpp"
#include "esp_partition.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "stage2_image";

namespace stage2_image {

bool sha256_partition(const char* label, char out_hex[65])
{
    (void)label;
    memset(out_hex, '0', 64);
    out_hex[64] = 0;
    ESP_LOGW(TAG, "sha256_partition placeholder");
    return false;
}

bool validate_file_sha256(const char* path, const char* expected_hex)
{
    (void)path;
    (void)expected_hex;
    ESP_LOGW(TAG, "validate_file_sha256 placeholder");
    return false;
}

bool flash_file_to_partition(const char* path, const char* partition_label)
{
    (void)path;
    (void)partition_label;
    ESP_LOGW(TAG, "flash_file_to_partition placeholder");
    return false;
}

bool backup_partition_to_file(const char* partition_label, const char* path)
{
    (void)partition_label;
    (void)path;
    ESP_LOGW(TAG, "backup_partition_to_file placeholder");
    return false;
}

}
