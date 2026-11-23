#include "vision/ScreenGrab.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace undek {

#ifdef _WIN32

bool ScreenGrab::capture(ImageFrame& out, std::string& error) {
    const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (w <= 0 || h <= 0) {
        error = "the virtual desktop has no size";
        return false;
    }

    HDC screen = GetDC(nullptr);
    if (!screen) {
        error = "can't get a screen device context";
        return false;
    }
    HDC mem = CreateCompatibleDC(screen);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h; // top-down, so rows already match ImageFrame
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap =
        mem ? CreateDIBSection(mem, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    if (!mem || !bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        if (mem) DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        error = "can't create the capture bitmap";
        return false;
    }

    const HGDIOBJ old = SelectObject(mem, bitmap);
    const BOOL copied = BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY | CAPTUREBLT);

    if (copied) {
        out.width = w;
        out.height = h;
        out.origin_x = x;
        out.origin_y = y;
        out.pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
        const auto* src = static_cast<const uint32_t*>(bits);
        for (size_t i = 0; i < out.pixels.size(); ++i) {
            const uint32_t bgra = src[i];
            out.pixels[i] = ((bgra & 0x0000FF) << 16) | (bgra & 0x00FF00) | ((bgra >> 16) & 0xFF);
        }
    } else {
        error = "the screen copy failed";
    }

    SelectObject(mem, old);
    DeleteObject(bitmap);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    if (copied) error.clear();
    return copied == TRUE;
}

#else

bool ScreenGrab::capture(ImageFrame& out, std::string& error) {
    (void)out;
    error = "screen capture is only available on Windows";
    return false;
}

#endif

} // namespace undek
