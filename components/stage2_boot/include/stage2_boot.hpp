#pragma once
#include "esp_partition.h"

namespace stage2_boot {
    bool boot_genesisos();
    bool boot_partition(const char* label);
    bool mark_partition_for_next_boot(const char* label);
    bool is_partition_valid(const char* label);
    bool can_boot_partition(const char* label);
    bool has_bootable_genesis_partition();
    const esp_partition_t* find_partition(const char* label);
    void reboot();
}
