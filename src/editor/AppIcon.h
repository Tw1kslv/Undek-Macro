#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace undek::editor {

// 101 is the icon resources/undek.rc puts in the exe. The generic Windows icon
// is only the fallback for a build that somehow has no resource at all.
inline HICON loadAppIcon() {
    HICON icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
    // IDI_APPLICATION is spelled in the build's character set, and this build
    // is ANSI, so it needs the cast to reach the wide call.
    const LPCWSTR generic_icon = reinterpret_cast<LPCWSTR>(IDI_APPLICATION);
    return icon ? icon : LoadIconW(nullptr, generic_icon);
}

} // namespace undek::editor
