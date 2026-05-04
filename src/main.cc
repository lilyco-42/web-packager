// WebPackager - ImGui + WebView2 web-to-desktop packager

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <tchar.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "webview/webview.h"
#include "web_packager.hh"
#include "i18n.hh"

#include <locale>
#include <stdexcept>

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK PreviewHostProc(HWND hwnd, UINT msg, WPARAM wParam,
                                 LPARAM lParam);

static ID3D11Device *g_pd3dDevice = nullptr;
static ID3D11DeviceContext *g_pd3dDeviceContext = nullptr;
static IDXGISwapChain *g_pSwapChain = nullptr;
static bool g_SwapChainOccluded = false;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView *g_mainRenderTargetView = nullptr;

static HWND g_previewHost = nullptr;
static HWND g_webviewWidget = nullptr;
static webview::webview *g_webview = nullptr;
static WebPackager *g_packager = nullptr;

// DPI / zoom
static float g_zoom = 1.0f;
static float g_base_dpi = 1.0f;

extern IMGUI_IMPL_API LRESULT
ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                               LPARAM lParam);

int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // Make process DPI-aware → crisp rendering on high-DPI screens
    ImGui_ImplWin32_EnableDpiAwareness();

    // Set locale to system ANSI codepage for Chinese filenames in paths
    try {
        std::locale::global(std::locale(""));
    } catch (const std::runtime_error &) {
        // System locale unavailable — Chinese filenames may not work
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool com_initialized = SUCCEEDED(hr);

    WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC,  WndProc,   0L, 0L,
                       hInstance,  nullptr,     nullptr,   nullptr,
                       nullptr,    L"WebPackager", nullptr};
    RegisterClassExW(&wc);

    WNDCLASSEXW ph_wc = {sizeof(ph_wc),     CS_CLASSDC,
                         PreviewHostProc,   0L, 0L,
                         hInstance,         nullptr, nullptr,
                         nullptr,           nullptr,
                         L"WebPackagerPreview", nullptr};
    RegisterClassExW(&ph_wc);

    HWND hwnd = CreateWindowW(L"WebPackager",
                              L"WebPackager - Web to EXE Packager",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                              CW_USEDEFAULT, 1400, 900, nullptr, nullptr,
                              wc.hInstance, nullptr);
    if (!hwnd)
        return 1;
    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    g_base_dpi = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);

    // Load CJK font at a fixed pixel size, then use io.FontGlobalScale for zoom
    ImFontConfig font_cfg;
    font_cfg.FontNo = 0;
    float base_font_size = 16.0f;
    const ImWchar *ranges =
        io.Fonts->GetGlyphRangesChineseSimplifiedCommon();
    bool cjk_ok = false;
    const char *cjk_paths[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",  "C:\\Windows\\Fonts\\simhei.ttf",
        "C:\\Windows\\Fonts\\SIMHEI.TTF", "C:\\Windows\\Fonts\\msyhbd.ttc",
    };
    for (auto *p : cjk_paths) {
        if (GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES) {
            if (io.Fonts->AddFontFromFileTTF(p, base_font_size, &font_cfg, ranges)) {
                cjk_ok = true;
                break;
            }
        }
    }
    if (!cjk_ok)
        io.Fonts->AddFontDefault();
    // Guarantee at least one font is loaded (prevents blank screen / crash)
    if (io.Fonts->Fonts.empty())
        io.Fonts->AddFontDefault();

    g_zoom = 1.0f;
    io.FontGlobalScale = g_base_dpi; // crisp at native DPI
    ImGui::GetStyle().ScaleAllSizes(g_base_dpi);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    g_previewHost = CreateWindowExW(
        WS_EX_CONTROLPARENT, L"WebPackagerPreview", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0, hwnd, nullptr,
        hInstance, nullptr);

    try {
        g_webview = new webview::webview(true, g_previewHost);
        auto wr = g_webview->widget();
        if (!wr.has_error())
            g_webviewWidget = static_cast<HWND>(wr.value());
        g_webview->set_html(
            std::string("<html><body style='display:flex;align-items:"
                        "center;justify-content:center;font-family:"
                        "sans-serif;color:#888;background:#1e1e1e;"
                        "margin:0;height:100vh;'><h2>")
            + _("preview_placeholder") + "</h2></body></html>");
    } catch (const std::exception &) {
        g_webview = nullptr;
        g_webviewWidget = nullptr;
    }

    WebPackager packager;
    g_packager = &packager;
    packager.m_preview_host_hwnd = g_previewHost;

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight,
                                        DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
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
                    PostQuitMessage(0);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(_("Build"))) {
                if (ImGui::MenuItem(_("Build EXE"), "F7"))
                    packager.BuildExe();
                ImGui::EndMenu();
            }
            // Zoom slider in menu bar
            ImGui::PushItemWidth(120);
            ImGui::SameLine(ImGui::GetWindowWidth() - 140);
            ImGui::Text("Zoom:"); ImGui::SameLine();
            if (ImGui::SliderFloat("##zoom", &g_zoom, 0.5f, 2.5f, "%.1fx")) {
                if (g_zoom < 0.5f) g_zoom = 0.5f;
                if (g_zoom > 2.5f) g_zoom = 2.5f;
                io.FontGlobalScale = g_base_dpi * g_zoom;
            }
            ImGui::PopItemWidth();
            ImGui::EndMainMenuBar();
        }

        float zoom_dpi = g_base_dpi * g_zoom;
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

        if (g_webview) {
            // Hide preview while progress popup is up (building + after-done until user closes)
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
            ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1),
                               "%s", _("WebView2 unavailable"));
            ImGui::TextWrapped("%s", _("webview2_hint"));
        }
        ImGui::End();

        packager.RenderBuildProgress();

        ImGui::Render();
        float clear_color[4] = {0.10f, 0.10f, 0.12f, 1.00f};
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView,
                                                 nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView,
                                                   clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
        g_SwapChainOccluded = false;
    }

    delete g_webview;
    g_webview = nullptr;
    g_webviewWidget = nullptr;
    g_previewHost = nullptr;

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    if (com_initialized)
        CoUninitialize();
    return 0;
}

// ---- D3D11 ----
bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = 0;
    D3D_FEATURE_LEVEL level;
    D3D_FEATURE_LEVEL levels[2] = {D3D_FEATURE_LEVEL_11_0,
                                   D3D_FEATURE_LEVEL_10_0};
    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2,
        D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &level,
        &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 2,
            D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &level,
            &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;
    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D *buf = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&buf));
    g_pd3dDevice->CreateRenderTargetView(buf, nullptr, &g_mainRenderTargetView);
    buf->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

// ---- Windows ----
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK PreviewHostProc(HWND hwnd, UINT msg, WPARAM wParam,
                                 LPARAM lParam) {
    if (msg == WM_SIZE) {
        HWND child = GetWindow(hwnd, GW_CHILD);
        if (child) {
            SetWindowPos(child, nullptr, 0, 0, LOWORD(lParam), HIWORD(lParam),
                         SWP_NOZORDER);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
