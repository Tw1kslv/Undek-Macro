#include "screen/ScreenSampler.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace undek {
namespace {

#ifdef _WIN32
// One screen DC per thread. PixelAt runs inside WaitUntil loops, and getting
// and releasing a DC for every lookup cost more than the lookup itself.
struct ScreenDc {
    HDC dc = GetDC(nullptr);
    ~ScreenDc() {
        if (dc) ReleaseDC(nullptr, dc);
    }
};

HDC screenDc() {
    thread_local ScreenDc cached;
    return cached.dc;
}
#endif

std::string lowerCase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

std::optional<unsigned int> ScreenSampler::pixelColor(int x, int y) {
#ifdef _WIN32
    const HDC hdc = screenDc();
    if (!hdc) return std::nullopt;
    const COLORREF c = GetPixel(hdc, x, y);
    if (c == CLR_INVALID) return std::nullopt;
    const int r = GetRValue(c);
    const int g = GetGValue(c);
    const int b = GetBValue(c);
    return (static_cast<unsigned int>(r) << 16) | (static_cast<unsigned int>(g) << 8) |
           static_cast<unsigned int>(b);
#else
    // No screen here, and saying black would be a lie a condition could act on.
    (void)x;
    (void)y;
    return std::nullopt;
#endif
}

bool ScreenSampler::colorMatches(unsigned int actual, unsigned int expected, int tolerance) {
    if (tolerance <= 0) return actual == expected;
    for (int shift = 0; shift <= 16; shift += 8) {
        const int a = static_cast<int>((actual >> shift) & 0xFF);
        const int e = static_cast<int>((expected >> shift) & 0xFF);
        if (std::abs(a - e) > tolerance) return false;
    }
    return true;
}

std::string WindowInfo::foregroundTitle() {
#ifdef _WIN32
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return {};
    char buf[512]{};
    GetWindowTextA(hwnd, buf, sizeof(buf));
    return buf;
#else
    return {};
#endif
}

bool WindowInfo::titleContains(const std::string& needle) {
    if (needle.empty()) return true;

    // The same needle gets checked before every statement while a run is going,
    // so only fold it again when it actually changes.
    thread_local std::string cached_needle;
    thread_local std::string cached_lower;
    if (cached_needle != needle) {
        cached_needle = needle;
        cached_lower = lowerCase(needle);
    }

    std::string title = lowerCase(foregroundTitle());
    return title.find(cached_lower) != std::string::npos;
}

} // namespace undek
