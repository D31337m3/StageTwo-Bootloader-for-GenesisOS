#include "stage2_repartition.hpp"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <string>

#include "esp_err.h"
#include "esp_flash.h"
#include "esp_flash_partitions.h"
#include "esp_log.h"
#include "esp_spi_flash.h"

#include "genesis_sdcard.hpp"
#include "stage2_logging.hpp"

static const char* TAG = "stage2_repartition";

namespace {

static esp_err_t read_file_into(const char* path, void* buf, size_t buf_len, size_t* out_len)
{
    FILE* f = fopen(path, "rb");
    if (!f) return ESP_ERR_NOT_FOUND;

    const size_t n = fread(buf, 1, buf_len, f);
    if (ferror(f)) {
        fclose(f);
        return ESP_FAIL;
    }
    fclose(f);
    if (out_len) *out_len = n;
    return ESP_OK;
}

static const esp_partition_info_t* find_entry(const esp_partition_info_t* table, int count, const char* label)
{
    for (int i = 0; i < count; i++) {
        if (memcmp(table[i].label, label, strlen(label)) == 0) return &table[i];
    }
    return nullptr;
}

static esp_err_t flash_write_aligned(uint32_t offset, const void* data, uint32_t size)
{
    if (!esp_partition_main_flash_region_safe(offset, size)) {
        return ESP_ERR_INVALID_STATE;
    }
    // Erase first (must be sector aligned).
    const uint32_t erase_size = (size + SPI_FLASH_SEC_SIZE - 1) & ~(SPI_FLASH_SEC_SIZE - 1);
    esp_err_t err = esp_flash_erase_region(esp_flash_default_chip, offset, erase_size);
    if (err != ESP_OK) return err;
    return esp_flash_write(esp_flash_default_chip, data, offset, size);
}

static esp_err_t flash_erase_region_safe(uint32_t offset, uint32_t size)
{
    if (!esp_partition_main_flash_region_safe(offset, size)) return ESP_ERR_INVALID_STATE;
    const uint32_t erase_size = (size + SPI_FLASH_SEC_SIZE - 1) & ~(SPI_FLASH_SEC_SIZE - 1);
    return esp_flash_erase_region(esp_flash_default_chip, offset, erase_size);
}

} // namespace

esp_err_t stage2_repartition::apply_from_sd(const char* package_dir)
{
    if (!genesis_sdcard::mounted()) {
        (void)genesis_sdcard::init_optional();
    }
    if (!genesis_sdcard::mounted()) {
        ESP_LOGE(TAG, "SD not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    std::string part_path = std::string(package_dir) + "/partition-table.bin";
    std::string app_path = std::string(package_dir) + "/genesisos.bin";

    uint8_t part_buf[ESP_PARTITION_TABLE_SIZE] = {};
    size_t part_len = 0;
    esp_err_t err = read_file_into(part_path.c_str(), part_buf, sizeof(part_buf), &part_len);
    if (err != ESP_OK) return err;
    if (part_len != ESP_PARTITION_TABLE_SIZE) {
        ESP_LOGE(TAG, "partition-table.bin must be exactly 0x1000 bytes");
        return ESP_ERR_INVALID_SIZE;
    }

    // Verify and locate required entries.
    const esp_partition_info_t* table = (const esp_partition_info_t*)part_buf;
    int num_parts = 0;
    err = esp_partition_table_verify(table, true, &num_parts);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Partition table verify failed");
        return err;
    }

    const esp_partition_info_t* st2 = find_entry(table, num_parts, "stagetwo");
    const esp_partition_info_t* ga = find_entry(table, num_parts, "genesis_a");
    const esp_partition_info_t* gb = find_entry(table, num_parts, "genesis_b");
    const esp_partition_info_t* gg = find_entry(table, num_parts, "genesis_gold");
    const esp_partition_info_t* user = find_entry(table, num_parts, "user");
    const esp_partition_info_t* python = find_entry(table, num_parts, "python");

    if (!st2 || !ga || !gb || !gg || !user || !python) {
        ESP_LOGE(TAG, "New table missing required entries");
        return ESP_ERR_INVALID_ARG;
    }

    // Safety: StageTwo must stay exactly where it is.
    if (st2->pos.offset != 0x20000 || st2->pos.size != 0x200000) {
        ESP_LOGE(TAG, "Refusing table: stagetwo must remain at 0x20000 size 0x200000");
        return ESP_ERR_INVALID_ARG;
    }

    // Read GenesisOS app.
    FILE* app = fopen(app_path.c_str(), "rb");
    if (!app) {
        ESP_LOGE(TAG, "Missing %s (errno=%d)", app_path.c_str(), errno);
        return ESP_ERR_NOT_FOUND;
    }
    fseek(app, 0, SEEK_END);
    long app_size_l = ftell(app);
    fseek(app, 0, SEEK_SET);
    if (app_size_l <= 0) {
        fclose(app);
        return ESP_ERR_INVALID_SIZE;
    }
    const uint32_t app_size = (uint32_t)app_size_l;

    // Require app fits at least into genesis_a slot (all slots expected equal).
    if (app_size > ga->pos.size) {
        fclose(app);
        ESP_LOGE(TAG, "genesisos.bin too large for slot (size=%u slot=%u)", (unsigned)app_size, (unsigned)ga->pos.size);
        return ESP_ERR_INVALID_SIZE;
    }

    stage2_logging::warn("Repartition: writing new partition table");
    err = flash_write_aligned(ESP_PARTITION_TABLE_OFFSET, part_buf, ESP_PARTITION_TABLE_SIZE);
    if (err != ESP_OK) {
        fclose(app);
        ESP_LOGE(TAG, "Failed to write partition table: %s", esp_err_to_name(err));
        return err;
    }

    stage2_logging::warn("Repartition: erasing Genesis slots + data partitions");
    (void)flash_erase_region_safe(ga->pos.offset, ga->pos.size);
    (void)flash_erase_region_safe(gb->pos.offset, gb->pos.size);
    (void)flash_erase_region_safe(gg->pos.offset, gg->pos.size);
    (void)flash_erase_region_safe(user->pos.offset, user->pos.size);
    (void)flash_erase_region_safe(python->pos.offset, python->pos.size);

    // Write GenesisOS app into A/B/Gold.
    stage2_logging::warn("Repartition: flashing GenesisOS into slots");
    uint8_t buf[4096];
    auto write_slot = [&](uint32_t slot_off) -> esp_err_t {
        fseek(app, 0, SEEK_SET);
        uint32_t written = 0;
        while (written < app_size) {
            const size_t want = (app_size - written) > sizeof(buf) ? sizeof(buf) : (app_size - written);
            const size_t n = fread(buf, 1, want, app);
            if (n != want) return ESP_FAIL;
            esp_err_t werr = esp_flash_write(esp_flash_default_chip, buf, slot_off + written, n);
            if (werr != ESP_OK) return werr;
            written += (uint32_t)n;
        }
        return ESP_OK;
    };

    err = write_slot(ga->pos.offset);
    if (err == ESP_OK) err = write_slot(gb->pos.offset);
    if (err == ESP_OK) err = write_slot(gg->pos.offset);
    fclose(app);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to flash slots: %s", esp_err_to_name(err));
        return err;
    }

    stage2_logging::warn("Repartition: done. Rebooting now");
    return ESP_OK;
}
