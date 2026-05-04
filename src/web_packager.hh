#pragma once
#include <string>
#include <vector>
#include <ctime>
#include <filesystem>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
namespace fs = std::filesystem;

struct ProjectFile {
    fs::path path;
    std::string name;
    std::string extension;
    std::string content;
    std::time_t last_modified = 0;
};

struct BuildSettings {
    std::string app_name = "MyApp";
    int window_width = 800;
    int window_height = 600;
    std::string output_dir = "./output";
};

class WebPackager {
public:
    WebPackager();
    ~WebPackager();

    // Panels (called from main.cc)
    void RenderFilePanel();
    void RenderBuildPanel();
    void RenderLogPanel();

    // File management
    void AddFiles();
    void RemoveFile(int index);
    const std::vector<ProjectFile>& GetFiles() const { return m_files; }

    // Preview
    std::string BuildCombinedHtml() const;
    bool NeedsPreviewUpdate() const;
    void ClearPreviewDirty();

    // Build
    void BuildExe();
    void RenderBuildProgress(); // overlay during building

    // Log
    void Log(const std::string& msg);
    const std::vector<std::string>& GetLogs() const { return m_logs; }
    bool IsBuilding() const { return m_building; }
    bool IsProgressVisible() const { return m_show_progress; }
    std::mutex& GetLogMutex() { return m_log_mutex; }

    // Preview host (set externally)
    void* m_preview_host_hwnd = nullptr;

private:
    void LoadFile(const std::string& path);

    void BuildExeThread();

    std::vector<ProjectFile> m_files;
    std::vector<std::string> m_logs;
    mutable std::mutex m_log_mutex;
    BuildSettings m_settings;
    bool m_preview_dirty = false;

    std::atomic<bool> m_building{false};
    std::atomic<bool> m_show_progress{false};
    std::atomic<float> m_build_progress{0.0f};
    std::string m_build_status;
    mutable std::mutex m_status_mutex;
    std::thread m_build_thread;
    std::chrono::steady_clock::time_point m_build_start_time;
};
