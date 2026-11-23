#include "input/InputRecorder.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "core/ScanKey.h"
#include "input/RecorderTape.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

#include <vector>

namespace undek {
namespace {

std::atomic<bool> g_recording{false};
std::atomic<bool> g_paused{false};
std::mutex g_mutex;
RecorderTape g_tape;

long long nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

#ifdef _WIN32
std::thread g_thread;
std::atomic<bool> g_ready{false};
DWORD g_hook_tid = 0;
std::atomic<HHOOK> g_kb_hook{nullptr};
std::atomic<HHOOK> g_mouse_hook{nullptr};
void* g_host_hwnd = nullptr;

std::string keyNameForVk(DWORD vk) {
    if (vk >= 'A' && vk <= 'Z') {
        char c = static_cast<char>(vk + ('a' - 'A'));
        return std::string(1, c);
    }
    if (vk >= '0' && vk <= '9') {
        return std::string(1, static_cast<char>(vk));
    }
    if (vk >= VK_F1 && vk <= VK_F12) {
        return "F" + std::to_string(vk - VK_F1 + 1);
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        return "numpad" + std::to_string(vk - VK_NUMPAD0);
    }
    switch (vk) {
        case VK_BACK:
            return "backspace";
        case VK_DELETE:
            return "delete";
        case VK_INSERT:
            return "insert";
        case VK_HOME:
            return "home";
        case VK_END:
            return "end";
        case VK_PRIOR:
            return "pageup";
        case VK_NEXT:
            return "pagedown";
        case VK_LEFT:
            return "left";
        case VK_RIGHT:
            return "right";
        case VK_UP:
            return "up";
        case VK_DOWN:
            return "down";
        case VK_CAPITAL:
            return "capslock";
        case VK_SPACE:
            return "space";
        case VK_RETURN:
            return "enter";
        case VK_TAB:
            return "tab";
        case VK_ESCAPE:
            return "esc";
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
            return "shift";
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
            return "ctrl";
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
            return "alt";
        case VK_OEM_1:
            return ";";
        case VK_OEM_PLUS:
            return "=";
        case VK_OEM_COMMA:
            return ",";
        case VK_OEM_MINUS:
            return "-";
        case VK_OEM_PERIOD:
            return ".";
        case VK_OEM_2:
            return "/";
        case VK_OEM_3:
            return "`";
        case VK_OEM_4:
            return "[";
        case VK_OEM_5:
            return "\\";
        case VK_OEM_6:
            return "]";
        case VK_OEM_7:
            return "'";
        default:
            return {};
    }
}

bool clickOnHostWindow(POINT pt) {
    if (!g_host_hwnd) return false;
    HWND hit = WindowFromPoint(pt);
    if (!hit) return false;
    return GetAncestor(hit, GA_ROOT) == GetAncestor(static_cast<HWND>(g_host_hwnd), GA_ROOT);
}

LRESULT CALLBACK lowLevelKbProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // The hook keeps running while paused so the held state stays true; it just
    // doesn't write anything into the take.
    if (nCode == HC_ACTION && g_recording.load()) {
        const auto* kb = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        if (kb->flags & LLKHF_INJECTED) return CallNextHookEx(nullptr, nCode, wParam, lParam);
        std::string key = keyNameForVk(kb->vkCode);
        if (key.empty()) {
            // No friendly name for this one, so keep the raw scancode rather
            // than dropping the press from the take.
            key = undek::formatScanKey(static_cast<unsigned short>(kb->scanCode),
                                       (kb->flags & LLKHF_EXTENDED) != 0);
        }
        const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
        const bool up = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;
        if (!key.empty() && (down || up)) {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_tape.setPaused(g_paused.load());
            g_tape.key(kb->vkCode, key, down, nowMs());
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK lowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_recording.load()) {
        const auto* ms = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        if (ms->flags & LLMHF_INJECTED) return CallNextHookEx(nullptr, nCode, wParam, lParam);
        if (!clickOnHostWindow(ms->pt)) {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_tape.setPaused(g_paused.load());
            const long long now = nowMs();

            if (wParam == WM_MOUSEMOVE) {
                g_tape.move(ms->pt.x, ms->pt.y, now);
            } else if (wParam == WM_MOUSEWHEEL) {
                g_tape.wheel(static_cast<short>(HIWORD(ms->mouseData)), now);
            } else if (wParam == WM_LBUTTONDOWN || wParam == WM_LBUTTONUP) {
                g_tape.button("left", wParam == WM_LBUTTONDOWN, now);
            } else if (wParam == WM_RBUTTONDOWN || wParam == WM_RBUTTONUP) {
                g_tape.button("right", wParam == WM_RBUTTONDOWN, now);
            } else if (wParam == WM_MBUTTONDOWN || wParam == WM_MBUTTONUP) {
                g_tape.button("middle", wParam == WM_MBUTTONDOWN, now);
            } else if (wParam == WM_XBUTTONDOWN || wParam == WM_XBUTTONUP) {
                const std::string name = (HIWORD(ms->mouseData) == XBUTTON2) ? "x2" : "x1";
                g_tape.button(name, wParam == WM_XBUTTONDOWN, now);
            }
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

#endif

} // namespace

void InputRecorder::setHostWindow(void* hwnd) {
#ifdef _WIN32
    g_host_hwnd = hwnd;
#else
    (void)hwnd;
#endif
}

bool InputRecorder::start(std::string& error_out) {
    bool expected = false;
    if (!g_recording.compare_exchange_strong(expected, true)) {
        // Already recording: nothing to report, the caller's take carries on.
        error_out.clear();
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_tape.reset(nowMs());
        g_paused = false;
    }

#ifdef _WIN32
    g_ready = false;
    if (g_thread.joinable()) g_thread.join();
    g_thread = std::thread([]() {
        MSG msg{};
        PeekMessage(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        g_hook_tid = GetCurrentThreadId();
        HINSTANCE hMod = GetModuleHandleA(nullptr);
        g_kb_hook = SetWindowsHookExA(WH_KEYBOARD_LL, lowLevelKbProc, hMod, 0);
        g_mouse_hook = SetWindowsHookExA(WH_MOUSE_LL, lowLevelMouseProc, hMod, 0);
        g_ready = true;

        while (g_recording.load() && GetMessage(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (g_kb_hook.load()) {
            UnhookWindowsHookEx(g_kb_hook.load());
            g_kb_hook = nullptr;
        }
        if (g_mouse_hook.load()) {
            UnhookWindowsHookEx(g_mouse_hook.load());
            g_mouse_hook = nullptr;
        }
        g_hook_tid = 0;
    });

    // Wait for the thread to try, so a refusal is reported rather than a take
    // that quietly records only half of what the user does.
    for (int i = 0; i < 500 && !g_ready.load(); ++i)
        Sleep(1);
    if (g_kb_hook.load() == nullptr || g_mouse_hook.load() == nullptr) {
        error_out = g_kb_hook.load() == nullptr ? "could not install the keyboard hook"
                                                : "could not install the mouse hook";
        stop();
        return false;
    }
    error_out.clear();
    return true;
#else
    error_out = "recording is only available on Windows";
    g_recording = false;
    return false;
#endif
}

void InputRecorder::stop() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        // Close out anything still pressed, so the take can't end with a key or
        // button held down.
        if (g_recording.exchange(false)) g_tape.closeOut(false, nowMs());
    }
    g_paused = false;
#ifdef _WIN32
    for (int i = 0; i < 1000 && !g_ready.load(); ++i)
        Sleep(1);
    if (g_hook_tid != 0) {
        PostThreadMessageA(g_hook_tid, WM_QUIT, 0, 0);
    }
    if (g_thread.joinable()) g_thread.join();
#endif
}

void InputRecorder::pause() {
    g_paused = true;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_tape.setPaused(true);
}

void InputRecorder::resume() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        // Anything released while paused never got its KeyUp/MouseUp written,
        // so write those before carrying on. Keys still held stay held.
        g_tape.closeOut(true, nowMs());
        g_tape.setPaused(false);
    }
    g_paused = false;
}

bool InputRecorder::isRecording() {
    return g_recording.load();
}

bool InputRecorder::isPaused() {
    return g_paused.load();
}

std::vector<std::string> InputRecorder::takeActions() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_tape.actions();
}

size_t InputRecorder::actionCount() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_tape.count();
}

} // namespace undek
