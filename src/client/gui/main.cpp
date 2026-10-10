// Win32 window + Direct3D 11 swap chain hosting the Dear ImGui UI.

#include "chat_app.hpp"
#include "net.hpp"
#include "theme.hpp"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <d3d11.h>
#include <dwmapi.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
ID3D11RenderTargetView* g_renderTarget = nullptr;
bool g_occluded = false;
UINT g_resizeWidth = 0;
UINT g_resizeHeight = 0;
float g_dpiScale = 1.0f;

void createRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    g_device->CreateRenderTargetView(backBuffer, nullptr, &g_renderTarget);
    backBuffer->Release();
}

void releaseRenderTarget() {
    if (g_renderTarget) {
        g_renderTarget->Release();
        g_renderTarget = nullptr;
    }
}

void releaseDevice() {
    releaseRenderTarget();
    if (g_swapChain) {
        g_swapChain->Release();
        g_swapChain = nullptr;
    }
    if (g_context) {
        g_context->Release();
        g_context = nullptr;
    }
    if (g_device) {
        g_device->Release();
        g_device = nullptr;
    }
}

bool createDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.RefreshRate.Numerator = 60;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hwnd;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL level{};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &desc, &g_swapChain, &g_device, &level,
                                               &g_context);
    if (hr == DXGI_ERROR_UNSUPPORTED)  // no usable GPU (e.g. some VMs): fall back to the software rasterizer
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                           &desc, &g_swapChain, &g_device, &level, &g_context);
    if (FAILED(hr))
        return false;
    createRenderTarget();
    return true;
}

LRESULT WINAPI windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return TRUE;

    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            g_resizeWidth = LOWORD(lParam);
            g_resizeHeight = HIWORD(lParam);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = static_cast<LONG>(880 * g_dpiScale);
        info->ptMinTrackSize.y = static_cast<LONG>(620 * g_dpiScale);
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU)  // disable the ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    net::WinsockSession winsock;
    if (!winsock.ok()) {
        MessageBoxW(nullptr, L"Could not initialise Winsock.", L"elazharyLearn", MB_ICONERROR);
        return 1;
    }

    ImGui_ImplWin32_EnableDpiAwareness();
    g_dpiScale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"ElazharyLearnWindow";
    RegisterClassExW(&wc);

    const int width = static_cast<int>(1180 * g_dpiScale);
    const int height = static_cast<int>(760 * g_dpiScale);
    const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"elazharyLearn", WS_OVERLAPPEDWINDOW, x, y, width, height, nullptr,
                              nullptr, instance, nullptr);

    // Dark title bar to match the theme (Windows 10 20H1+ / Windows 11).
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark);

    if (!createDevice(hwnd)) {
        releaseDevice();
        MessageBoxW(hwnd, L"Could not create a Direct3D 11 device.", L"elazharyLearn", MB_ICONERROR);
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, instance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;  // fixed layout, nothing worth persisting
    ui::setup(g_dpiScale);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    {
        ChatApp app(hwnd);
        const ImVec4 clear = ImGui::ColorConvertU32ToFloat4(ui::col::appBg);
        const float clearColor[4] = {clear.x, clear.y, clear.z, clear.w};

        bool done = false;
        while (!done) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
                if (msg.message == WM_QUIT)
                    done = true;
            }
            if (done)
                break;

            // Window minimised or fully covered: don't burn GPU time.
            if (g_occluded && g_swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
                Sleep(15);
                continue;
            }
            g_occluded = false;

            if (g_resizeWidth != 0 && g_resizeHeight != 0) {
                releaseRenderTarget();
                g_swapChain->ResizeBuffers(0, g_resizeWidth, g_resizeHeight, DXGI_FORMAT_UNKNOWN, 0);
                g_resizeWidth = g_resizeHeight = 0;
                createRenderTarget();
            }

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            app.frame();
            ImGui::Render();

            g_context->OMSetRenderTargets(1, &g_renderTarget, nullptr);
            g_context->ClearRenderTargetView(g_renderTarget, clearColor);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

            const HRESULT hr = g_swapChain->Present(1, 0);  // vsync
            g_occluded = hr == DXGI_STATUS_OCCLUDED;
        }
    }  // ChatApp (and its network threads) shut down before the UI does

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    releaseDevice();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, instance);
    return 0;
}
