#pragma once
#include <string>
#include <vector>

namespace stage2_manifest {

struct ImageEntry {
    std::string name;
    std::string partition;
    std::string sha256;
    std::string url;
    size_t size = 0;
};

struct Manifest {
    std::string product;
    std::string board;
    std::string version;
    std::vector<ImageEntry> images;
};

bool parse_manifest_file(const char* path, Manifest& out);

}
