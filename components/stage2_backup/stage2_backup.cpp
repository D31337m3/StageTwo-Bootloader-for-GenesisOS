#include "stage2_backup.hpp"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "esp_log.h"

#include "genesis_sdcard.hpp"
#include "stage2_logging.hpp"

static const char* TAG = "stage2_backup";

namespace {

static constexpr uint32_t k_version = 1;
static const char k_magic[8] = {'G','O','S','B','K','P','0','1'};

enum entry_type_t : uint8_t {
    ENTRY_END = 0,
    ENTRY_DIR = 1,
    ENTRY_FILE = 2,
};

static bool path_starts_with(const char* path, const char* prefix)
{
    return strncmp(path, prefix, strlen(prefix)) == 0;
}

static esp_err_t ensure_dir(const char* path)
{
    if (mkdir(path, 0777) == 0) return ESP_OK;
    if (errno == EEXIST) return ESP_OK;
    return ESP_FAIL;
}

static esp_err_t write_u32(FILE* f, uint32_t v)
{
    return fwrite(&v, 1, sizeof(v), f) == sizeof(v) ? ESP_OK : ESP_FAIL;
}

static esp_err_t read_u32(FILE* f, uint32_t* out)
{
    return fread(out, 1, sizeof(*out), f) == sizeof(*out) ? ESP_OK : ESP_FAIL;
}

static esp_err_t write_entry_header(FILE* f, entry_type_t type, const char* rel_path, uint32_t size_bytes)
{
    const uint32_t path_len = (uint32_t)strlen(rel_path);
    const uint8_t type_u8 = (uint8_t)type;

    if (fwrite(&type_u8, 1, 1, f) != 1) return ESP_FAIL;
    if (write_u32(f, path_len) != ESP_OK) return ESP_FAIL;
    if (write_u32(f, size_bytes) != ESP_OK) return ESP_FAIL;
    if (path_len > 0 && fwrite(rel_path, 1, path_len, f) != path_len) return ESP_FAIL;
    return ESP_OK;
}

static esp_err_t backup_file(FILE* out, const char* abs_path, const char* rel_path)
{
    struct stat st;
    if (stat(abs_path, &st) != 0) return ESP_FAIL;
    if (!S_ISREG(st.st_mode)) return ESP_OK;

    FILE* in = fopen(abs_path, "rb");
    if (!in) return ESP_FAIL;

    const uint32_t size = (uint32_t)st.st_size;
    esp_err_t err = write_entry_header(out, ENTRY_FILE, rel_path, size);
    if (err != ESP_OK) {
        fclose(in);
        return err;
    }

    uint8_t buf[4096];
    uint32_t left = size;
    while (left > 0) {
        const size_t want = left > sizeof(buf) ? sizeof(buf) : left;
        const size_t n = fread(buf, 1, want, in);
        if (n == 0) {
            fclose(in);
            return ESP_FAIL;
        }
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            return ESP_FAIL;
        }
        left -= (uint32_t)n;
    }

    fclose(in);
    return ESP_OK;
}

static esp_err_t backup_dir_recursive(FILE* out, const char* abs_dir, const char* rel_dir)
{
    // Write the directory entry (so empty dirs are preserved).
    if (write_entry_header(out, ENTRY_DIR, rel_dir, 0) != ESP_OK) return ESP_FAIL;

    DIR* d = opendir(abs_dir);
    if (!d) return ESP_FAIL;

    struct dirent* de;
    while ((de = readdir(d)) != nullptr) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;

        std::string child_abs = std::string(abs_dir) + "/" + de->d_name;
        std::string child_rel;
        if (rel_dir[0] == '\0') {
            child_rel = de->d_name;
        } else {
            child_rel = std::string(rel_dir) + "/" + de->d_name;
        }

        struct stat st;
        if (stat(child_abs.c_str(), &st) != 0) {
            closedir(d);
            return ESP_FAIL;
        }

        if (S_ISDIR(st.st_mode)) {
            if (backup_dir_recursive(out, child_abs.c_str(), child_rel.c_str()) != ESP_OK) {
                closedir(d);
                return ESP_FAIL;
            }
        } else if (S_ISREG(st.st_mode)) {
            if (backup_file(out, child_abs.c_str(), child_rel.c_str()) != ESP_OK) {
                closedir(d);
                return ESP_FAIL;
            }
        }
    }

    closedir(d);
    return ESP_OK;
}

static esp_err_t rm_rf(const char* abs_path)
{
    struct stat st;
    if (stat(abs_path, &st) != 0) {
        return (errno == ENOENT) ? ESP_OK : ESP_FAIL;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR* d = opendir(abs_path);
        if (!d) return ESP_FAIL;
        struct dirent* de;
        while ((de = readdir(d)) != nullptr) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
            std::string child = std::string(abs_path) + "/" + de->d_name;
            if (rm_rf(child.c_str()) != ESP_OK) {
                closedir(d);
                return ESP_FAIL;
            }
        }
        closedir(d);
        if (rmdir(abs_path) != 0) return ESP_FAIL;
        return ESP_OK;
    }

    if (unlink(abs_path) != 0) return ESP_FAIL;
    return ESP_OK;
}

static esp_err_t ensure_parent_dirs(const char* abs_path)
{
    // Create parent directories for a file path (best-effort).
    char tmp[256];
    strncpy(tmp, abs_path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;

    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            (void)mkdir(tmp, 0777);
            *p = '/';
        }
    }
    return ESP_OK;
}

static esp_err_t restore_entries(FILE* in)
{
    while (true) {
        uint8_t type_u8 = 0xFF;
        if (fread(&type_u8, 1, 1, in) != 1) return ESP_FAIL;

        if ((entry_type_t)type_u8 == ENTRY_END) return ESP_OK;

        uint32_t path_len = 0;
        uint32_t size = 0;
        if (read_u32(in, &path_len) != ESP_OK) return ESP_FAIL;
        if (read_u32(in, &size) != ESP_OK) return ESP_FAIL;

        std::string rel;
        rel.resize(path_len);
        if (path_len > 0 && fread(rel.data(), 1, path_len, in) != path_len) return ESP_FAIL;

        std::string abs = std::string("/user/") + rel;

        if ((entry_type_t)type_u8 == ENTRY_DIR) {
            (void)ensure_parent_dirs(abs.c_str());
            (void)mkdir(abs.c_str(), 0777);
            continue;
        }

        if ((entry_type_t)type_u8 != ENTRY_FILE) return ESP_FAIL;

        (void)ensure_parent_dirs(abs.c_str());
        FILE* out = fopen(abs.c_str(), "wb");
        if (!out) return ESP_FAIL;

        uint8_t buf[4096];
        uint32_t left = size;
        while (left > 0) {
            const size_t want = left > sizeof(buf) ? sizeof(buf) : left;
            const size_t n = fread(buf, 1, want, in);
            if (n == 0) {
                fclose(out);
                return ESP_FAIL;
            }
            if (fwrite(buf, 1, n, out) != n) {
                fclose(out);
                return ESP_FAIL;
            }
            left -= (uint32_t)n;
        }

        fclose(out);
    }
}

} // namespace

esp_err_t stage2_backup::backup_user_to_sd(const char* archive_path)
{
    if (!genesis_sdcard::mounted()) {
        (void)genesis_sdcard::init_optional();
    }
    if (!genesis_sdcard::mounted()) {
        ESP_LOGE(TAG, "SD not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    if (!path_starts_with(archive_path, genesis_sdcard::mount_path())) {
        ESP_LOGE(TAG, "archive_path must be under %s", genesis_sdcard::mount_path());
        return ESP_ERR_INVALID_ARG;
    }

    stage2_logging::info("Backup: start");
    (void)ensure_dir("/sd/stage2");
    (void)ensure_dir("/sd/stage2/backups");

    FILE* out = fopen(archive_path, "wb");
    if (!out) {
        ESP_LOGE(TAG, "Failed to open %s: errno=%d", archive_path, errno);
        return ESP_FAIL;
    }

    if (fwrite(k_magic, 1, sizeof(k_magic), out) != sizeof(k_magic)) {
        fclose(out);
        return ESP_FAIL;
    }
    if (write_u32(out, k_version) != ESP_OK) {
        fclose(out);
        return ESP_FAIL;
    }

    // Backup root directory as empty rel path.
    esp_err_t err = backup_dir_recursive(out, "/user", "");
    if (err != ESP_OK) {
        fclose(out);
        ESP_LOGE(TAG, "Backup failed");
        return err;
    }

    // End marker.
    const uint8_t end = (uint8_t)ENTRY_END;
    if (fwrite(&end, 1, 1, out) != 1) {
        fclose(out);
        return ESP_FAIL;
    }

    fclose(out);
    stage2_logging::info("Backup: done");
    return ESP_OK;
}

esp_err_t stage2_backup::restore_user_from_sd(const char* archive_path)
{
    if (!genesis_sdcard::mounted()) {
        (void)genesis_sdcard::init_optional();
    }
    if (!genesis_sdcard::mounted()) {
        ESP_LOGE(TAG, "SD not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    FILE* in = fopen(archive_path, "rb");
    if (!in) {
        ESP_LOGE(TAG, "Failed to open %s: errno=%d", archive_path, errno);
        return ESP_ERR_NOT_FOUND;
    }

    char magic[sizeof(k_magic)] = {};
    if (fread(magic, 1, sizeof(magic), in) != sizeof(magic) || memcmp(magic, k_magic, sizeof(k_magic)) != 0) {
        fclose(in);
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint32_t version = 0;
    if (read_u32(in, &version) != ESP_OK || version != k_version) {
        fclose(in);
        return ESP_ERR_INVALID_VERSION;
    }

    stage2_logging::warn("Restore: wiping /user");
    // Wipe contents of /user but keep /user itself.
    DIR* d = opendir("/user");
    if (d) {
        struct dirent* de;
        while ((de = readdir(d)) != nullptr) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
            std::string child = std::string("/user/") + de->d_name;
            (void)rm_rf(child.c_str());
        }
        closedir(d);
    }

    stage2_logging::info("Restore: extracting");
    esp_err_t err = restore_entries(in);
    fclose(in);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Restore failed");
        return err;
    }
    stage2_logging::info("Restore: done");
    return ESP_OK;
}
