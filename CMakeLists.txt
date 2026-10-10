cmake_minimum_required(VERSION 3.24)
project(ElazharyLearnChat VERSION 1.0.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "elazharyLearn targets Windows (Winsock2 + Direct3D 11).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Settings shared by every target in this project.
add_library(project_options INTERFACE)
target_compile_definitions(project_options INTERFACE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00)
if(MSVC)
    target_compile_options(project_options INTERFACE /W4 /permissive- /utf-8)
else()
    target_compile_options(project_options INTERFACE -Wall -Wextra -Wpedantic)
    # Self-contained executables: no MinGW runtime DLLs needed next to the .exe.
    target_link_options(project_options INTERFACE -static)
endif()

# --- Shared networking + protocol ------------------------------------------
add_library(chat_common STATIC
    src/common/net.cpp
    src/common/protocol.cpp)
target_include_directories(chat_common PUBLIC src/common)
target_link_libraries(chat_common PUBLIC project_options ws2_32)

# --- Server ------------------------------------------------------------------
add_library(chat_server_core STATIC
    src/server/chat_server.cpp
    src/server/log.cpp)
target_include_directories(chat_server_core PUBLIC src/server)
target_link_libraries(chat_server_core PUBLIC chat_common)

add_executable(chat_server src/server/main.cpp)
target_link_libraries(chat_server PRIVATE chat_server_core)

# --- Client networking (UI-agnostic) ----------------------------------------
add_library(chat_client_core STATIC src/client/chat_client.cpp)
target_include_directories(chat_client_core PUBLIC src/client)
target_link_libraries(chat_client_core PUBLIC chat_common)

add_executable(chat_cli src/cli/main.cpp)
target_link_libraries(chat_cli PRIVATE chat_client_core)

# --- Dear ImGui v1.91.5 (vendored, MIT licence) -----------------------------
set(imgui_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS UNICODE _UNICODE)
target_link_libraries(imgui PUBLIC d3d11 dxgi d3dcompiler dwmapi gdi32)

# --- GUI client --------------------------------------------------------------
add_executable(chat_client WIN32
    src/client/gui/main.cpp
    src/client/gui/chat_app.cpp
    src/client/gui/settings.cpp
    src/client/gui/theme.cpp)
target_link_libraries(chat_client PRIVATE chat_client_core imgui)

# --- Tests -------------------------------------------------------------------
enable_testing()
add_executable(chat_tests tests/integration_tests.cpp)
target_link_libraries(chat_tests PRIVATE chat_server_core chat_client_core)
add_test(NAME integration COMMAND chat_tests)
