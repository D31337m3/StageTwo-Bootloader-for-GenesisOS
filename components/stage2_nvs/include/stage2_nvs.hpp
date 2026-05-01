#pragma once
#include <string>

namespace stage2_nvs {
    void init();
    bool recovery_requested();
    void set_recovery_requested(bool enabled);
    void clear_recovery_requested();

    std::string get_saved_ssid();
    std::string get_saved_wifi_password();
    void save_wifi_credentials(const std::string& ssid, const std::string& password);
}
