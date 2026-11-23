#include "input/Win32SendInputBackend.h"

#include "core/ScanKey.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <vector>

namespace {

// Strict UTF-8 decode; returns an empty vector for malformed input.
std::vector<unsigned int> utf8CodePoints(const std::string& s) {
    std::vector<unsigned int> out;
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned int cp = 0;
        int extra = 0;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1Fu;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0Fu;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07u;
            extra = 3;
        } else {
            return {};
        }
        if (i + static_cast<size_t>(extra) >= s.size()) return {};
        for (int k = 1; k <= extra; ++k) {
            const unsigned char cc = static_cast<unsigned char>(s[i + static_cast<size_t>(k)]);
            if ((cc & 0xC0) != 0x80) return {};
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        i += static_cast<size_t>(extra) + 1;
        out.push_back(cp);
    }
    return out;
}

} // namespace

namespace undek {

Vec2 Win32SendInputBackend::cursorPosition() const {
    POINT pt{};
    GetCursorPos(&pt);
    return {static_cast<double>(pt.x), static_cast<double>(pt.y)};
}

InputResult Win32SendInputBackend::sendOne(INPUT& in, const char* what) {
    if (SendInput(1, &in, sizeof(INPUT)) == 1) return InputResult::success();
    return InputResult::fail(std::string(what) + " was blocked (error " +
                             std::to_string(GetLastError()) + ")");
}

InputResult Win32SendInputBackend::moveAbsolute(int x, int y) {
    // Use the virtual desktop so monitors placed left of or above the
    // primary one don't shift every absolute move.
    const int origin_x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int origin_y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    const int w = (width > 1) ? width - 1 : 1;
    const int h = (height > 1) ? height - 1 : 1;

    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = static_cast<LONG>((static_cast<long long>(x - origin_x) * 65535) / w);
    in.mi.dy = static_cast<LONG>((static_cast<long long>(y - origin_y) * 65535) / h);
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    return sendOne(in, "mouse move");
}

InputResult Win32SendInputBackend::moveRelative(int dx, int dy) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = dx;
    in.mi.dy = dy;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    return sendOne(in, "relative mouse move");
}

DWORD Win32SendInputBackend::mouseDownFlagFor(const std::string& button) {
    std::string b = button;
    std::transform(b.begin(), b.end(), b.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (b == "RIGHT" || b == "RMOUSE" || b == "{RMOUSE}") return MOUSEEVENTF_RIGHTDOWN;
    if (b == "LEFT" || b == "LMOUSE" || b == "{LMOUSE}") return MOUSEEVENTF_LEFTDOWN;
    if (b == "MIDDLE" || b == "MMOUSE") return MOUSEEVENTF_MIDDLEDOWN;
    if (b == "X1" || b == "XBUTTON1" || b == "MOUSE4") return MOUSEEVENTF_XDOWN;
    if (b == "X2" || b == "XBUTTON2" || b == "MOUSE5") return MOUSEEVENTF_XDOWN;
    return MOUSEEVENTF_RIGHTDOWN;
}

DWORD Win32SendInputBackend::mouseUpFlagFor(const std::string& button) {
    std::string b = button;
    std::transform(b.begin(), b.end(), b.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (b == "RIGHT" || b == "RMOUSE" || b == "{RMOUSE}") return MOUSEEVENTF_RIGHTUP;
    if (b == "LEFT" || b == "LMOUSE" || b == "{LMOUSE}") return MOUSEEVENTF_LEFTUP;
    if (b == "MIDDLE" || b == "MMOUSE") return MOUSEEVENTF_MIDDLEUP;
    if (b == "X1" || b == "XBUTTON1" || b == "MOUSE4") return MOUSEEVENTF_XUP;
    if (b == "X2" || b == "XBUTTON2" || b == "MOUSE5") return MOUSEEVENTF_XUP;
    return MOUSEEVENTF_RIGHTUP;
}

DWORD Win32SendInputBackend::mouseDataFor(const std::string& button) {
    std::string b = button;
    std::transform(b.begin(), b.end(), b.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (b == "X1" || b == "XBUTTON1" || b == "MOUSE4") return XBUTTON1;
    if (b == "X2" || b == "XBUTTON2" || b == "MOUSE5") return XBUTTON2;
    return 0;
}

InputResult Win32SendInputBackend::mouseDown(const std::string& button) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = mouseDownFlagFor(button);
    in.mi.mouseData = mouseDataFor(button);
    return sendOne(in, "mouse down");
}

InputResult Win32SendInputBackend::mouseUp(const std::string& button) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = mouseUpFlagFor(button);
    in.mi.mouseData = mouseDataFor(button);
    return sendOne(in, "mouse up");
}

InputResult Win32SendInputBackend::mouseWheel(int delta) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_WHEEL;
    in.mi.mouseData = static_cast<DWORD>(delta);
    return sendOne(in, "scroll");
}

InputResult Win32SendInputBackend::clickLeft() {
    INPUT down{};
    down.type = INPUT_MOUSE;
    down.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    INPUT up{};
    up.type = INPUT_MOUSE;
    up.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    INPUT seq[2] = {down, up};
    const UINT sent = SendInput(2, seq, sizeof(INPUT));
    if (sent == 2) return InputResult::success();

    std::string why = "click only got " + std::to_string(sent) + " of 2 events through";
    if (sent == 1) {
        // The press landed and the release didn't, so try once more to let go
        // of the button before telling anyone about it.
        INPUT up_only[1] = {up};
        const UINT released = SendInput(1, up_only, sizeof(INPUT));
        if (released == 1) {
            why += " (released the button again)";
        } else {
            why += " (and the mouse-up cleanup failed too)";
            // Both releases lost, so the button is likely still held and the
            // run's cleanup is the only thing left that can let go of it.
            return InputResult::uncertain(why);
        }
    }
    return InputResult::fail(why);
}

WORD Win32SendInputBackend::virtualKeyFor(const std::string& key) {
    const auto cps = utf8CodePoints(key);
    if (cps.size() == 1) {
        const unsigned int cp = cps.front();
        if (cp > 0xFFFF) return 0; // astral plane: typed via Unicode input
        const SHORT scan = VkKeyScanW(static_cast<wchar_t>(cp));
        if (scan == -1) return 0;
        const WORD scanned = static_cast<WORD>(scan);
        // Bits 1-2 mean the character needs Ctrl or AltGr, which plain
        // VK injection cannot express reliably; type it as Unicode instead.
        if ((HIBYTE(scanned) & 0x06) != 0) return 0;
        return static_cast<WORD>(LOBYTE(scanned));
    }
    std::string upper = key;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    if (upper == "SPACE") return VK_SPACE;
    if (upper == "ENTER" || upper == "RETURN") return VK_RETURN;
    if (upper == "TAB") return VK_TAB;
    if (upper == "ESC" || upper == "ESCAPE") return VK_ESCAPE;
    if (upper == "SHIFT") return VK_SHIFT;
    if (upper == "CTRL" || upper == "CONTROL") return VK_CONTROL;
    if (upper == "ALT") return VK_MENU;
    if (upper == "BACKSPACE") return VK_BACK;
    if (upper == "DELETE") return VK_DELETE;
    if (upper == "INSERT") return VK_INSERT;
    if (upper == "HOME") return VK_HOME;
    if (upper == "END") return VK_END;
    if (upper == "PAGEUP" || upper == "PRIOR") return VK_PRIOR;
    if (upper == "PAGEDOWN" || upper == "NEXT") return VK_NEXT;
    if (upper == "LEFT") return VK_LEFT;
    if (upper == "RIGHT") return VK_RIGHT;
    if (upper == "UP") return VK_UP;
    if (upper == "DOWN") return VK_DOWN;
    if (upper == "CAPSLOCK") return VK_CAPITAL;
    if (upper.size() == 7 && upper.rfind("NUMPAD", 0) == 0 && upper[6] >= '0' && upper[6] <= '9') {
        return static_cast<WORD>(VK_NUMPAD0 + (upper[6] - '0'));
    }

    for (int i = 1; i <= 12; ++i) {
        if (upper == "F" + std::to_string(i)) {
            return static_cast<WORD>(VK_F1 + i - 1);
        }
    }
    return 0;
}

bool Win32SendInputBackend::needsShiftFor(const std::string& key) {
    const auto cps = utf8CodePoints(key);
    if (cps.size() != 1 || cps.front() > 0xFFFF) return false;
    const SHORT scan = VkKeyScanW(static_cast<wchar_t>(cps.front()));
    if (scan == -1) return false;
    return (HIBYTE(static_cast<WORD>(scan)) & 1) != 0;
}

bool Win32SendInputBackend::supportsKey(const std::string& key) const {
    ScanKey scan{};
    if (parseScanKey(key, scan)) return true;
    if (virtualKeyFor(key) != 0) return true;
    // A single character without a layout key can still be typed through
    // Unicode injection; unknown multi-character key names cannot.
    return utf8CodePoints(key).size() == 1;
}

InputResult Win32SendInputBackend::sendVk(WORD vk, bool up) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    return sendOne(in, "key event");
}

InputResult Win32SendInputBackend::keyDown(const std::string& key) {
    ScanKey scan{};
    if (parseScanKey(key, scan)) {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wScan = scan.code;
        in.ki.dwFlags = KEYEVENTF_SCANCODE | (scan.extended ? KEYEVENTF_EXTENDEDKEY : 0);
        return sendOne(in, "scan code key");
    }

    const WORD vk = virtualKeyFor(key);
    if (!vk) return sendUnicodeKey(key, false);

    if (needsShiftFor(key)) {
        const InputResult shift = sendVk(VK_SHIFT, false);
        if (!shift.ok) return shift;
    }
    return sendVk(vk, false);
}

InputResult Win32SendInputBackend::keyUp(const std::string& key) {
    ScanKey scan{};
    if (parseScanKey(key, scan)) {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wScan = scan.code;
        in.ki.dwFlags =
            KEYEVENTF_SCANCODE | (scan.extended ? KEYEVENTF_EXTENDEDKEY : 0) | KEYEVENTF_KEYUP;
        return sendOne(in, "scan code key");
    }

    const WORD vk = virtualKeyFor(key);
    if (!vk) return sendUnicodeKey(key, true);

    const bool shift = needsShiftFor(key);
    const InputResult released = sendVk(vk, true);
    if (shift) sendVk(VK_SHIFT, true);
    return released;
}

InputResult Win32SendInputBackend::sendUnicode(unsigned short unit, bool up) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = 0;
    in.ki.wScan = unit;
    in.ki.dwFlags = KEYEVENTF_UNICODE | (up ? KEYEVENTF_KEYUP : 0);
    return sendOne(in, "unicode key");
}

InputResult Win32SendInputBackend::sendUnicodeKey(const std::string& key, bool up) {
    const auto cps = utf8CodePoints(key);
    if (cps.size() != 1) return InputResult::fail("cannot type '" + key + "'");
    const unsigned int cp = cps.front();
    if (cp <= 0xFFFF) return sendUnicode(static_cast<unsigned short>(cp), up);
    const unsigned int v = cp - 0x10000u;
    const InputResult high = sendUnicode(static_cast<unsigned short>(0xD800u + (v >> 10)), up);
    if (!high.ok) return high;
    return sendUnicode(static_cast<unsigned short>(0xDC00u + (v & 0x3FFu)), up);
}

} // namespace undek

#else

namespace undek {

Vec2 Win32SendInputBackend::cursorPosition() const {
    return {};
}
InputResult Win32SendInputBackend::moveAbsolute(int, int) {
    return InputResult::fail("only supported on Windows");
}
InputResult Win32SendInputBackend::moveRelative(int, int) {
    return InputResult::fail("only supported on Windows");
}
InputResult Win32SendInputBackend::clickLeft() {
    return InputResult::fail("only supported on Windows");
}
InputResult Win32SendInputBackend::mouseDown(const std::string&) {
    return InputResult::fail("only supported on Windows");
}
InputResult Win32SendInputBackend::mouseUp(const std::string&) {
    return InputResult::fail("only supported on Windows");
}
InputResult Win32SendInputBackend::mouseWheel(int) {
    return InputResult::fail("only supported on Windows");
}
InputResult Win32SendInputBackend::keyDown(const std::string&) {
    return InputResult::fail("only supported on Windows");
}
InputResult Win32SendInputBackend::keyUp(const std::string&) {
    return InputResult::fail("only supported on Windows");
}
WORD Win32SendInputBackend::virtualKeyFor(const std::string&) {
    return 0;
}
DWORD Win32SendInputBackend::mouseDownFlagFor(const std::string&) {
    return 0;
}
DWORD Win32SendInputBackend::mouseUpFlagFor(const std::string&) {
    return 0;
}
DWORD Win32SendInputBackend::mouseDataFor(const std::string&) {
    return 0;
}
bool Win32SendInputBackend::needsShiftFor(const std::string&) {
    return false;
}
bool Win32SendInputBackend::supportsKey(const std::string&) const {
    return false;
}
InputResult Win32SendInputBackend::sendVk(WORD, bool) {
    return InputResult::fail("only supported on Windows");
}
InputResult Win32SendInputBackend::sendUnicode(unsigned short, bool) {
    return InputResult::fail("only supported on Windows");
}
InputResult Win32SendInputBackend::sendUnicodeKey(const std::string&, bool) {
    return InputResult::fail("only supported on Windows");
}

} // namespace undek

#endif
