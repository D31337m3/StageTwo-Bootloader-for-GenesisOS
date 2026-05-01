#pragma once
#include <stddef.h>
#include <stdint.h>

namespace stage2_image {
    bool sha256_partition(const char* label, char out_hex[65]);
    bool validate_file_sha256(const char* path, const char* expected_hex);
    bool flash_file_to_partition(const char* path, const char* partition_label);
    bool backup_partition_to_file(const char* partition_label, const char* path);
}
