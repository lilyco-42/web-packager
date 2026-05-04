#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>

struct ProjectFile;

namespace project_generator {

// Combine HTML, CSS, JS files into a single self-contained HTML
inline std::string combine_files(const std::vector<ProjectFile>& files) {
    std::string html_content;
    std::string css_content;
    std::string js_content;
    std::string main_html;

    for (const auto& f : files) {
        if (f.extension == "html") {
            main_html = f.content;
        } else if (f.extension == "css") {
            css_content += f.content + "\n";
        } else if (f.extension == "js") {
            js_content += f.content + "\n";
        }
    }

    if (main_html.empty()) {
        main_html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"></head><body></body></html>";
    }

    // Inject CSS into <head>
    if (!css_content.empty()) {
        std::string style_tag = "<style>\n" + css_content + "\n</style>\n";
        size_t head_end = main_html.find("</head>");
        if (head_end != std::string::npos) {
            main_html.insert(head_end, style_tag);
        } else {
            // No <head> tag, create one
            size_t html_tag_end = main_html.find(">");
            size_t body_start = main_html.find("<body");
            if (body_start != std::string::npos && body_start < html_tag_end) {
                html_tag_end = body_start;
            }
            std::string head_section = "\n<head>\n" + style_tag + "\n</head>\n";
            if (body_start != std::string::npos) {
                main_html.insert(body_start, head_section);
            } else {
                main_html += head_section;
            }
        }
    }

    // Inject JS before </body>
    if (!js_content.empty()) {
        std::string script_tag = "\n<script>\n" + js_content + "\n</script>\n";
        size_t body_end = main_html.find("</body>");
        if (body_end != std::string::npos) {
            main_html.insert(body_end, script_tag);
        } else {
            main_html += script_tag;
        }
    }

    return main_html;
}

// Escape a string for use in a C++ raw string literal
// We use R"html(...)html" so we need to handle cases where )html appears in content
inline std::string escape_for_cpp_raw_string(const std::string& s) {
    // Use a more complex delimiter if needed
    std::string delimiter = "html";
    std::string closing = ")" + delimiter + "\"";

    // Check if content contains the closing sequence
    if (s.find(closing) != std::string::npos) {
        // Use a different delimiter
        for (int i = 0; i < 100; i++) {
            std::string alt_delim = "html" + std::to_string(i);
            std::string alt_close = ")" + alt_delim + "\"";
            if (s.find(alt_close) == std::string::npos) {
                delimiter = alt_delim;
                break;
            }
        }
    }

    return "R\"" + delimiter + "(" + s + ")" + delimiter + "\"";
}

// Generate main.cc with embedded HTML
// Uses a temp file + navigate() instead of set_html() so that localStorage
// works (NavigateToString gives about:blank origin which blocks localStorage).
inline std::string generate_main_cc(const std::string& combined_html,
                                     const std::string& app_name,
                                     int window_width = 800,
                                     int window_height = 600) {
    std::ostringstream out;

    out << "#include \"webview/webview.h\"\n";
    out << "#include <iostream>\n";
    out << "#include <fstream>\n";
    out << "#include <filesystem>\n";
    out << "#include <sstream>\n";
    out << "\n";
    out << "constexpr const char* html_content =\n";
    out << "    " << escape_for_cpp_raw_string(combined_html) << ";\n";
    out << "\n";
    out << "static std::string url_encode_path(const std::string &s) {\n";
    out << "  std::ostringstream out;\n";
    out << "  for (char c : s) {\n";
    out << "    if (c == ' ') out << \"%20\";\n";
    out << "    else out << c;\n";
    out << "  }\n";
    out << "  return out.str();\n";
    out << "}\n";
    out << "\n";
    out << "#ifdef _WIN32\n";
    out << "int WINAPI WinMain(HINSTANCE /*hInst*/, HINSTANCE /*hPrevInst*/,\n";
    out << "                   LPSTR /*lpCmdLine*/, int /*nCmdShow*/) {\n";
    out << "#else\n";
    out << "int main() {\n";
    out << "#endif\n";
    out << "  try {\n";
    out << "    // Write HTML to temp file so that file:// origin gives localStorage access\n";
    out << "    auto tmp = std::filesystem::temp_directory_path() / \"" << app_name << "_page.html\";\n";
    out << "    { std::ofstream f(tmp); f << html_content; }\n";
    out << "\n";
    out << "    webview::webview w(false, nullptr);\n";
    out << "    w.set_title(\"" << app_name << "\");\n";
    out << "    w.set_size(" << window_width << ", " << window_height << ", WEBVIEW_HINT_NONE);\n";
    out << "    w.navigate(\"file:///\" + url_encode_path(tmp.generic_string()));\n";
    out << "    w.run();\n";
    out << "    std::error_code ec;\n";
    out << "    std::filesystem::remove(tmp, ec);\n";
    out << "  } catch (const webview::exception& e) {\n";
    out << "    std::cerr << e.what() << '\\n';\n";
    out << "    return 1;\n";
    out << "  }\n";
    out << "  return 0;\n";
    out << "}\n";

    return out.str();
}

} // namespace project_generator
