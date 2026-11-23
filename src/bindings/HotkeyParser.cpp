#include "bindings/HotkeyParser.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>

namespace undek::bindings {
namespace {

std::string trim(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

std::string upper(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

unsigned int modifierFlag(const std::string& token) {
    static const std::unordered_map<std::string, unsigned int> mods = {
        {"CTRL", MOD_CONTROL}, {"CONTROL", MOD_CONTROL}, {"ALT", MOD_ALT},  {"MENU", MOD_ALT},
        {"SHIFT", MOD_SHIFT},  {"WIN", MOD_WIN},         {"LWIN", MOD_WIN}, {"RWIN", MOD_WIN},
    };
    const auto it = mods.find(upper(token));
    return (it != mods.end()) ? it->second : 0;
}

unsigned int virtualKeyForToken(const std::string& token) {
    const std::string t = upper(token);

    if (t.size() == 1) {
        const char c = t[0];
        if (c >= 'A' && c <= 'Z') return static_cast<unsigned int>(c);
        if (c >= '0' && c <= '9') return static_cast<unsigned int>(c);
    }

    if (t.rfind("F", 0) == 0 && t.size() >= 2) {
        try {
            const int fn = std::stoi(t.substr(1));
            if (fn >= 1 && fn <= 24) return static_cast<unsigned int>(VK_F1 + fn - 1);
        } catch (...) {
        }
    }

    static const std::unordered_map<std::string, unsigned int> named = {
        {"SPACE", VK_SPACE},
        {"TAB", VK_TAB},
        {"ENTER", VK_RETURN},
        {"RETURN", VK_RETURN},
        {"ESC", VK_ESCAPE},
        {"ESCAPE", VK_ESCAPE},
        {"BACKSPACE", VK_BACK},
        {"DELETE", VK_DELETE},
        {"DEL", VK_DELETE},
        {"INSERT", VK_INSERT},
        {"HOME", VK_HOME},
        {"END", VK_END},
        {"PAGEUP", VK_PRIOR},
        {"PAGEDOWN", VK_NEXT},
        {"UP", VK_UP},
        {"DOWN", VK_DOWN},
        {"LEFT", VK_LEFT},
        {"RIGHT", VK_RIGHT},
        {"NUMPAD0", VK_NUMPAD0},
        {"NUMPAD1", VK_NUMPAD1},
        {"NUMPAD2", VK_NUMPAD2},
        {"NUMPAD3", VK_NUMPAD3},
        {"NUMPAD4", VK_NUMPAD4},
        {"NUMPAD5", VK_NUMPAD5},
        {"NUMPAD6", VK_NUMPAD6},
        {"NUMPAD7", VK_NUMPAD7},
        {"NUMPAD8", VK_NUMPAD8},
        {"NUMPAD9", VK_NUMPAD9},
        {"MOUSE4", VK_XBUTTON1},
        {"XBUTTON1", VK_XBUTTON1},
        {"MOUSE5", VK_XBUTTON2},
        {"XBUTTON2", VK_XBUTTON2},
        {"MOUSE3", VK_MBUTTON},
        {"MBUTTON", VK_MBUTTON},
        {"MIDDLEMOUSE", VK_MBUTTON},
        {"MOUSE1", VK_LBUTTON},
        {"LBUTTON", VK_LBUTTON},
        {"LEFTMOUSE", VK_LBUTTON},
        {"MOUSE2", VK_RBUTTON},
        {"RBUTTON", VK_RBUTTON},
        {"RIGHTMOUSE", VK_RBUTTON},
        {"CAPSLOCK", VK_CAPITAL},
        {"TILDE", VK_OEM_3},
        {"GRAVE", VK_OEM_3},
        {"MINUS", VK_OEM_MINUS},
        {"EQUALS", VK_OEM_PLUS},
        {"LBRACKET", VK_OEM_4},
        {"RBRACKET", VK_OEM_6},
        {"SEMICOLON", VK_OEM_1},
        {"QUOTE", VK_OEM_7},
        {"COMMA", VK_OEM_COMMA},
        {"PERIOD", VK_OEM_PERIOD},
        {"SLASH", VK_OEM_2},
    };
    const auto it = named.find(t);
    if (it != named.end()) return it->second;

    return 0;
}

} // namespace

bool isMouseKey(unsigned int vk) {
    return vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 ||
           vk == VK_XBUTTON2;
}

ParsedHotkey parseHotkey(const std::string& spec) {
    ParsedHotkey out;
    std::stringstream ss(spec);
    std::string part;
    std::string last_key;

    while (std::getline(ss, part, '+')) {
        part = upper(trim(part));
        if (part.empty()) continue;

        const unsigned int mod = modifierFlag(part);
        if (mod != 0) {
            out.modifiers |= mod;
        } else {
            last_key = part;
        }
    }

    if (last_key.empty()) return out;

    out.virtual_key = virtualKeyForToken(last_key);
    out.valid = (out.virtual_key != 0);
    return out;
}

bool hotkeysEquivalent(const std::string& a, const std::string& b) {
    const ParsedHotkey pa = parseHotkey(a);
    const ParsedHotkey pb = parseHotkey(b);
    return pa.valid && pb.valid && pa.modifiers == pb.modifiers && pa.virtual_key == pb.virtual_key;
}

std::string pollPressedHotkey(bool& cancelled_out) {
    cancelled_out = false;
#ifdef _WIN32
    if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0) {
        cancelled_out = true;
        return {};
    }

    std::string prefix;
    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) prefix += "Ctrl+";
    if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) prefix += "Shift+";
    if ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0) prefix += "Alt+";

    if ((GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0) return prefix + "Mouse4";
    if ((GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0) return prefix + "Mouse5";
    if ((GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0) return prefix + "Mouse3";
    if ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0) return prefix + "Mouse2";

    for (int fn = 1; fn <= 24; ++fn) {
        if ((GetAsyncKeyState(VK_F1 + fn - 1) & 0x8000) != 0) {
            return prefix + "F" + std::to_string(fn);
        }
    }

    for (char c = 'A'; c <= 'Z'; ++c) {
        if ((GetAsyncKeyState(c) & 0x8000) != 0) {
            return prefix + std::string(1, c);
        }
    }

    for (char c = '0'; c <= '9'; ++c) {
        if ((GetAsyncKeyState(c) & 0x8000) != 0) {
            return prefix + std::string(1, c);
        }
    }

    struct KeyPair {
        int vk;
        const char* name;
    };
    static const KeyPair extras[] = {
        {VK_SPACE, "Space"},     {VK_TAB, "Tab"},         {VK_RETURN, "Enter"},
        {VK_BACK, "Backspace"},  {VK_DELETE, "Delete"},   {VK_INSERT, "Insert"},
        {VK_HOME, "Home"},       {VK_END, "End"},         {VK_PRIOR, "PageUp"},
        {VK_NEXT, "PageDown"},   {VK_UP, "Up"},           {VK_DOWN, "Down"},
        {VK_LEFT, "Left"},       {VK_RIGHT, "Right"},     {VK_CAPITAL, "CapsLock"},
        {VK_OEM_3, "Tilde"},     {VK_NUMPAD0, "Numpad0"}, {VK_NUMPAD1, "Numpad1"},
        {VK_NUMPAD2, "Numpad2"}, {VK_NUMPAD3, "Numpad3"}, {VK_NUMPAD4, "Numpad4"},
        {VK_NUMPAD5, "Numpad5"}, {VK_NUMPAD6, "Numpad6"}, {VK_NUMPAD7, "Numpad7"},
        {VK_NUMPAD8, "Numpad8"}, {VK_NUMPAD9, "Numpad9"},
    };
    for (const auto& kp : extras) {
        if ((GetAsyncKeyState(kp.vk) & 0x8000) != 0) {
            return prefix + kp.name;
        }
    }
#endif
    return {};
}

} // namespace undek::bindings
