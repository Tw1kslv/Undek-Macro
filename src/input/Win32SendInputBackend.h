#pragma once

#include "input/IInputBackend.h"

#ifdef _WIN32
#include <windows.h>
#else
// The Windows types below are only ever touched by the Windows build. These
// stand-ins keep the header, and everything including it, buildable elsewhere,
// which is how the backend factory gets tested off Windows.
using WORD = unsigned short;
using DWORD = unsigned long;
struct INPUT {};
#endif

#include <unordered_map>

namespace undek {

class Win32SendInputBackend : public IInputBackend {
public:
    Vec2 cursorPosition() const override;
    InputResult moveAbsolute(int x, int y) override;
    InputResult moveRelative(int dx, int dy) override;
    InputResult clickLeft() override;
    InputResult mouseDown(const std::string& button) override;
    InputResult mouseUp(const std::string& button) override;
    InputResult mouseWheel(int delta) override;
    InputResult keyDown(const std::string& key) override;
    InputResult keyUp(const std::string& key) override;
    bool supportsKey(const std::string& key) const override;

    static WORD virtualKeyFor(const std::string& key);
    static bool needsShiftFor(const std::string& key);

private:
    static DWORD mouseDownFlagFor(const std::string& button);
    static DWORD mouseUpFlagFor(const std::string& button);
    static DWORD mouseDataFor(const std::string& button);
    static InputResult sendOne(INPUT& in, const char* what);
    static InputResult sendVk(WORD vk, bool up);
    // Types one character that has no key on the current layout (or needs
    // Ctrl/AltGr) by injecting its UTF-16 code units directly.
    static InputResult sendUnicodeKey(const std::string& key, bool up);
    static InputResult sendUnicode(unsigned short unit, bool up);
};

} // namespace undek
