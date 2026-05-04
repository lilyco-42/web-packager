#pragma once
#include <string>
#include <unordered_map>
#include <windows.h>

enum class Lang { EN, ZH };

class I18n {
public:
    static I18n &Instance() {
        static I18n inst;
        return inst;
    }

    Lang GetLang() const { return m_lang; }

    void SetLang(Lang lang) { m_lang = lang; }

    void ToggleLang() {
        m_lang = (m_lang == Lang::EN) ? Lang::ZH : Lang::EN;
    }

    const char *Get(const char *key) const {
        auto it = m_strings.find(key);
        if (it == m_strings.end())
            return key;
        auto lang_it = it->second.find(m_lang);
        if (lang_it == it->second.end()) {
            // Fall back to English
            auto en_it = it->second.find(Lang::EN);
            if (en_it != it->second.end())
                return en_it->second.c_str();
            return key;
        }
        return lang_it->second.c_str();
    }

private:
    I18n() {
        LANGID lid = GetUserDefaultUILanguage();
        // 0x0804 = zh-CN, 0x0C04 = zh-HK, 0x0404 = zh-TW
        m_lang = (lid == 0x0804 || lid == 0x0C04 || lid == 0x0404) ? Lang::ZH
                                                                   : Lang::EN;

        // clang-format off
        // === Menu & Actions ===
        m_strings["File"]        = {{Lang::EN, "File"},        {Lang::ZH, "文件"}};
        m_strings["Build"]       = {{Lang::EN, "Build"},       {Lang::ZH, "构建"}};
        m_strings["Help"]        = {{Lang::EN, "Help"},        {Lang::ZH, "帮助"}};
        m_strings["Add Files"]   = {{Lang::EN, "Add Files"},   {Lang::ZH, "添加文件"}};
        m_strings["Clear All"]   = {{Lang::EN, "Clear All"},   {Lang::ZH, "清空列表"}};
        m_strings["Exit"]        = {{Lang::EN, "Exit"},        {Lang::ZH, "退出"}};
        m_strings["Build EXE"]   = {{Lang::EN, "Build EXE"},   {Lang::ZH, "生成 EXE"}};
        m_strings["Language"]    = {{Lang::EN, "Language"},    {Lang::ZH, "语言"}};
        m_strings["English"]     = {{Lang::EN, "English"},     {Lang::ZH, "English"}};
        m_strings["Chinese"]     = {{Lang::EN, "中文"},        {Lang::ZH, "中文"}};

        // === Panel Titles ===
        m_strings["Files"]       = {{Lang::EN, "Files"},       {Lang::ZH, "文件"}};
        m_strings["Preview"]     = {{Lang::EN, "Preview"},     {Lang::ZH, "预览"}};
        m_strings["Build Settings"] = {{Lang::EN, "Build Settings"}, {Lang::ZH, "构建设置"}};
        m_strings["Build Log"]   = {{Lang::EN, "Build Log"},   {Lang::ZH, "构建日志"}};

        // === File Panel ===
        m_strings["empty_hint"]  = {{Lang::EN, "Click [+ Add Files]\nto add HTML, CSS, JS files."},
                                     {Lang::ZH, "点击 [+ Add Files]\n添加 HTML, CSS, JS 文件。"}};

        // === Build Panel ===
        m_strings["App Settings"] = {{Lang::EN, "App Settings"}, {Lang::ZH, "应用设置"}};
        m_strings["App Name"]    = {{Lang::EN, "App Name"},     {Lang::ZH, "应用名称"}};
        m_strings["Window Size"] = {{Lang::EN, "Window Size"},  {Lang::ZH, "窗口大小"}};
        m_strings["Output Dir"]  = {{Lang::EN, "Output Dir"},   {Lang::ZH, "输出目录"}};
        m_strings["no_files"]    = {{Lang::EN, "Add files first!"}, {Lang::ZH, "请先添加文件！"}};
        m_strings["Generating"]  = {{Lang::EN, "Generating..."}, {Lang::ZH, "生成中..."}};
        m_strings["Compiling"]   = {{Lang::EN, "Compiling..."},  {Lang::ZH, "编译中..."}};
        m_strings["Linking"]     = {{Lang::EN, "Linking..."},    {Lang::ZH, "链接中..."}};
        m_strings["Build Done"]  = {{Lang::EN, "Build complete!"}, {Lang::ZH, "构建完成！"}};
        m_strings["Build Failed"] = {{Lang::EN, "Build failed"}, {Lang::ZH, "构建失败"}};

        // === Log messages ===
        m_strings["WebView2 unavailable"] = {{Lang::EN, "WebView2 unavailable"},
                                              {Lang::ZH, "WebView2 不可用"}};
        m_strings["webview2_hint"] = {{Lang::EN, "Install WebView2 Runtime:\nhttps://developer.microsoft.com/microsoft-edge/webview2/"},
                                       {Lang::ZH, "请安装 WebView2 Runtime:\nhttps://developer.microsoft.com/microsoft-edge/webview2/"}};
        m_strings["preview_placeholder"] = {{Lang::EN, "Add files to preview"},
                                             {Lang::ZH, "添加文件以预览"}};
        m_strings["Added"]       = {{Lang::EN, "Added"},        {Lang::ZH, "已添加"}};
        m_strings["Removed"]     = {{Lang::EN, "Removed"},      {Lang::ZH, "已移除"}};
        m_strings["Updated"]     = {{Lang::EN, "Updated"},      {Lang::ZH, "已更新"}};
        m_strings["Error"]       = {{Lang::EN, "Error"},        {Lang::ZH, "错误"}};
        m_strings["Success"]     = {{Lang::EN, "Success"},      {Lang::ZH, "成功"}};
        m_strings["skipped"]     = {{Lang::EN, "Skipped non-web file"}, {Lang::ZH, "已跳过非网页文件"}};
        m_strings["file_not_found"] = {{Lang::EN, "File not found"}, {Lang::ZH, "文件不存在"}};
        m_strings["no_files_error"] = {{Lang::EN, "No files to package"}, {Lang::ZH, "没有文件可以打包"}};
        m_strings["cmake_failed"] = {{Lang::EN, "CMake configuration failed"}, {Lang::ZH, "CMake 配置失败"}};
        m_strings["build_failed"] = {{Lang::EN, "Build failed"}, {Lang::ZH, "编译失败"}};
        m_strings["exe_generated"] = {{Lang::EN, "EXE generated:"}, {Lang::ZH, "EXE 已生成："}};
        m_strings["exe_not_found"] = {{Lang::EN, "EXE not found at"}, {Lang::ZH, "未找到生成的 EXE："}};
        m_strings["compilation_failed"] = {{Lang::EN, "Compilation failed"}, {Lang::ZH, "编译失败"}};
        m_strings["link_failed"] = {{Lang::EN, "Link failed"}, {Lang::ZH, "链接失败"}};
        m_strings["Elapsed"] = {{Lang::EN, "Elapsed"}, {Lang::ZH, "已用时"}};
        m_strings["details"] = {{Lang::EN, "Details"}, {Lang::ZH, "详情"}};
        m_strings["Close"] = {{Lang::EN, "Close"}, {Lang::ZH, "关闭"}};
        m_strings["Warning"]     = {{Lang::EN, "Warning"},        {Lang::ZH, "警告"}};
        m_strings["Remove"]      = {{Lang::EN, "Remove"},         {Lang::ZH, "移除"}};
        m_strings["Removed"]     = {{Lang::EN, "Removed"},        {Lang::ZH, "已移除"}};
        m_strings["Width"]       = {{Lang::EN, "Width"},          {Lang::ZH, "宽度"}};
        m_strings["Height"]      = {{Lang::EN, "Height"},         {Lang::ZH, "高度"}};
        // clang-format on
    }

    Lang m_lang = Lang::EN;
    std::unordered_map<std::string, std::unordered_map<Lang, std::string>>
        m_strings;
};

#define _(key) I18n::Instance().Get(key)
