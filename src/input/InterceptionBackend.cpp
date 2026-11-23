#include "input/InterceptionBackend.h"

#include "core/ScanKey.h"

#include <cctype>
#include <cmath>

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

#ifdef _WIN32
namespace {

struct InterceptionKeyStroke {
    unsigned short code;
    unsigned short state;
    unsigned int information;
};

struct InterceptionMouseStroke {
    unsigned short state;
    unsigned short flags;
    short rolling;
    int x;
    int y;
    unsigned int information;
};

constexpr unsigned short kKeyDown = 0x00;
constexpr unsigned short kKeyUp = 0x01;
constexpr unsigned short kKeyE0 = 0x02;

constexpr unsigned short kMouseLeftDown = 0x001;
constexpr unsigned short kMouseLeftUp = 0x002;
constexpr unsigned short kMouseRightDown = 0x004;
constexpr unsigned short kMouseRightUp = 0x008;
constexpr unsigned short kMouseMiddleDown = 0x010;
constexpr unsigned short kMouseMiddleUp = 0x020;
constexpr unsigned short kMouseX1Down = 0x040;
constexpr unsigned short kMouseX1Up = 0x080;
constexpr unsigned short kMouseX2Down = 0x100;
constexpr unsigned short kMouseX2Up = 0x200;

constexpr unsigned short kMouseMoveRelative = 0x000;
constexpr unsigned short kMouseMoveAbsolute = 0x001;
constexpr unsigned short kMouseWheel = 0x800;

// Arrows, nav keys and the right-hand modifiers all carry the E0 prefix.
bool isExtendedVk(WORD vk) {
    switch (vk) {
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_INSERT:
        case VK_DELETE:
        case VK_DIVIDE:
        case VK_NUMLOCK:
        case VK_RCONTROL:
        case VK_RMENU:
            return true;
        default:
            return false;
    }
}

constexpr int kKeyboardDeviceBase = 1;
constexpr int kMouseDeviceBase = 11;
constexpr int kMaxDeviceIndex = 10;

} // namespace
#endif

InterceptionBackend::InterceptionBackend() {
#ifdef _WIN32
    // Only next to the exe or in system32. A plain LoadLibrary would also take
    // an interception.dll out of the working directory or PATH, and those are
    // places anything can drop a file.
    module_ = static_cast<void*>(
        LoadLibraryExA("interception.dll", nullptr,
                       LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32));
    if (!module_) return;

    auto sym = [this](const char* name) {
        return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module_), name));
    };

    api_.create_context =
        reinterpret_cast<decltype(api_.create_context)>(sym("interception_create_context"));
    api_.destroy_context =
        reinterpret_cast<decltype(api_.destroy_context)>(sym("interception_destroy_context"));
    api_.send = reinterpret_cast<decltype(api_.send)>(sym("interception_send"));
    api_.get_hardware_id =
        reinterpret_cast<decltype(api_.get_hardware_id)>(sym("interception_get_hardware_id"));

    if (!api_.create_context || !api_.destroy_context || !api_.send || !api_.get_hardware_id) {
        FreeLibrary(static_cast<HMODULE>(module_));
        module_ = nullptr;
        return;
    }

    context_ = api_.create_context();
    if (!context_) return;

    char hwid[256];
    for (int i = 0; i < kMaxDeviceIndex && keyboard_device_ == 0; ++i) {
        if (api_.get_hardware_id(context_, kKeyboardDeviceBase + i, hwid, sizeof(hwid)) > 0) {
            keyboard_device_ = kKeyboardDeviceBase + i;
        }
    }
    for (int i = 0; i < kMaxDeviceIndex && mouse_device_ == 0; ++i) {
        if (api_.get_hardware_id(context_, kMouseDeviceBase + i, hwid, sizeof(hwid)) > 0) {
            mouse_device_ = kMouseDeviceBase + i;
        }
    }
    if (keyboard_device_ == 0) keyboard_device_ = kKeyboardDeviceBase;
    if (mouse_device_ == 0) mouse_device_ = kMouseDeviceBase;

    live_ = true;
#endif
}

InterceptionBackend::~InterceptionBackend() {
#ifdef _WIN32
    if (context_ && api_.destroy_context) api_.destroy_context(context_);
    context_ = nullptr;
    if (module_) FreeLibrary(static_cast<HMODULE>(module_));
    module_ = nullptr;
#endif
}

const char* InterceptionBackend::statusName() const {
#ifdef _WIN32
    if (!module_) return "interception.dll not found next to the app";
    if (!context_) return "driver not installed";
    if (!live_) return "send failed, using SendInput";
    return "Interception driver";
#else
    return "unsupported platform";
#endif
}

Vec2 InterceptionBackend::cursorPosition() const {
    return fallback_.cursorPosition();
}

#ifdef _WIN32
bool InterceptionBackend::sendKeyboard(unsigned short code, bool up, bool e0) {
    if (!context_ || !live_ || keyboard_device_ == 0) return false;
    InterceptionKeyStroke k{};
    k.code = code;
    k.state = (up ? kKeyUp : kKeyDown) | (e0 ? kKeyE0 : 0);
    k.information = 0;
    const int sent = api_.send(context_, keyboard_device_, &k, 1);
    if (sent <= 0) live_ = false;
    return sent > 0;
}

bool InterceptionBackend::sendMouseWheel(short delta) {
    if (!context_ || !live_ || mouse_device_ == 0) return false;
    InterceptionMouseStroke m{};
    m.state = kMouseWheel;
    m.rolling = delta;
    const int sent = api_.send(context_, mouse_device_, &m, 1);
    if (sent <= 0) live_ = false;
    return sent > 0;
}

bool InterceptionBackend::sendMouse(unsigned short state, unsigned short flags, int x, int y) {
    if (!context_ || !live_ || mouse_device_ == 0) return false;
    InterceptionMouseStroke m{};
    m.state = state;
    m.flags = flags;
    m.rolling = 0;
    m.x = x;
    m.y = y;
    m.information = 0;
    const int sent = api_.send(context_, mouse_device_, &m, 1);
    if (sent <= 0) live_ = false;
    return sent > 0;
}

unsigned short InterceptionBackend::scancodeFor(const std::string& key, bool& e0) const {
    e0 = false;
    ScanKey raw{};
    if (parseScanKey(key, raw)) {
        e0 = raw.extended;
        return raw.code;
    }
    const WORD vk = Win32SendInputBackend::virtualKeyFor(key);
    if (!vk) return 0;
    const UINT sc = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    if (!sc) return 0;
    e0 = isExtendedVk(vk) || (sc & 0xFF00) != 0;
    return static_cast<unsigned short>(sc & 0xFF);
}
#endif

InputResult InterceptionBackend::keyDown(const std::string& key) {
#ifdef _WIN32
    bool e0 = false;
    const unsigned short sc = scancodeFor(key, e0);
    if (sc) {
        const bool shift = Win32SendInputBackend::needsShiftFor(key);
        bool ok = true;
        if (shift) ok = sendKeyboard(0x2A, false, false);
        if (ok) ok = sendKeyboard(sc, false, e0);
        if (ok) return InputResult::success();
        // Undo the Shift we pressed; if the driver sidesteps the release,
        // release it through SendInput so it is never left held down.
        if (shift && !sendKeyboard(0x2A, true, false)) fallback_.keyUp("shift");
    }
#endif
    return fallback_.keyDown(key);
}

InputResult InterceptionBackend::keyUp(const std::string& key) {
#ifdef _WIN32
    bool e0 = false;
    const unsigned short sc = scancodeFor(key, e0);
    if (sc) {
        const bool shift = Win32SendInputBackend::needsShiftFor(key);
        bool ok = sendKeyboard(sc, true, e0);
        // Release Shift even when the key release failed, otherwise the
        // hardware Shift scancode from keyDown stays down.
        if (shift && !sendKeyboard(0x2A, true, false)) {
            fallback_.keyUp("shift");
            ok = false;
        }
        if (ok) return InputResult::success();
    }
#endif
    return fallback_.keyUp(key);
}

bool InterceptionBackend::supportsKey(const std::string& key) const {
    // Anything the driver cannot encode is emitted through the SendInput
    // fallback, so support is limited by Win32's key mapping.
    return fallback_.supportsKey(key);
}

InputResult InterceptionBackend::moveRelative(int dx, int dy) {
#ifdef _WIN32
    if (dx == 0 && dy == 0) return InputResult::success();
    if (sendMouse(0, kMouseMoveRelative, dx, dy)) return InputResult::success();
#endif
    return fallback_.moveRelative(dx, dy);
}

InputResult InterceptionBackend::moveAbsolute(int x, int y) {
#ifdef _WIN32
    // The driver only understands one normalised range, which is the primary
    // monitor. Anything on another monitor goes through SendInput instead of
    // landing in the wrong place.
    const int screen_w = GetSystemMetrics(SM_CXSCREEN);
    const int screen_h = GetSystemMetrics(SM_CYSCREEN);
    const int sw = (screen_w > 1) ? screen_w - 1 : 1;
    const int sh = (screen_h > 1) ? screen_h - 1 : 1;
    const bool on_primary = x >= 0 && y >= 0 && x < screen_w && y < screen_h;
    if (on_primary) {
        const LONG nx = static_cast<LONG>((x * 65535) / sw);
        const LONG ny = static_cast<LONG>((y * 65535) / sh);
        if (sendMouse(0, kMouseMoveAbsolute, nx, ny)) return InputResult::success();
    }
#endif
    return fallback_.moveAbsolute(x, y);
}

InputResult InterceptionBackend::mouseDown(const std::string& button) {
#ifdef _WIN32
    std::string b = button;
    for (auto& c : b)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    unsigned short state = kMouseRightDown;
    if (b == "LEFT" || b == "LMOUSE" || b == "{LMOUSE}")
        state = kMouseLeftDown;
    else if (b == "MIDDLE" || b == "MMOUSE")
        state = kMouseMiddleDown;
    else if (b == "X1" || b == "XBUTTON1" || b == "MOUSE4")
        state = kMouseX1Down;
    else if (b == "X2" || b == "XBUTTON2" || b == "MOUSE5")
        state = kMouseX2Down;
    if (sendMouse(state, kMouseMoveRelative, 0, 0)) return InputResult::success();
#endif
    return fallback_.mouseDown(button);
}

InputResult InterceptionBackend::mouseUp(const std::string& button) {
#ifdef _WIN32
    std::string b = button;
    for (auto& c : b)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    unsigned short state = kMouseRightUp;
    if (b == "LEFT" || b == "LMOUSE" || b == "{LMOUSE}")
        state = kMouseLeftUp;
    else if (b == "MIDDLE" || b == "MMOUSE")
        state = kMouseMiddleUp;
    else if (b == "X1" || b == "XBUTTON1" || b == "MOUSE4")
        state = kMouseX1Up;
    else if (b == "X2" || b == "XBUTTON2" || b == "MOUSE5")
        state = kMouseX2Up;
    if (sendMouse(state, kMouseMoveRelative, 0, 0)) return InputResult::success();
#endif
    return fallback_.mouseUp(button);
}

InputResult InterceptionBackend::mouseWheel(int delta) {
#ifdef _WIN32
    if (sendMouseWheel(static_cast<short>(delta))) return InputResult::success();
#endif
    return fallback_.mouseWheel(delta);
}

InputResult InterceptionBackend::clickLeft() {
#ifdef _WIN32
    if (context_ && live_ && mouse_device_ != 0) {
        if (!sendMouse(kMouseLeftDown, kMouseMoveRelative, 0, 0)) {
            return fallback_.clickLeft();
        }
        if (!sendMouse(kMouseLeftUp, kMouseMoveRelative, 0, 0)) {
            const InputResult up = fallback_.mouseUp("left");
            // The device press landed and neither way of releasing it did.
            return up.ok ? up : InputResult::uncertain(up.error);
        }
        return InputResult::success();
    }
#endif
    return fallback_.clickLeft();
}

} // namespace undek
