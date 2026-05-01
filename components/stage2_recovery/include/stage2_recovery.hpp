#pragma once
#include "lvgl.h"

namespace stage2_recovery {
    void run();
    bool system_scan();
    bool repair_system();
    bool backup_firmware();
    bool backup_user_data();
    void create_recovery_menu();
}
