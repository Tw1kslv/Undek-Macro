#include "editor/TrayIcon.h"

#include "editor/AppIcon.h"

#include <cwchar>

namespace undek::editor {

bool TrayIcon::create(HWND hwnd, const wchar_t* tooltip) {
    if (active_) return true;
    data_ = {};
    data_.cbSize = sizeof(data_);
    data_.hWnd = hwnd;
    data_.uID = 1;
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data_.uCallbackMessage = kTrayMessage;
    data_.hIcon = loadAppIcon();
    wcsncpy_s(data_.szTip, tooltip, _TRUNCATE);
    active_ = Shell_NotifyIconW(NIM_ADD, &data_) == TRUE;
    return active_;
}

void TrayIcon::destroy() {
    if (!active_) return;
    Shell_NotifyIconW(NIM_DELETE, &data_);
    active_ = false;
}

UINT TrayIcon::showMenu(HWND hwnd, bool window_visible) const {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kTrayShowHide, window_visible ? L"Hide window" : L"Show window");
    AppendMenuW(menu, MF_STRING, kTrayStopMacro, L"Stop running macro");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayQuit, L"Quit");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    const UINT cmd = static_cast<UINT>(TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr));
    PostMessage(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
    return cmd;
}

} // namespace undek::editor
