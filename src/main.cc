// WebPackager - ImGui + WebView2 web-to-desktop packager
// Cross-platform via GLFW + OpenGL3

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include "webview/webview.h"
#include "web_packager.hh"
#include "i18n.hh"

#include <cstdio>
#include <locale>
#include <stdexcept>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>

static HWND g_previewHost = nullptr;
static HWND g_webviewWidget = nullptr;

LRESULT CALLBACK PreviewHostProc(HWND hwnd, UINT msg, WPARAM wParam,
                                 LPARAM lParam) {
    if (msg == WM_SIZE) {
        HWND child = GetWindow(hwnd, GW_CHILD);
        if (child)
            SetWindowPos(child, nullptr, 0, 0, LOWORD(lParam), HIWORD(lParam),
                         SWP_NOZORDER);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
#endif

static GLFWwindow *g_window = nullptr;
static webview::webview *g_webview = nullptr;

// DPI scaling
static float g_base_dpi = 1.0f;

static void glfw_error_callback(int error, const char *desc) {
    std::fprintf(stderr, "GLFW Error %d: %s\n", error, desc);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    try {
        std::locale::global(std::locale(""));
    } catch (const std::runtime_error &) {
    }

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    g_window = glfwCreateWindow(1400, 900,
                                "WebPackager - Web to EXE Packager", nullptr,
                                nullptr);
    if (!g_window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

#ifdef _WIN32
    float xscale, yscale;
    glfwGetWindowContentScale(g_window, &xscale, &yscale);
    g_base_dpi = xscale;
#else
    g_base_dpi = 1.0f;
#endif

    // Load CJK font (Windows only)
#ifdef _WIN32
    {
        ImFontConfig font_cfg;
        font_cfg.FontNo = 0;
        float base_font_size = 16.0f;
        const ImWchar *ranges =
            io.Fonts->GetGlyphRangesChineseSimplifiedCommon();
        bool cjk_loaded = false;
        const char *cjk_fonts[] = {
            "C:\\Windows\\Fonts\\msyh.ttc", "C:\\Windows\\Fonts\\simhei.ttf",
            "C:\\Windows\\Fonts\\SIMHEI.TTF", "C:\\Windows\\Fonts\\msyhbd.ttc",
        };
        for (auto *p : cjk_fonts) {
            if (GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES) {
                if (io.Fonts->AddFontFromFileTTF(p, base_font_size, &font_cfg,
                                                 ranges)) {
                    cjk_loaded = true;
                    break;
                }
            }
        }
        if (!cjk_loaded)
            io.Fonts->AddFontDefault();
    }
#else
    io.Fonts->AddFontDefault();
#endif
    if (io.Fonts->Fonts.empty())
        io.Fonts->AddFontDefault();

    io.FontGlobalScale = g_base_dpi;
    ImGui::GetStyle().ScaleAllSizes(g_base_dpi);

    ImGui_ImplGlfw_InitForOpenGL(g_window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // --- Embedded webview preview (Windows only) ---
#ifdef _WIN32
    HINSTANCE hInstance = GetModuleHandleW(nullptr);
    WNDCLASSEXW ph_wc = {sizeof(ph_wc),         CS_CLASSDC,
                         PreviewHostProc,        0L,
                         0L,                     hInstance,
                         nullptr,                nullptr,
                         nullptr,                nullptr,
                         L"WebPackagerPreview", nullptr};
    RegisterClassExW(&ph_wc);
    HWND g_previewHost = CreateWindowExW(
        WS_EX_CONTROLPARENT, L"WebPackagerPreview", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0,
        glfwGetWin32Window(g_window), nullptr, hInstance, nullptr);

    try {
        g_webview = new webview::webview(true, g_previewHost);
        auto wr = g_webview->widget();
        if (!wr.has_error())
            g_webviewWidget = static_cast<HWND>(wr.value());
        g_webview->set_html(std::string(
            "<html><body style='display:flex;align-items:"
            "center;justify-content:center;font-family:"
            "sans-serif;color:#888;background:#1e1e1e;"
            "margin:0;height:100vh;'><h2>")
            + _("preview_placeholder") + "</h2></body></html>");
    } catch (const std::exception &) {
        g_webview = nullptr;
        g_webviewWidget = nullptr;
    }
#else
    void *g_previewHost = nullptr;
    HWND g_webviewWidget = nullptr;
    (void)g_previewHost;
    (void)g_webviewWidget;
#endif

    WebPackager packager;
#ifdef _WIN32
    packager.m_preview_host_hwnd = g_previewHost;
#endif

    while (!glfwWindowShouldClose(g_window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGuiViewport *vp = ImGui::GetMainViewport();
        ImVec2 vs = vp->Size;

        // Menu bar
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu(_("File"))) {
                if (ImGui::MenuItem(_("Add Files"), "Ctrl+O"))
                    packager.AddFiles();
                if (ImGui::MenuItem(_("Clear All"))) {
                    while (!packager.GetFiles().empty())
                        packager.RemoveFile(0);
                }
                ImGui::Separator();
                if (ImGui::BeginMenu(_("Language"))) {
                    if (ImGui::MenuItem(_("English"), nullptr,
                                        I18n::Instance().GetLang() == Lang::EN))
                        I18n::Instance().SetLang(Lang::EN);
                    if (ImGui::MenuItem(_("Chinese"), nullptr,
                                        I18n::Instance().GetLang() == Lang::ZH))
                        I18n::Instance().SetLang(Lang::ZH);
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem(_("Exit")))
                    glfwSetWindowShouldClose(g_window, GLFW_TRUE);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(_("Build"))) {
                if (ImGui::MenuItem(_("Build EXE"), "F7"))
                    packager.BuildExe();
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        float zoom_dpi = g_base_dpi;
        float mh = ImGui::GetFrameHeight();
        float lw = 220.0f * zoom_dpi;
        float rw = 260.0f * zoom_dpi;
        float lh = 145.0f * zoom_dpi;

        // Left: Files
        ImGui::SetNextWindowPos(ImVec2(0, mh));
        ImGui::SetNextWindowSize(ImVec2(lw, vs.y - mh - lh));
        ImGui::Begin(_("Files"), nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        packager.RenderFilePanel();
        ImGui::End();

        // Right: Build Settings
        ImGui::SetNextWindowPos(ImVec2(vs.x - rw, mh));
        ImGui::SetNextWindowSize(ImVec2(rw, vs.y - mh - lh));
        ImGui::Begin(_("Build Settings"), nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        packager.RenderBuildPanel();
        ImGui::End();

        // Bottom: Log
        ImGui::SetNextWindowPos(ImVec2(0, vs.y - lh));
        ImGui::SetNextWindowSize(ImVec2(vs.x, lh));
        ImGui::Begin(_("Build Log"), nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        packager.RenderLogPanel();
        ImGui::End();

        // Center: Preview
        float px = lw, py = mh, pw = vs.x - lw - rw, ph = vs.y - mh - lh;
        ImGui::SetNextWindowPos(ImVec2(px, py));
        ImGui::SetNextWindowSize(ImVec2(pw, ph));
        ImGui::Begin(_("Preview"), nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_NoScrollWithMouse);

#ifdef _WIN32
        if (g_webview) {
            if (packager.IsProgressVisible()) {
                if (g_previewHost && IsWindowVisible(g_previewHost))
                    ShowWindow(g_previewHost, SW_HIDE);
            } else {
                if (g_previewHost && !IsWindowVisible(g_previewHost))
                    ShowWindow(g_previewHost, SW_SHOW);
            }
            ImVec2 cm = ImGui::GetCursorScreenPos();
            ImVec2 av = ImGui::GetContentRegionAvail();
            ImGui::Dummy(av);
            int ww = (int)av.x, wh = (int)av.y;
            if (ww > 0 && wh > 0 && g_previewHost && IsWindow(g_previewHost)) {
                SetWindowPos(g_previewHost, nullptr, (int)cm.x, (int)cm.y, ww,
                             wh, SWP_NOZORDER | SWP_NOACTIVATE);
                if (g_webviewWidget && IsWindow(g_webviewWidget))
                    SetWindowPos(g_webviewWidget, nullptr, 0, 0, ww, wh,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
            }
            try {
                if (packager.NeedsPreviewUpdate()) {
                    g_webview->set_html(packager.BuildCombinedHtml());
                    packager.ClearPreviewDirty();
                }
            } catch (...) {
            }
        } else {
            ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s",
                               _("WebView2 unavailable"));
            ImGui::TextWrapped("%s", _("webview2_hint"));
        }
#else
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1),
                           "Live preview requires WebView2 (Windows)");
        ImGui::TextWrapped(
            "Preview is available only on Windows with WebView2. "
            "EXE generation targets Windows.");
#endif
        ImGui::End();

        packager.RenderBuildProgress();

        // Render
        ImGui::Render();
        int fb_w, fb_h;
        glfwGetFramebufferSize(g_window, &fb_w, &fb_h);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.10f, 0.10f, 0.12f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(g_window);
    }

    delete g_webview;
    g_webview = nullptr;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(g_window);
    glfwTerminate();
    return 0;
}
