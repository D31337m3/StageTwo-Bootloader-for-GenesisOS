#include "stage2_nvs.hpp"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char* TAG = "stage2_nvs";
static const char* NS = "genesis_stage2";

namespace stage2_nvs {

void init()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS init requested erase due to version/page issue");
        nvs_flash_erase();
        nvs_flash_init();
    }
}

bool recovery_requested()
{
    nvs_handle_t h;
    uint8_t value = 0;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    nvs_get_u8(h, "recovery_requested", &value);
    nvs_close(h);
    return value == 1;
}

void set_recovery_requested(bool enabled)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "recovery_requested", enabled ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

void clear_recovery_requested()
{
    set_recovery_requested(false);
}

std::string get_saved_ssid() { return ""; }
std::string get_saved_wifi_password() { return ""; }

void save_wifi_credentials(const std::string& ssid, const std::string& password)
{
    (void)ssid;
    (void)password;
    ESP_LOGW(TAG, "Wi-Fi credential persistence placeholder");
}

}
