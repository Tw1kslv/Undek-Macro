#include "bindings/HotkeyRegistry.h"

#include "bindings/HotkeyParser.h"

#include <atomic>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace undek::bindings {
namespace {

constexpr int kStopHotkeyId = 0;
constexpr int kAppHotkeyIdBase = 9000;

std::vector<RegisteredHotkey> g_entries;
std::atomic<bool> g_suspended{false};
bool g_stop_registered = false;
HWND g_hwnd = nullptr;

HWND targetWindow() {
    return g_hwnd ? g_hwnd : nullptr;
}

bool registerOne(int id, unsigned int mod, unsigned int vk, std::string& error_out) {
    if (isMouseKey(vk)) {
        return true;
    }
    HWND hwnd = targetWindow();
    if (!hwnd) {
        error_out = "No target window for hotkey registration";
        return false;
    }
    if (!RegisterHotKey(hwnd, id, mod, vk)) {
        DWORD err = GetLastError();
        error_out = "RegisterHotKey failed (id=" + std::to_string(id) +
                    ", vk=" + std::to_string(vk) + ", mod=" + std::to_string(mod) +
                    ", err=" + std::to_string(err) + ")";
        return false;
    }
    return true;
}

} // namespace

void HotkeyRegistry::setWindow(void* hwnd) {
    g_hwnd = static_cast<HWND>(hwnd);
}

void* HotkeyRegistry::window() {
    return g_hwnd;
}

void HotkeyRegistry::clear() {
    unregisterBindings();
}

void HotkeyRegistry::add(RegisteredHotkey entry) {
    g_entries.push_back(std::move(entry));
}

const std::vector<RegisteredHotkey>& HotkeyRegistry::entries() {
    return g_entries;
}

bool HotkeyRegistry::registerAll(std::string& error_out) {
#ifdef _WIN32
    bool all_ok = true;
    std::string errs;
    for (const auto& e : g_entries) {
        std::string one;
        if (!registerOne(e.id, e.modifiers, e.virtual_key, one)) {
            all_ok = false;
            if (!errs.empty()) errs += "; ";
            errs += e.hotkey_spec + " (" + one + ")";
        }
    }
    if (!all_ok) error_out = errs;
    return all_ok;
#else
    (void)error_out;
    return false;
#endif
}

void HotkeyRegistry::unregisterBindings() {
#ifdef _WIN32
    for (const auto& e : g_entries) {
        if (!isMouseKey(e.virtual_key)) {
            UnregisterHotKey(targetWindow(), e.id);
        }
    }
#endif
    g_entries.clear();
    g_suspended = false;
}

void HotkeyRegistry::unregisterAll() {
    unregisterBindings();
#ifdef _WIN32
    if (g_stop_registered) {
        UnregisterHotKey(targetWindow(), kStopHotkeyId);
        g_stop_registered = false;
    }
#endif
}

void HotkeyRegistry::suspendMacroHotkeys() {
#ifdef _WIN32
    if (g_entries.empty() || g_suspended.exchange(true)) return;
    for (const auto& e : g_entries) {
        if (!isMouseKey(e.virtual_key)) {
            UnregisterHotKey(targetWindow(), e.id);
        }
    }
#endif
}

void HotkeyRegistry::resumeMacroHotkeys() {
#ifdef _WIN32
    if (!g_suspended.exchange(false)) return;
    for (const auto& e : g_entries) {
        if (!isMouseKey(e.virtual_key)) {
            RegisterHotKey(targetWindow(), e.id, e.modifiers, e.virtual_key);
        }
    }
    flushPendingMacroHotkeys();
#endif
}

void HotkeyRegistry::flushPendingMacroHotkeys() {
#ifdef _WIN32
    MSG msg{};
    while (PeekMessage(&msg, nullptr, WM_HOTKEY, WM_HOTKEY, PM_REMOVE)) {
        if (static_cast<int>(msg.wParam) == kStopHotkeyId ||
            static_cast<int>(msg.wParam) >= kAppHotkeyIdBase) {
            PostMessage(nullptr, WM_HOTKEY, msg.wParam, msg.lParam);
        }
    }
#endif
}

bool HotkeyRegistry::macroHotkeysSuspended() {
    return g_suspended.load();
}

bool HotkeyRegistry::registerStopHotkey(const std::string& spec, std::string& error_out) {
#ifdef _WIN32
    const ParsedHotkey hk = parseHotkey(spec);
    if (!hk.valid) {
        error_out = "Invalid stop hotkey: " + spec;
        return false;
    }
    if (g_stop_registered) {
        UnregisterHotKey(targetWindow(), kStopHotkeyId);
        g_stop_registered = false;
    }
    if (!registerOne(kStopHotkeyId, hk.modifiers | MOD_NOREPEAT, hk.virtual_key, error_out))
        return false;
    g_stop_registered = true;
    return true;
#else
    (void)spec;
    (void)error_out;
    return false;
#endif
}

} // namespace undek::bindings
