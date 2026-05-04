#include "stage2_ota.hpp"
#include "stage2_ui.hpp"
#include "stage2_logging.hpp"
#include "stage2_nvs.hpp"
#include "genesis_theme.hpp"
#include "genesis_display.hpp"
#include "stage2_wifi.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "mbedtls/md.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"

static const char* TAG = "stage2_ota";

#ifndef GENESIS_UPDATE_MANIFEST_URL
#define GENESIS_UPDATE_MANIFEST_URL "https://raw.githubusercontent.com/OWNER/REPO/main/releases/latest/manifest.json"
#endif

#ifndef GENESIS_UPDATE_SERVER
#define GENESIS_UPDATE_SERVER "raw.githubusercontent.com"
#endif

#ifndef GENESIS_OSK_PASSWORD
#define GENESIS_OSK_PASSWORD "genesis"
#endif

namespace stage2_ota {

static bool s_downloading = false;
static DownloadProgress s_progress = {0, 0, 0};

static std::string s_saved_version;
static std::string s_saved_sha256;
static size_t s_saved_size = 0;

static const char* kSlotA = "genesis_a";
static const char* kSlotB = "genesis_b";

static const char* pick_inactive_slot_label()
{
    const esp_partition_t* boot = esp_ota_get_boot_partition();
    if (boot) {
        if (strcmp(boot->label, kSlotA) == 0) return kSlotB;
        if (strcmp(boot->label, kSlotB) == 0) return kSlotA;
    }

    // Unknown/currently not a GenesisOS slot -> default to slot B, keeping slot A as the "known good" slot.
    return kSlotB;
}

static esp_err_t http_event_handler(esp_http_client_event_t* evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }

    return ESP_OK;
}

bool fetch_manifest(UpdateManifest* manifest)
{
    if (!manifest) return false;

    ESP_LOGI(TAG, "Fetching update manifest from: %s", GENESIS_UPDATE_MANIFEST_URL);

    // Configure HTTP client (avoid designated initializers in C++).
    esp_http_client_config_t config = {};
    config.url = GENESIS_UPDATE_MANIFEST_URL;
    config.event_handler = http_event_handler;
    config.timeout_ms = 10000;
    config.transport_type = HTTP_TRANSPORT_OVER_TCP;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length <= 0) {
        ESP_LOGE(TAG, "Failed to fetch headers");
        esp_http_client_cleanup(client);
        return false;
    }

    // Read manifest JSON
    char* buffer = (char*)malloc(content_length + 1);
    if (!buffer) {
        esp_http_client_cleanup(client);
        return false;
    }

    int read_len = esp_http_client_read_response(client, buffer, content_length);
    buffer[read_len] = '\0';

    ESP_LOGI(TAG, "Manifest response: %s", buffer);

    // Parse simple JSON (version, url, sha256, size)
    // In production, use cJSON or similar
    char* version_start = strstr(buffer, "\"version\":\"");
    char* url_start = strstr(buffer, "\"url\":\"");
    char* sha_start = strstr(buffer, "\"sha256\":\"");
    char* size_start = strstr(buffer, "\"size\":");

    if (version_start && url_start && sha_start && size_start) {
        version_start += 10; // skip "\"version\":"
        char* version_end = strchr(version_start, '"');
        if (version_end) {
            manifest->version.assign(version_start, version_end - version_start);
        }

        url_start += 6; // skip "\"url\":"
        char* url_end = strchr(url_start, '"');
        if (url_end) {
            manifest->url.assign(url_start, url_end - url_start);
        }

        sha_start += 9; // skip "\"sha256\":"
        char* sha_end = strchr(sha_start, '"');
        if (sha_end) {
            manifest->sha256.assign(sha_start, sha_end - sha_start);
        }

        size_start += 6; // skip "\"size\":"
        manifest->size = atoi(size_start);

        ESP_LOGI(TAG, "Parsed manifest: version=%s, size=%zu", manifest->version.c_str(), manifest->size);
        free(buffer);
        esp_http_client_cleanup(client);
        return true;
    }

    free(buffer);
    esp_http_client_cleanup(client);
    return false;
}

bool validate_image(const uint8_t* data, size_t len, const std::string& expected_sha256)
{
    if (!data || len == 0 || expected_sha256.empty()) {
        return false;
    }

    // Calculate SHA-256
    uint8_t hash[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, data, len);
    mbedtls_md_finish(&ctx, hash);
    mbedtls_md_free(&ctx);

    // Convert to hex string
    char hash_str[65];
    for (int i = 0; i < 32; i++) {
        sprintf(hash_str + i * 2, "%02x", hash[i]);
    }
    hash_str[64] = '\0';

    ESP_LOGI(TAG, "Calculated hash: %s", hash_str);
    ESP_LOGI(TAG, "Expected hash:   %s", expected_sha256.c_str());

    bool valid = (strcmp(hash_str, expected_sha256.c_str()) == 0);
    if (valid) {
        stage2_logging::info("Image validation passed");
    } else {
        stage2_logging::error("Image validation FAILED - checksum mismatch!");
    }

    return valid;
}

bool save_update(const uint8_t* data, size_t len)
{
    ESP_LOGI(TAG, "Saving update to flash: %zu bytes", len);

    // Save into the inactive GenesisOS OTA slot (A/B), never into the golden recovery image.
    const char* slot_label = pick_inactive_slot_label();
    const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, slot_label);

    if (!part) {
        ESP_LOGE(TAG, "Target OTA slot not found (%s)", slot_label);
        return false;
    }

    if (len > part->size) {
        ESP_LOGE(TAG, "Update too large for OTA slot %s: %zu > %zu", slot_label, len, part->size);
        return false;
    }

    // Erase partition
    esp_err_t err = esp_partition_erase_range(part, 0, part->size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase partition: %s", esp_err_to_name(err));
        return false;
    }

    // Write data
    err = esp_partition_write(part, 0, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write update: %s", esp_err_to_name(err));
        return false;
    }

    // Save metadata to NVS
    nvs_handle_t nvs;
    err = nvs_open("ota", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return false;
    }

    nvs_set_str(nvs, "saved_version", s_saved_version.c_str());
    nvs_set_str(nvs, "saved_sha256", s_saved_sha256.c_str());
    nvs_set_str(nvs, "saved_slot", slot_label);
    nvs_set_u64(nvs, "saved_size", (uint64_t)s_saved_size);
    nvs_commit(nvs);
    nvs_close(nvs);

    ESP_LOGI(TAG, "Update saved successfully to %s", slot_label);
    return true;
}

bool has_saved_update()
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("ota", NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return false;
    }

    char version[64] = {0};
    size_t len = sizeof(version);
    err = nvs_get_str(nvs, "saved_version", version, &len);
    nvs_close(nvs);

    return (err == ESP_OK && strlen(version) > 0);
}

bool get_saved_update_info(UpdateManifest* manifest)
{
    if (!manifest) return false;

    nvs_handle_t nvs;
    esp_err_t err = nvs_open("ota", NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return false;
    }

    char version[64] = {0};
    char sha256[64] = {0};
    char slot[16] = {0};
    size_t len = sizeof(version);

    nvs_get_str(nvs, "saved_version", version, &len);
    len = sizeof(sha256);
    nvs_get_str(nvs, "saved_sha256", sha256, &len);
    len = sizeof(slot);
    if (nvs_get_str(nvs, "saved_slot", slot, &len) != ESP_OK) {
        // Backward compatibility: older builds used a fixed staging partition.
        strncpy(slot, kSlotB, sizeof(slot) - 1);
    }
    uint64_t saved_size_u64 = 0;
    nvs_get_u64(nvs, "saved_size", &saved_size_u64);
    s_saved_size = (size_t)saved_size_u64;

    nvs_close(nvs);

    manifest->version = version;
    manifest->sha256 = sha256;
    manifest->size = s_saved_size;

    ESP_LOGI(TAG, "Saved update slot: %s", slot);
    return true;
}

bool download_update(const UpdateManifest& manifest, DownloadProgress* progress)
{
    if (!progress) return false;

    ESP_LOGI(TAG, "Downloading update from: %s", manifest.url.c_str());

    s_downloading = true;
    s_progress = {0, 0, 0};

    // Configure HTTP client for download (avoid designated initializers in C++).
    esp_http_client_config_t config = {};
    config.url = manifest.url.c_str();
    config.event_handler = http_event_handler;
    config.timeout_ms = 30000;
    config.transport_type = HTTP_TRANSPORT_OVER_TCP;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client for download");
        s_downloading = false;
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open download: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        s_downloading = false;
        return false;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length <= 0) {
        ESP_LOGE(TAG, "Failed to get content length");
        esp_http_client_cleanup(client);
        s_downloading = false;
        return false;
    }

    progress->total = content_length;
    progress->downloaded = 0;

    // Allocate buffer for download
    uint8_t* buffer = (uint8_t*)malloc(content_length);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate download buffer");
        esp_http_client_cleanup(client);
        s_downloading = false;
        return false;
    }

    // Read data in chunks
    int total_read = 0;
    int chunk_size = 4096;
    char chunk[chunk_size];

    while (total_read < content_length) {
        int read = esp_http_client_read_response(client, chunk, chunk_size);
        if (read <= 0) {
            break;
        }

        memcpy(buffer + total_read, chunk, read);
        total_read += read;

        progress->downloaded = total_read;
        progress->percent = (total_read * 100) / content_length;

        ESP_LOGI(TAG, "Downloaded: %d/%d (%d%%)", total_read, content_length, progress->percent);

        // Update UI
        stage2_ui::show_progress("Downloading Update", progress->percent);
    }

    esp_http_client_cleanup(client);
    s_downloading = false;

    if (total_read != content_length) {
        ESP_LOGE(TAG, "Download incomplete: %d vs %d", total_read, content_length);
        free(buffer);
        return false;
    }

    // Validate the downloaded image
    if (!validate_image(buffer, total_read, manifest.sha256)) {
        ESP_LOGE(TAG, "Downloaded image validation failed");
        free(buffer);
        return false;
    }

    // Save the update
    if (!save_update(buffer, total_read)) {
        ESP_LOGE(TAG, "Failed to save update");
        free(buffer);
        return false;
    }

    free(buffer);
    return true;
}

bool download_update_flow()
{
    stage2_logging::info("Starting OTA update flow");

    // Check for saved update first
    if (has_saved_update()) {
        ESP_LOGI(TAG, "Saved update found");
        UpdateManifest saved;
        if (get_saved_update_info(&saved)) {
            ESP_LOGI(TAG, "Saved version: %s", saved.version.c_str());
        }
    }

    // Initialize Wi-Fi + autoconnect using GenesisOS-style persisted creds (/user/wifi.toml).
    ESP_LOGI(TAG, "Initializing Wi-Fi for OTA");
    (void)stage2_wifi_init();
    (void)stage2_wifi_autoconnect();

    int retries = 30;
    while (retries-- > 0) {
        if (stage2_wifi_is_connected()) break;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (!stage2_wifi_is_connected()) {
        ESP_LOGE(TAG, "Wi-Fi not connected; configure in Advanced -> Wi-Fi Setup");
        stage2_ui::show_error("Wi-Fi not connected. Configure Advanced -> Wi-Fi Setup");
        return false;
    }

    // Fetch manifest
    UpdateManifest manifest;
    if (!fetch_manifest(&manifest)) {
        ESP_LOGE(TAG, "Failed to fetch update manifest");
        esp_wifi_stop();
        return false;
    }

    // Download update
    DownloadProgress progress = {0, 0, 0};
    if (!download_update(manifest, &progress)) {
        ESP_LOGE(TAG, "Update download failed");
        esp_wifi_stop();
        return false;
    }

    // Cleanup
    esp_wifi_stop();
    esp_wifi_deinit();

    stage2_logging::info("OTA update downloaded and saved successfully");
    return true;
}

bool install_saved_update()
{
    if (!stage2_ui::confirm_destructive_action(
        "Install Saved Update",
        "This will install a saved and verified GenesisOS image.",
        "Affected: inactive GenesisOS OTA slot"
    )) {
        return false;
    }

    if (!has_saved_update()) {
        ESP_LOGE(TAG, "No saved update found");
        stage2_ui::show_error("No saved update found");
        return false;
    }

    // Get saved update info
    UpdateManifest manifest;
    if (!get_saved_update_info(&manifest)) {
        ESP_LOGE(TAG, "Failed to get saved update info");
        return false;
    }

    ESP_LOGI(TAG, "Installing saved update: %s", manifest.version.c_str());

    // Installing means marking the saved slot for next boot.
    // The slot label is persisted in NVS by save_update().
    nvs_handle_t nvs;
    esp_err_t nvs_err = nvs_open("ota", NVS_READONLY, &nvs);
    char slot[16] = {0};
    size_t slot_len = sizeof(slot);
    if (nvs_err == ESP_OK) {
        if (nvs_get_str(nvs, "saved_slot", slot, &slot_len) != ESP_OK) {
            strncpy(slot, kSlotB, sizeof(slot) - 1);
        }
        nvs_close(nvs);
    } else {
        strncpy(slot, kSlotB, sizeof(slot) - 1);
    }

    const esp_partition_t* staged = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, slot);

    if (!staged) {
        ESP_LOGE(TAG, "Saved slot partition not found (%s)", slot);
        return false;
    }

    esp_app_desc_t desc = {};
    if (esp_ota_get_partition_description(staged, &desc) != ESP_OK) {
        ESP_LOGE(TAG, "Staged image is not a valid app partition");
        return false;
    }

    stage2_ui::show_progress("Installing Update", 50);

    esp_err_t err = esp_ota_set_boot_partition(staged);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set boot partition: %s", esp_err_to_name(err));
        return false;
    }

    stage2_ui::show_progress("Installing Update", 100);
    stage2_logging::info("Update installed successfully");

    return true;
}

}
