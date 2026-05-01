#pragma once
#include <string>
#include <vector>

namespace stage2_ota {

struct UpdateManifest {
    std::string version;
    std::string url;
    std::string sha256;
    size_t size;
    std::string release_notes;
};

struct DownloadProgress {
    int percent;
    size_t downloaded;
    size_t total;
};

bool download_update_flow();
bool install_saved_update();
bool fetch_manifest(UpdateManifest* manifest);
bool download_update(const UpdateManifest& manifest, DownloadProgress* progress);
bool validate_image(const uint8_t* data, size_t len, const std::string& expected_sha256);
bool save_update(const uint8_t* data, size_t len);
bool has_saved_update();
bool get_saved_update_info(UpdateManifest* manifest);

}
