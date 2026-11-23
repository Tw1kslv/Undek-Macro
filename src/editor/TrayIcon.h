#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h> // before shellapi.h, which needs its types
#include <shellapi.h>

namespace undek::editor {

constexpr UINT kTrayMessage = WM_APP + 1;

enum TrayCommand : UINT {
    kTrayShowHide = 1001,
    kTrayStopMacro = 1002,
    kTrayQuit = 1003,
};

class TrayIcon {
public:
    bool create(HWND hwnd, const wchar_t* tooltip);
    void destroy();
    UINT showMenu(HWND hwnd, bool window_visible) const;
    bool active() const { return active_; }

private:
    NOTIFYICONDATAW data_{};
    bool active_ = false;
};

} // namespace undek::editor
