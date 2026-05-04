// SFX stub - self-extracting launcher for WebPackager
// Uses Windows built-in APIs only.
//
// Build:   g++ -O3 -mwindows -static -s sfx_stub.cc -o sfx_stub.exe
// Package: sfx_packager sfx_stub.exe dist_dir output.exe

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <cstdio>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    char self[MAX_PATH];
    GetModuleFileNameA(nullptr, self, MAX_PATH);

    // Read self into memory
    std::ifstream f(self, std::ios::binary | std::ios::ate);
    if (!f) { MessageBoxA(nullptr, "Read error", nullptr, MB_ICONERROR); return 1; }
    size_t sz = (size_t)f.tellg();
    f.seekg(0);
    std::vector<char> data(sz);
    if (!f.read(data.data(), sz)) { MessageBoxA(nullptr, "Read error", nullptr, MB_ICONERROR); return 1; }
    f.close();

    // Find "SFX_END" marker near the end
    std::string marker = "SFX_END";
    std::string data_str(data.data(), data.size());
    auto marker_pos = data_str.rfind(marker);
    if (marker_pos == std::string::npos) {
        MessageBoxA(nullptr, "Corrupted package (no marker)", nullptr, MB_ICONERROR);
        return 1;
    }
    if (marker_pos < 4) {
        MessageBoxA(nullptr, "Corrupted package (bad offset)", nullptr, MB_ICONERROR);
        return 1;
    }

    // 4 bytes before marker = package data offset
    uint32_t pkg_offset;
    memcpy(&pkg_offset, &data[marker_pos - 4], 4);

    // Temp dir
    char tmp_dir[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp_dir);
    std::string out_dir = std::string(tmp_dir) + "WebPackager\\";
    CreateDirectoryA(out_dir.c_str(), nullptr);

    // Parse file entries starting at pkg_offset
    size_t pos = pkg_offset;
    while (pos + 2 <= data.size()) {
        uint16_t nl;
        memcpy(&nl, &data[pos], 2);
        pos += 2;
        if (nl == 0) break; // end marker

        if (pos + nl + 4 > data.size()) break;
        std::string name(&data[pos], nl);
        pos += nl;

        uint32_t dl;
        memcpy(&dl, &data[pos], 4);
        pos += 4;
        if (pos + dl > data.size()) break;

        // Create all subdirectories in the path
        std::string full = out_dir + name;
        for (size_t i = 0; i < full.size(); ++i) {
            if (full[i] == '\\') {
                char c = full[i];
                full[i] = '\0';
                CreateDirectoryA(full.c_str(), nullptr);
                full[i] = c;
            }
        }

        // Write file
        std::ofstream out(full, std::ios::binary);
        out.write(&data[pos], dl);
        pos += dl;
    }

    // Launch web_packager from temp
    std::string exe_path = out_dir + "web_packager.exe";
    ShellExecuteA(nullptr, "open", exe_path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return 0;
}
