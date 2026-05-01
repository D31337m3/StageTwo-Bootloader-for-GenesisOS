#include "stage2_manifest.hpp"
#include "stage2_logging.hpp"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "string.h"
#include "stdio.h"

static const char* TAG = "stage2_manifest";

namespace stage2_manifest {

static const char* skip_whitespace(const char* p)
{
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

static const char* parse_string(const char* p, std::string& out)
{
    p = skip_whitespace(p);
    if (*p != '"') return p;
    p++;

    out.clear();
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            p++;
            out += *p++;
        } else {
            out += *p++;
        }
    }
    if (*p == '"') p++;
    return p;
}

static const char* parse_number(const char* p, size_t& out)
{
    p = skip_whitespace(p);
    out = 0;
    while (*p >= '0' && *p <= '9') {
        out = out * 10 + (*p - '0');
        p++;
    }
    return p;
}

static const char* parse_value(const char* p, const char* key, std::string& out)
{
    size_t key_len = strlen(key);
    while (*p) {
        p = skip_whitespace(p);
        if (*p == '}') return p;

        // Parse key
        std::string k;
        p = parse_string(p, k);
        p = skip_whitespace(p);
        if (*p == ':') p++;
        p = skip_whitespace(p);

        if (k == key) {
            // Check if value is string or number
            if (*p == '"') {
                p = parse_string(p, out);
            } else {
                size_t num;
                p = parse_number(p, num);
                char buf[32];
                snprintf(buf, sizeof(buf), "%zu", num);
                out = buf;
            }
            return p;
        }

        // Skip value
        if (*p == '"') {
            p = parse_string(p, out);
        } else if (*p == '{') {
            int depth = 1;
            p++;
            while (*p && depth > 0) {
                if (*p == '{') depth++;
                else if (*p == '}') depth--;
                p++;
            }
        } else if (*p == '[') {
            int depth = 1;
            p++;
            while (*p && depth > 0) {
                if (*p == '[') depth++;
                else if (*p == ']') depth--;
                p++;
            }
        } else {
            while (*p && *p != ',' && *p != '}' && *p != ']') p++;
        }

        if (*p == ',') p++;
    }
    return p;
}

bool parse_manifest_file(const char* path, Manifest& out)
{
    if (!path) {
        ESP_LOGE(TAG, "Null path provided");
        return false;
    }

    ESP_LOGI(TAG, "Parsing manifest: %s", path);

    // Open file
    FILE* f = fopen(path, "r");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open manifest file: %s", path);
        return false;
    }

    // Read file content
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* buffer = (char*)malloc(len + 1);
    if (!buffer) {
        fclose(f);
        return false;
    }

    fread(buffer, 1, len, f);
    buffer[len] = '\0';
    fclose(f);

    // Parse JSON
    const char* p = buffer;

    // Find "product"
    p = parse_value(p, "product", out.product);
    ESP_LOGI(TAG, "  product: %s", out.product.c_str());

    // Find "board"
    p = parse_value(p, "board", out.board);
    ESP_LOGI(TAG, "  board: %s", out.board.c_str());

    // Find "version"
    p = parse_value(p, "version", out.version);
    ESP_LOGI(TAG, "  version: %s", out.version.c_str());

    // Find "images" array
    p = skip_whitespace(p);
    if (*p == '{') {
        p++;
        while (*p && *p != '}') {
            p = skip_whitespace(p);
            std::string key;
            p = parse_string(p, key);
            p = skip_whitespace(p);
            if (*p == ':') p++;
            p = skip_whitespace(p);

            if (key == "images" && *p == '[') {
                p++;
                while (*p && *p != ']') {
                    p = skip_whitespace(p);
                    if (*p == '{') {
                        p++;
                        ImageEntry entry;
                        while (*p && *p != '}') {
                            p = skip_whitespace(p);
                            std::string k;
                            p = parse_string(p, k);
                            p = skip_whitespace(p);
                            if (*p == ':') p++;
                            p = skip_whitespace(p);

                            std::string v;
                            if (*p == '"') {
                                p = parse_string(p, v);
                            } else {
                                size_t num;
                                p = parse_number(p, num);
                                char buf[32];
                                snprintf(buf, sizeof(buf), "%zu", num);
                                v = buf;
                            }

                            if (k == "name") entry.name = v;
                            else if (k == "partition") entry.partition = v;
                            else if (k == "sha256") entry.sha256 = v;
                            else if (k == "url") entry.url = v;
                            else if (k == "size") entry.size = atoi(v.c_str());

                            p = skip_whitespace(p);
                            if (*p == ',') p++;
                        }
                        if (*p == '}') p++;

                        if (!entry.name.empty()) {
                            out.images.push_back(entry);
                            ESP_LOGI(TAG, "    image: %s (%s)", entry.name.c_str(), entry.partition.c_str());
                        }
                    }
                    p = skip_whitespace(p);
                    if (*p == ',') p++;
                }
                if (*p == ']') p++;
            } else {
                // Skip other fields
                if (*p == '"') {
                    std::string v;
                    p = parse_string(p, v);
                } else if (*p == '{') {
                    int depth = 1;
                    p++;
                    while (*p && depth > 0) {
                        if (*p == '{') depth++;
                        else if (*p == '}') depth--;
                        p++;
                    }
                }
                p = skip_whitespace(p);
                if (*p == ',') p++;
            }
        }
    }

    free(buffer);
    stage2_logging::info("Manifest parsed successfully");
    return true;
}

}
