#pragma once

namespace genesis_sdcard {
    bool init_optional();
    bool mounted();
    const char* mount_path();
}
