#include "stage2_fs.hpp"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"

static const char* TAG = "stage2_fs";

static wl_handle_t s_wl_handle_python = WL_INVALID_HANDLE;
static wl_handle_t s_wl_handle_user = WL_INVALID_HANDLE;

static bool file_exists(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static esp_err_t write_text_file_atomic(const char* path, const char* contents)
{
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE* f = fopen(tmp, "wb");
    if (!f) return ESP_FAIL;
    fwrite(contents, 1, strlen(contents), f);
    fclose(f);
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void ensure_user_defaults()
{
    mkdir("/user", 0777);
    mkdir("/user/apps", 0777);
    mkdir("/user/media", 0777);

    if (!file_exists("/user/wifi.toml")) {
        const char* contents =
            "version = 1\n"
            "\n"
            "# Saved networks\n"
            "[[networks]]\n"
            "ssid = \"\"\n"
            "psk = \"\"\n"
            "disabled = true\n";
        ESP_LOGI(TAG, "Creating /user/wifi.toml");
        (void)write_text_file_atomic("/user/wifi.toml", contents);
    }
}

esp_err_t stage2_fs::mount_internal()
{
    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 8,
        .allocation_unit_size = 4096,
    };

    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl("/mpython", "python", &mount_config, &s_wl_handle_python);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to mount /mpython (%s)", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "/mpython mounted");
    }

    err = esp_vfs_fat_spiflash_mount_rw_wl("/user", "user", &mount_config, &s_wl_handle_user);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to mount /user (%s)", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "/user mounted");
    ensure_user_defaults();
    return ESP_OK;
}
