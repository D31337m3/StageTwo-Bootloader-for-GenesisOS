#include "genesis_sdcard.hpp"
#include "esp_log.h"

static const char* TAG = "genesis_sdcard";
static bool s_mounted = false;

namespace genesis_sdcard {

bool init_optional()
{
    ESP_LOGW(TAG, "Placeholder SD init. Replace with GenesisOS SD mount code.");
    s_mounted = false;
    return false;
}

bool mounted()
{
    return s_mounted;
}

const char* mount_path()
{
    return "/sdcard";
}

}
