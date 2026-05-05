cmake_minimum_required(VERSION 3.16)
project(web_packager LANGUAGES CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ---- Dependencies ----

# imgui (Win32 + DX11)
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui")
if(NOT EXISTS "${IMGUI_DIR}")
  include(FetchContent)
  FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.8
  )
  FetchContent_MakeAvailable(imgui)
  set(IMGUI_DIR "${imgui_SOURCE_DIR}")
endif()

# webview (Win32 WebView2)
include(FetchContent)
FetchContent_Declare(
  webview
  GIT_REPOSITORY https://github.com/webview/webview.git
  GIT_TAG 0.12.0
)
FetchContent_MakeAvailable(webview)

# ---- Executable (Windows only) ----
add_executable(web_packager WIN32
  src/main.cc
  src/web_packager.cc
  src/app.rc
  "${IMGUI_DIR}/imgui.cpp"
  "${IMGUI_DIR}/imgui_draw.cpp"
  "${IMGUI_DIR}/imgui_tables.cpp"
  "${IMGUI_DIR}/imgui_widgets.cpp"
  "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
  "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp"
)

target_include_directories(web_packager PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${IMGUI_DIR}"
  "${IMGUI_DIR}/backends"
)

target_link_libraries(web_packager PRIVATE
  webview::core
  d3d11
  dxgi
  dwmapi
  d3dcompiler
)

set_target_properties(web_packager PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)
