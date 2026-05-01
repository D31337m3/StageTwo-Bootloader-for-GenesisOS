#pragma once

namespace stage2_repair {
    bool repair_system();
    bool format_user_partitions();
    bool factory_reset();
    bool clear_safe_nvs();
}
