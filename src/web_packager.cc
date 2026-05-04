#include "web_packager.hh"
#include "project_generator.hh"
#include "i18n.hh"

#include "imgui.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <windows.h>

namespace fs = std::filesystem;

WebPackager::WebPackager() {}
WebPackager::~WebPackager() {
    if (m_build_thread.joinable())
        m_build_thread.join();
}

void WebPackager::RenderFilePanel() {
    if (ImGui::Button(_("Add Files"), ImVec2(-1, 0))) {
        AddFiles();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", _("Add Files"));

    ImGui::SeparatorText((std::string(_("Files")) + " (" +
                          std::to_string(m_files.size()) + ")")
                             .c_str());

    if (m_files.empty()) {
        ImGui::TextWrapped("%s", _("empty_hint"));
    }

    int remove_idx = -1;
    for (int i = 0; i < (int)m_files.size(); ++i) {
        auto &f = m_files[i];
        const char *icon = "[?]";
        if (f.extension == "html")
            icon = "[H]";
        else if (f.extension == "css")
            icon = "[C]";
        else if (f.extension == "js")
            icon = "[J]";

        ImGui::PushID(i);
        ImGui::BulletText("%s %s", icon, f.name.c_str());
        if (ImGui::BeginPopupContextItem("ctx")) {
            if (ImGui::MenuItem(_("Remove")))
                remove_idx = i;
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (remove_idx >= 0)
        RemoveFile(remove_idx);
}

void WebPackager::RenderBuildPanel() {
    ImGui::SeparatorText(_("App Settings"));

    char buf[256];
    strncpy(buf, m_settings.app_name.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    if (ImGui::InputText(_("App Name"), buf, sizeof(buf)))
        m_settings.app_name = buf;

    ImGui::InputInt(_("Width"), &m_settings.window_width);
    ImGui::InputInt(_("Height"), &m_settings.window_height);

    char dir[512];
    strncpy(dir, m_settings.output_dir.c_str(), sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    if (ImGui::InputText(_("Output Dir"), dir, sizeof(dir)))
        m_settings.output_dir = dir;

    ImGui::Separator();

    bool can_build = !m_files.empty() && !m_building;

    if (can_build) {
        if (ImGui::Button(_("Build EXE"), ImVec2(-1, 48)))
            BuildExe();
    } else {
        ImGui::BeginDisabled();
        ImGui::Button(_("Build EXE"), ImVec2(-1, 48));
        ImGui::EndDisabled();
        if (m_files.empty())
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "%s", _("no_files"));
    }

    // Inline progress bar when building
    if (m_building) {
        ImGui::Spacing();
        auto status = [&]() {
            std::lock_guard<std::mutex> lk(m_status_mutex);
            return m_build_status;
        }();
        ImGui::ProgressBar(m_build_progress, ImVec2(-1, 0), status.c_str());
    }
}

void WebPackager::RenderLogPanel() {
    ImGui::BeginChild("log", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    // Thread-safe snapshot of logs
    std::vector<std::string> logs_snapshot;
    {
        std::lock_guard<std::mutex> lock(m_log_mutex);
        logs_snapshot = m_logs;
    }

    for (const auto &msg : logs_snapshot) {
        ImU32 col = 0xFFFFFFFF;
        if (msg.find(_("Error")) != std::string::npos ||
            msg.find("失败") != std::string::npos)
            col = 0xFF4D4DFF;
        else if (msg.find(_("Success")) != std::string::npos ||
                 msg.find("成功") != std::string::npos)
            col = 0xFF4DFF4D;
        ImGui::TextColored(ImColor(col), "%s", msg.c_str());
    }

    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

void WebPackager::AddFiles() {
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter =
        "Web Files\0*.html;*.htm;*.css;*.js\0"
        "HTML\0*.html;*.htm\0"
        "CSS\0*.css\0"
        "JavaScript\0*.js\0"
        "All Files\0*.*\0";
    char files[65536] = {};
    ofn.lpstrFile = files;
    ofn.nMaxFile = sizeof(files);
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST |
                OFN_HIDEREADONLY | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) {
        char *p = files;
        std::string dir = p;
        p += dir.size() + 1;
        if (*p == 0) {
            LoadFile(dir);
        } else {
            while (*p) {
                LoadFile(dir + "\\" + p);
                p += strlen(p) + 1;
            }
        }
    }
}

void WebPackager::LoadFile(const std::string &path) {
    // Convert ANSI path (from GetOpenFileNameA) to native wide path for
    // correct Chinese-character handling regardless of C++ locale
    int wlen = MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wpath(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, &wpath[0], wlen);
    fs::path p(wpath.c_str()); // native wchar_t path, no locale conversion

    if (!fs::exists(p)) {
        Log(std::string(_("Error")) + ": " + _("file_not_found") + " " +
            p.string());
        return;
    }

    std::ifstream file(p, std::ios::binary);
    if (!file) {
        Log(std::string(_("Error")) + ": " + _("file_not_found") + " " +
            p.string());
        return;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".htm")
        ext = "html";
    else if (!ext.empty() && ext[0] == '.')
        ext = ext.substr(1);

    if (ext != "html" && ext != "css" && ext != "js") {
        Log(std::string(_("Warning")) + ": " + _("skipped") + ": " +
            p.filename().string());
        return;
    }

    auto mod_time = fs::last_write_time(p).time_since_epoch().count();

    for (auto &existing : m_files) {
        if (existing.path == p) {
            existing.content = content;
            existing.last_modified = mod_time;
            Log(std::string(_("Updated")) + ": " + p.filename().string());
            m_preview_dirty = true;
            return;
        }
    }

    ProjectFile pf;
    pf.path = p;
    pf.name = p.filename().string();
    pf.extension = ext;
    pf.content = content;
    pf.last_modified = mod_time;
    m_files.push_back(std::move(pf));
    m_preview_dirty = true;
    Log(std::string(_("Added")) + ": " + p.filename().string());
}

void WebPackager::RemoveFile(int index) {
    if (index >= 0 && index < (int)m_files.size()) {
        Log(std::string(_("Removed")) + ": " + m_files[index].name);
        m_files.erase(m_files.begin() + index);
        m_preview_dirty = true;
    }
}

std::string WebPackager::BuildCombinedHtml() const {
    return project_generator::combine_files(m_files);
}

bool WebPackager::NeedsPreviewUpdate() const {
    if (m_preview_dirty)
        return true;
    for (const auto &f : m_files) {
        auto t = fs::last_write_time(f.path).time_since_epoch().count();
        if (t != f.last_modified)
            return true;
    }
    return false;
}

void WebPackager::ClearPreviewDirty() { m_preview_dirty = false; }

void WebPackager::RenderBuildProgress() {
    if (!m_show_progress)
        return;

    ImGui::OpenPopup("##build_progress");

    // Scale popup size with DPI so it displays fully on high-DPI screens
    float dpi_scale = ImGui::GetFontSize() / 16.0f;
    if (dpi_scale < 1.0f) dpi_scale = 1.0f;
    ImVec2 popup_size(480 * dpi_scale, 340 * dpi_scale);

    ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(popup_size, ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    if (ImGui::BeginPopupModal("##build_progress", nullptr,
                               ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoMove)) {
        ImGui::TextUnformatted(_("Build EXE"));
        ImGui::Separator();

        auto now = std::chrono::steady_clock::now();
        auto sec = std::chrono::duration_cast<std::chrono::seconds>(
                       now - m_build_start_time)
                       .count();
        ImGui::Text("%s: %lld:%02lld", _("Elapsed"), sec / 60, sec % 60);

        ImGui::Spacing();
        ImGui::Spacing();
        auto status = [&]() {
            std::lock_guard<std::mutex> lk(m_status_mutex);
            return m_build_status;
        }();
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.1f, 0.7f, 0.3f, 1.0f));
        ImGui::ProgressBar(m_build_progress, ImVec2(-1, 28 * dpi_scale), status.c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::Spacing();

        ImGui::SeparatorText(_("details"));
        float log_height = 160 * dpi_scale;
        ImGui::BeginChild("##build_log", ImVec2(0, log_height), true,
                          ImGuiWindowFlags_HorizontalScrollbar);
        {
            std::lock_guard<std::mutex> lock(m_log_mutex);
            for (const auto &msg : m_logs) {
                ImU32 col = 0xFFFFFFFF;
                if (msg.find(_("Error")) != std::string::npos ||
                    msg.find("失败") != std::string::npos)
                    col = 0xFF4D4DFF;
                else if (msg.find(_("Success")) != std::string::npos ||
                         msg.find("成功") != std::string::npos)
                    col = 0xFF4DFF4D;
                ImGui::TextColored(ImColor(col), "%s", msg.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10)
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        ImGui::Separator();
        float bw = 100.0f * dpi_scale;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - bw - 8 * dpi_scale);
        if (!m_building) {
            if (ImGui::Button(_("Close"), ImVec2(bw, 28 * dpi_scale))) {
                m_show_progress = false;
                ImGui::CloseCurrentPopup();
            }
        } else {
            ImGui::BeginDisabled();
            ImGui::Button(_("Close"), ImVec2(bw, 28));
            ImGui::EndDisabled();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

void WebPackager::Log(const std::string &msg) {
    std::lock_guard<std::mutex> lock(m_log_mutex);
    m_logs.push_back(msg);
    if (m_logs.size() > 1000)
        m_logs.erase(m_logs.begin(), m_logs.begin() + (m_logs.size() - 1000));
}

// Run a shell command without showing a console window.
// Returns the process exit code, or -1 on failure to launch.
// If output is non-null, captures stdout+stderr into it.
static int run_hidden(const std::string &cmd, const std::string &cwd,
                      std::string *output = nullptr) {
    std::string full = "cmd.exe /c " + cmd;
    // CreateProcess needs a mutable buffer for lpCommandLine
    std::vector<char> buf(full.begin(), full.end());
    buf.push_back('\0');

    // cwd must be null-terminated; an empty string means inherit
    std::vector<char> cwdbuf(cwd.begin(), cwd.end());
    cwdbuf.push_back('\0');
    const char *cwd_ptr = cwd.empty() ? nullptr : cwdbuf.data();

    // Pipe for capturing output
    HANDLE hStdoutRead = nullptr, hStdoutWrite = nullptr;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    bool capture = (output != nullptr);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    if (capture) {
        if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0))
            return -1;
        SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hStdoutWrite;
        si.hStdError = hStdoutWrite;
    }

    if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr,
                        capture ? TRUE : FALSE, CREATE_NO_WINDOW, nullptr,
                        cwd_ptr, &si, &pi)) {
        if (capture) {
            CloseHandle(hStdoutRead);
            CloseHandle(hStdoutWrite);
        }
        return -1;
    }

    if (capture)
        CloseHandle(hStdoutWrite); // parent no longer needs write end

    HANDLE h = pi.hProcess;
    DWORD nHandles = capture ? 2 : 1;
    HANDLE hEvents[2] = {h, hStdoutRead};

    // Pump messages while waiting so the window stays responsive
    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(nHandles, hEvents, FALSE,
                                            INFINITE, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0) {
            // Process finished -- drain remaining pipe data
            if (capture) {
                CHAR buf2[4096];
                DWORD dwRead = 0;
                while (PeekNamedPipe(hStdoutRead, nullptr, 0, nullptr,
                                     &dwRead, nullptr) &&
                       dwRead > 0) {
                    if (!ReadFile(hStdoutRead, buf2,
                                  std::min<size_t>(sizeof(buf2), dwRead), &dwRead,
                                  nullptr) ||
                        dwRead == 0)
                        break;
                    buf2[dwRead] = '\0';
                    *output += buf2;
                }
            }
            break;
        }
        if (capture && r == WAIT_OBJECT_0 + 1) {
            // Pipe data available
            CHAR buf2[4096];
            DWORD dwRead = 0;
            if (ReadFile(hStdoutRead, buf2, sizeof(buf2) - 1, &dwRead,
                         nullptr) &&
                dwRead > 0) {
                buf2[dwRead] = '\0';
                *output += buf2;
            }
        } else if (r == WAIT_OBJECT_0 + nHandles) {
            // Windows message in queue
            MSG msg;
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        } else {
            break;
        }
    }

    DWORD ec = 0;
    GetExitCodeProcess(pi.hProcess, &ec);
    if (capture) {
        CloseHandle(hStdoutRead);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(ec);
}

void WebPackager::BuildExe() {
    if (m_files.empty()) {
        Log(std::string(_("Error")) + ": " + _("no_files_error"));
        return;
    }
    if (m_building)
        return;

    m_building = true;
    m_show_progress = true;
    m_build_progress = 0.0f;
    m_build_start_time = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lk(m_status_mutex);
        m_build_status = _("Generating");
    }

    if (m_build_thread.joinable())
        m_build_thread.join();
    m_build_thread = std::thread(&WebPackager::BuildExeThread, this);
}

void WebPackager::BuildExeThread() {
    try {
        char exe_path[MAX_PATH];
        GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
        fs::path exe_dir = fs::path(exe_path).parent_path();
        fs::path lib_dir = exe_dir / "lib";

        fs::path webview_lib = lib_dir / "webview" / "libwebview.a";
        if (!fs::exists(webview_lib)) {
            Log(std::string(_("Error")) +
                ": Bundled webview library not found at " +
                webview_lib.string());
            Log("(exe dir: " + exe_dir.string() + ")");
            m_building = false;
            return;
        }

        fs::path output_dir = fs::absolute(m_settings.output_dir);
        fs::create_directories(output_dir);

        std::string html = project_generator::combine_files(m_files);
        std::string main_cc = project_generator::generate_main_cc(
            html, m_settings.app_name, m_settings.window_width,
            m_settings.window_height);

        fs::path build_dir = output_dir / "build_tmp";
        fs::remove_all(build_dir);
        fs::create_directories(build_dir);
        std::ofstream(build_dir / "main.cc") << main_cc;
        Log(std::string(_("Generated")) + ": main.cc");

        m_build_progress = 0.15f;
        {
            std::lock_guard<std::mutex> lk(m_status_mutex);
            m_build_status = _("Compiling");
        }

        auto wv_inc = (lib_dir / "webview" / "include").string();
        auto wv2_inc = (lib_dir / "webview2" / "include").string();
        auto wv_lib = (lib_dir / "webview").string();
        auto wv2_lib_x64 =
            (lib_dir / "webview2" / "lib" / "x64" / "WebView2LoaderStatic.lib")
                .string();
        auto src_file = (build_dir / "main.cc").string();
        auto obj_file = (build_dir / "main.o").string();
        auto out_file =
            (output_dir / (m_settings.app_name + ".exe")).string();

        // Step 1: compile
        {
            std::string cmd = "g++ -std=c++17 -O3 -DNDEBUG -DWEBVIEW_STATIC"
                " -I\"" + wv_inc + "\""
                " -I\"" + wv2_inc + "\""
                " -c \"" + src_file + "\" -o \"" + obj_file + "\""
                " 2>&1";

            Log("> g++ -c main.cc ...");
            std::string out;
            if (run_hidden(cmd, build_dir.string(), &out) != 0) {
                Log(std::string(_("Error")) + ": " + _("compilation_failed"));
                if (!out.empty())
                    Log(out);
                m_building = false;
                return;
            }
            if (!out.empty())
                Log(out);
        }

        m_build_progress = 0.5f;
        {
            std::lock_guard<std::mutex> lk(m_status_mutex);
            m_build_status = _("Linking");
        }

        // Step 2: link
        {
            std::string cmd = "g++ \"" + obj_file + "\""
                " -o \"" + out_file + "\""
                " -L\"" + wv_lib + "\" -lwebview"
                " \"" + wv2_lib_x64 + "\""
                " -static -static-libgcc -static-libstdc++"
                " -mwindows"
                " -ld3d11 -ldxgi -ldwmapi -ld3dcompiler"
                " -ladvapi32 -lole32 -lshell32 -lshlwapi"
                " -luser32 -lkernel32 -lgdi32 -lwinspool"
                " -loleaut32 -luuid -lcomdlg32"
                " 2>&1";

            Log("> g++ -o " + m_settings.app_name + ".exe ...");
            std::string out;
            if (run_hidden(cmd, build_dir.string(), &out) != 0) {
                Log(std::string(_("Error")) + ": " + _("link_failed"));
                if (!out.empty())
                    Log(out);
                m_building = false;
                return;
            }
            if (!out.empty())
                Log(out);
        }

        fs::remove_all(build_dir);

        m_build_progress = 1.0f;
        {
            std::lock_guard<std::mutex> lk(m_status_mutex);
            m_build_status = _("Build Done");
        }
        Log(std::string(_("Success")) + ": " + _("exe_generated") + " " +
            out_file);

    } catch (const std::exception &e) {
        Log(std::string(_("Error")) + ": " + e.what());
    }

    m_building = false;
}
