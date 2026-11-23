#include "app/SingleInstance.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace undek {
namespace {

bool g_claimed = false;

#ifdef _WIN32
HANDLE g_lock = nullptr;

// The window class, not the title: the title carries the version number, so it
// changes with every release.
void bringRunningEditorForward() {
    const HWND running = FindWindowA("UndekMacro", nullptr);
    if (!running) return;
    ShowWindow(running, SW_RESTORE);
    SetForegroundWindow(running);
}
#endif

} // namespace

bool claimEditorInstance(std::string& error_out) {
    if (g_claimed) return true;

#ifdef _WIN32
    g_lock = CreateMutexA(nullptr, TRUE, "Local\\UndekMacro-Editor");
    if (!g_lock) {
        // An unusual failure here shouldn't keep the editor shut.
        g_claimed = true;
        return true;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        bringRunningEditorForward();
        error_out = "Undek Macro is already running - bringing that window forward.";
        return false;
    }
#else
    (void)error_out;
#endif

    g_claimed = true;
    return true;
}

} // namespace undek
