#include "app/AppPaths.h"
#include "app/AppSettings.h"
#include "app/FileLog.h"
#include "app/FirstRun.h"
#include "app/SingleInstance.h"
#include "app/main_cli.h"
#include "bindings/HotkeyParser.h"
#include "bindings/HotkeyRegistry.h"
#include "core/Cancel.h"
#include "editor/AppIcon.h"
#include "editor/TrayIcon.h"
#include "editor/UiTheme.h"
#include "editor/UndekApp.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h> // first: the rest of these need its types
#include <windowsx.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <tchar.h>
#include <cstdio>
#include <vector>

#pragma comment(lib, "d3d11.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

namespace {

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static HWND g_hwnd = nullptr;

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL featureLevel;
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                      D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice,
                                      &featureLevel, &g_pd3dDeviceContext) != S_OK) {
        return false;
    }
    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) {
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }
    if (g_pd3dDeviceContext) {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }
    if (g_pd3dDevice) {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
}

constexpr int kShowHotkeyId = 9000;
constexpr int kStealthHotkeyId = 9001;
constexpr int kPickerHotkeyId = 9002;
constexpr int kPauseHotkeyId = 9003;

undek::editor::TrayIcon g_tray;
bool g_stealth_hidden = false;
bool g_was_window_visible = true;
bool g_was_tray_active = true;
bool g_show_hk_registered = false;
bool g_stealth_hk_registered = false;
bool g_picker_hk_registered = false;
bool g_pause_hk_registered = false;
bool g_show_was_down = false;
bool g_stealth_was_down = false;
bool g_picker_was_down = false;
bool g_pause_was_down = false;

bool windowShown() {
    return IsWindowVisible(g_hwnd) && !IsIconic(g_hwnd);
}

// Rounded corners are asked of the compositor rather than cut with a window
// region: a region would leave the corners jagged and would have to be rebuilt
// on every resize, while this leaves the window rectangle, the resize edges and
// the shadow exactly as they are. Windows 10 has no such attribute and ignores
// the call, which leaves the corners square.
void applyWindowCorners(HWND hwnd) {
    // DWMWA_WINDOW_CORNER_PREFERENCE; spelled out because the enum isn't in
    // every SDK. 1 is don't round, 2 is round.
    constexpr DWORD kCornerPreference = 33;
    const int preference = IsZoomed(hwnd) ? 1 : 2;
    DwmSetWindowAttribute(hwnd, kCornerPreference, &preference, sizeof(preference));
}

void showMainWindow() {
    ShowWindow(g_hwnd, SW_SHOW);
    ShowWindow(g_hwnd, SW_RESTORE);
    SetForegroundWindow(g_hwnd);
}

void toggleMainWindow() {
    if (g_stealth_hidden) {
        g_stealth_hidden = false;
        g_tray.create(g_hwnd, L"Undek Macro");
        showMainWindow();
        return;
    }
    if (windowShown())
        ShowWindow(g_hwnd, SW_HIDE);
    else
        showMainWindow();
}

void toggleStealthMode() {
    if (!g_stealth_hidden) {
        g_was_window_visible = windowShown();
        g_was_tray_active = g_tray.active();
        ShowWindow(g_hwnd, SW_HIDE);
        g_tray.destroy();
        g_stealth_hidden = true;
    } else {
        g_stealth_hidden = false;
        if (g_was_tray_active) {
            g_tray.create(g_hwnd, L"Undek Macro");
        }
        if (g_was_window_visible) {
            showMainWindow();
        } else {
            ShowWindow(g_hwnd, SW_HIDE);
        }
    }
}

void registerAppHotkeys() {
    if (!g_hwnd) return;
    UnregisterHotKey(g_hwnd, kShowHotkeyId);
    UnregisterHotKey(g_hwnd, kStealthHotkeyId);
    UnregisterHotKey(g_hwnd, kPickerHotkeyId);
    UnregisterHotKey(g_hwnd, kPauseHotkeyId);
    g_show_hk_registered = false;
    g_stealth_hk_registered = false;
    g_picker_hk_registered = false;
    g_pause_hk_registered = false;

    const auto& s = undek::AppSettings::get();
    const auto hk_show = undek::bindings::parseHotkey(s.show_hotkey);
    if (hk_show.valid && !undek::bindings::isMouseKey(hk_show.virtual_key)) {
        g_show_hk_registered =
            RegisterHotKey(g_hwnd, kShowHotkeyId, hk_show.modifiers | MOD_NOREPEAT,
                           hk_show.virtual_key) == TRUE;
    }

    const auto hk_stealth = undek::bindings::parseHotkey(s.stealth_hotkey);
    if (hk_stealth.valid && !undek::bindings::isMouseKey(hk_stealth.virtual_key)) {
        g_stealth_hk_registered =
            RegisterHotKey(g_hwnd, kStealthHotkeyId, hk_stealth.modifiers | MOD_NOREPEAT,
                           hk_stealth.virtual_key) == TRUE;
    }

    const auto hk_picker = undek::bindings::parseHotkey(s.picker_hotkey);
    if (hk_picker.valid && !undek::bindings::isMouseKey(hk_picker.virtual_key)) {
        g_picker_hk_registered =
            RegisterHotKey(g_hwnd, kPickerHotkeyId, hk_picker.modifiers | MOD_NOREPEAT,
                           hk_picker.virtual_key) == TRUE;
    }

    const auto hk_pause = undek::bindings::parseHotkey(s.pause_hotkey);
    if (hk_pause.valid && !undek::bindings::isMouseKey(hk_pause.virtual_key)) {
        g_pause_hk_registered =
            RegisterHotKey(g_hwnd, kPauseHotkeyId, hk_pause.modifiers | MOD_NOREPEAT,
                           hk_pause.virtual_key) == TRUE;
    }
}

bool isParsedHotkeyDown(const undek::bindings::ParsedHotkey& hk) {
    if (!hk.valid) return false;
    if ((GetAsyncKeyState(static_cast<int>(hk.virtual_key)) & 0x8000) == 0) return false;
    const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    if (((hk.modifiers & MOD_CONTROL) != 0) != ctrl) return false;
    if (((hk.modifiers & MOD_SHIFT) != 0) != shift) return false;
    if (((hk.modifiers & MOD_ALT) != 0) != alt) return false;
    return true;
}

void pollFallbackAppHotkeys(undek::editor::UndekApp& app) {
    const auto& s = undek::AppSettings::get();

    if (!g_show_hk_registered) {
        const auto hk = undek::bindings::parseHotkey(s.show_hotkey);
        const bool down = isParsedHotkeyDown(hk);
        if (down && !g_show_was_down) {
            g_show_was_down = true;
            toggleMainWindow();
        } else if (!down) {
            g_show_was_down = false;
        }
    }

    if (!g_stealth_hk_registered) {
        const auto hk = undek::bindings::parseHotkey(s.stealth_hotkey);
        const bool down = isParsedHotkeyDown(hk);
        if (down && !g_stealth_was_down) {
            g_stealth_was_down = true;
            toggleStealthMode();
        } else if (!down) {
            g_stealth_was_down = false;
        }
    }

    if (!g_picker_hk_registered) {
        const auto hk = undek::bindings::parseHotkey(s.picker_hotkey);
        const bool down = isParsedHotkeyDown(hk);
        if (down && !g_picker_was_down) {
            g_picker_was_down = true;
            app.captureCursorAndPixel();
        } else if (!down) {
            g_picker_was_down = false;
        }
    }

    if (!g_pause_hk_registered) {
        const auto hk = undek::bindings::parseHotkey(s.pause_hotkey);
        const bool down = isParsedHotkeyDown(hk);
        if (down && !g_pause_was_down) {
            g_pause_was_down = true;
            app.togglePause();
        } else if (!down) {
            g_pause_was_down = false;
        }
    }
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
        case WM_SYSCOMMAND:
            // The caption button asks to minimise, and the setting decides what
            // that means: hide to the tray, or leave the window minimised with a
            // taskbar button. A window without a caption is not minimised by
            // DefWindowProc, so the request is carried out here either way.
            if ((wParam & 0xFFF0) == SC_MINIMIZE) {
                if (undek::AppSettings::get().minimize_to_tray)
                    ShowWindow(hWnd, SW_HIDE);
                else
                    ShowWindow(hWnd, SW_MINIMIZE);
                return 0;
            }
            break;
        case WM_CLOSE:
            if (undek::AppSettings::get().close_to_tray) {
                ShowWindow(hWnd, SW_HIDE);
                return 0;
            }
            DestroyWindow(hWnd);
            return 0;
        case undek::editor::kTrayMessage:
            if (LOWORD(lParam) == WM_LBUTTONUP) {
                toggleMainWindow();
            } else if (LOWORD(lParam) == WM_RBUTTONUP) {
                switch (g_tray.showMenu(hWnd, windowShown())) {
                    case undek::editor::kTrayShowHide:
                        toggleMainWindow();
                        break;
                    case undek::editor::kTrayStopMacro:
                        undek::cancel::request();
                        break;
                    case undek::editor::kTrayQuit:
                        DestroyWindow(hWnd);
                        break;
                    default:
                        break;
                }
            }
            return 0;
        case WM_NCCALCSIZE:
            // A thick frame reserves a border that the system paints, and it
            // shows as a light band around the app once maximized. The whole
            // window is client area instead, with the resize edges coming from
            // WM_NCHITTEST below.
            if (wParam == TRUE) return 0;
            break;
        case WM_NCHITTEST: {
            if (IsZoomed(hWnd)) return HTCLIENT;
            POINT cursor{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RECT rect;
            GetWindowRect(hWnd, &rect);
            constexpr int kEdge = 6;
            const bool left = cursor.x < rect.left + kEdge;
            const bool right = cursor.x >= rect.right - kEdge;
            const bool top = cursor.y < rect.top + kEdge;
            const bool bottom = cursor.y >= rect.bottom - kEdge;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
            return HTCLIENT;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            // A captionless window has no system minimum either, and below this
            // the sidebar, editor and status bar stop fitting.
            info->ptMinTrackSize.x = 900;
            info->ptMinTrackSize.y = 600;
            // A popup also maximizes over the whole monitor, taskbar included,
            // so the work area is named explicitly.
            MONITORINFO monitor{sizeof(monitor)};
            if (GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
                const RECT& work = monitor.rcWork;
                const RECT& full = monitor.rcMonitor;
                info->ptMaxPosition.x = work.left - full.left;
                info->ptMaxPosition.y = work.top - full.top;
                info->ptMaxSize.x = work.right - work.left;
                info->ptMaxSize.y = work.bottom - work.top;
            }
            return 0;
        }
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) applyWindowCorners(hWnd);
            if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
                CleanupRenderTarget();
                g_pSwapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN,
                                            0);
                CreateRenderTarget();
            }
            return 0;
        case WM_DESTROY:
            UnregisterHotKey(hWnd, kShowHotkeyId);
            UnregisterHotKey(hWnd, kStealthHotkeyId);
            UnregisterHotKey(hWnd, kPickerHotkeyId);
            UnregisterHotKey(hWnd, kPauseHotkeyId);
            g_tray.destroy();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

bool hasCliArgs() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const bool cli = (argc > 1);
    LocalFree(argv);
    return cli;
}

int cliMain() {
    int argc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    std::vector<char*> ptrs;
    for (int i = 0; i < argc; ++i) {
        const int len = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, s.data(), len, nullptr, nullptr);
        args.push_back(s);
    }
    LocalFree(wargv);
    for (auto& a : args)
        ptrs.push_back(a.data());
    return runCli(static_cast<int>(ptrs.size()), ptrs.data());
}

} // namespace

namespace undek::editor {
void refreshGlobalAppHotkeys() {
    registerAppHotkeys();
}
} // namespace undek::editor

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    ImGui_ImplWin32_EnableDpiAwareness();

    if (hasCliArgs()) {
        // A redirected stream belongs to whoever started us: `undek --version >
        // v.txt` should fill that file, not a console opened for our own use,
        // and `> NUL` should keep it quiet. stdout and stderr are asked
        // separately, because `2> errors.txt` can redirect one without the
        // other and repointing both would throw the file away. GetConsoleMode
        // is what tells a real console apart from the other character devices
        // (NUL, serial ports) that GetFileType reports the same way.
        enum class Stream { Missing, Console, Redirected };
        const auto streamState = [](DWORD which) {
            const HANDLE handle = GetStdHandle(which);
            if (!handle || handle == INVALID_HANDLE_VALUE) return Stream::Missing;
            DWORD mode = 0;
            if (GetConsoleMode(handle, &mode)) return Stream::Console;
            return Stream::Redirected;
        };
        const Stream out_state = streamState(STD_OUTPUT_HANDLE);
        const Stream err_state = streamState(STD_ERROR_HANDLE);
        if (out_state == Stream::Missing || err_state == Stream::Missing) {
            AllocConsole();
            FILE* f = nullptr;
            if (out_state == Stream::Missing) freopen_s(&f, "CONOUT$", "w", stdout);
            if (err_state == Stream::Missing) freopen_s(&f, "CONOUT$", "w", stderr);
        }
        return cliMain();
    }

    std::string instance_error;
    if (!undek::claimEditorInstance(instance_error)) {
        MessageBoxA(nullptr, instance_error.c_str(), "Undek Macro", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    undek::AppPaths::initialize();
    const undek::FirstRunReport first_run =
        undek::prepareFirstRun(undek::AppPaths::root(), UNDEK_VERSION);
    undek::FileLog::init(undek::AppPaths::resolve("logs"));
    undek::AppSettings::get().load(undek::AppPaths::resolve("settings.json"));

    const HICON app_icon = undek::editor::loadAppIcon();
    WNDCLASSEX wc{sizeof(WNDCLASSEX),       CS_CLASSDC, WndProc, 0L,      0L,
                  GetModuleHandle(nullptr), app_icon,   nullptr, nullptr, nullptr,
                  _T("UndekMacro"),         app_icon};
    RegisterClassEx(&wc);
    // Frameless: the caption is drawn in ImGui, in the menu bar. A popup with a
    // thick frame instead of a caption keeps the resize border, the snap
    // behaviour and the drop shadow, and WS_EX_APPWINDOW keeps it on the
    // taskbar, which an unowned popup would otherwise not promise.
    g_hwnd = CreateWindowEx(WS_EX_APPWINDOW, wc.lpszClassName,
                            _T("Undek Macro v" UNDEK_VERSION),
                            WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX, 80,
                            80, 1280, 720, nullptr, nullptr, wc.hInstance, nullptr);
    if (g_hwnd) {
        applyWindowCorners(g_hwnd);
        SendMessageW(g_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(app_icon));
        SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(app_icon));
    }

    if (!CreateDeviceD3D(g_hwnd)) {
        CleanupDeviceD3D();
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    g_tray.create(g_hwnd, L"Undek Macro");
    registerAppHotkeys();

    const auto& settings = undek::AppSettings::get();
    if (!settings.start_minimized)
        ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    else if (settings.minimize_to_tray)
        ShowWindow(g_hwnd, SW_HIDE);
    else
        ShowWindow(g_hwnd, SW_SHOWMINNOACTIVE);
    UpdateWindow(g_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    const float dpi_scale = ImGui_ImplWin32_GetDpiScaleForHwnd(g_hwnd);
    undek::editor::ApplyModernTheme(dpi_scale);
    undek::editor::LoadUiFonts(dpi_scale);

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    undek::bindings::HotkeyRegistry::setWindow(g_hwnd);

    undek::editor::UndekApp app;
    app.setHostWindow(g_hwnd);
    app.showWelcome(first_run, UNDEK_VERSION);

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            if (msg.message == WM_HOTKEY) {
                const int id = static_cast<int>(msg.wParam);
                if (id == kShowHotkeyId) {
                    toggleMainWindow();
                    continue;
                }
                if (id == kStealthHotkeyId) {
                    toggleStealthMode();
                    continue;
                }
                if (id == kPickerHotkeyId) {
                    app.captureCursorAndPixel();
                    continue;
                }
                if (id == kPauseHotkeyId) {
                    app.togglePause();
                    continue;
                }
                app.onHotkeyMessage(id);
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        app.pollHotkeys();
        pollFallbackAppHotkeys(app);

        if (!windowShown()) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 12, QS_ALLINPUT);
            continue;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        app.renderFrame();

        ImGui::Render();
        const float clear[4] = {0.09f, 0.09f, 0.11f, 1.0f};
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    app.shutdown();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(g_hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);
    return 0;
}
