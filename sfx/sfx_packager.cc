// SFX packager tool - builds self-extracting web_packager.exe
// Usage: sfx_packager <stub.exe> <dist_dir> <output.exe>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <cstdint>
#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: sfx_packager <stub.exe> <dist_dir> <output.exe>\n");
        return 1;
    }

    std::string stub_path = argv[1];
    std::string dist_dir = argv[2];
    std::string out_path = argv[3];

    // Read stub
    std::ifstream f_stub(stub_path, std::ios::binary | std::ios::ate);
    if (!f_stub) { fprintf(stderr, "Can't read stub\n"); return 1; }
    size_t stub_sz = (size_t)f_stub.tellg();
    f_stub.seekg(0);
    std::vector<char> out(stub_sz);
    f_stub.read(out.data(), stub_sz);
    f_stub.close();

    // Collect files from dist_dir and build package data
    std::vector<char> pkg;
    auto dist_prefix = fs::absolute(dist_dir).string();

    for (auto &entry : fs::recursive_directory_iterator(dist_dir)) {
        if (!entry.is_regular_file()) continue;

        auto full_path = entry.path().string();
        auto rel_path = fs::relative(full_path, dist_prefix).string();
        std::replace(rel_path.begin(), rel_path.end(), '/', '\\');

        auto content = entry.file_size();
        std::ifstream f_in(full_path, std::ios::binary);
        std::vector<char> buf(content);
        if (content > 0) f_in.read(buf.data(), content);
        f_in.close();

        // Write entry: name_len(2) + name + data_len(4) + data
        uint16_t nl = (uint16_t)rel_path.size();
        uint32_t dl = (uint32_t)content;

        auto old_sz = pkg.size();
        pkg.resize(old_sz + 2 + nl + 4 + dl);
        memcpy(&pkg[old_sz], &nl, 2);
        memcpy(&pkg[old_sz + 2], rel_path.data(), nl);
        memcpy(&pkg[old_sz + 2 + nl], &dl, 4);
        if (content > 0)
            memcpy(&pkg[old_sz + 2 + nl + 4], buf.data(), dl);
    }

    // End marker
    uint16_t end = 0;
    auto old_sz = pkg.size();
    pkg.resize(old_sz + 2 + 1);
    memcpy(&pkg[old_sz], &end, 2);

    // Write package data after stub
    size_t pkg_offset = out.size();
    out.insert(out.end(), pkg.begin(), pkg.end());

    // Write offset (4 bytes) + "SFX_END" marker
    uint32_t pkg_off_le = (uint32_t)pkg_offset;
    out.resize(out.size() + 4);
    memcpy(&out[out.size() - 4], &pkg_off_le, 4);
    std::string m = "SFX_END";
    out.insert(out.end(), m.begin(), m.end());

    // Write output
    std::ofstream f_out(out_path, std::ios::binary);
    f_out.write(out.data(), out.size());
    f_out.close();

    printf("SFX created: %s (%zu bytes)\n", out_path.c_str(), out.size());
    return 0;
}
