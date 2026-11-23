#pragma once

#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace undek::bindings {

struct ParsedHotkey {
    unsigned int modifiers = 0;
    unsigned int virtual_key = 0;
    bool valid = false;
};

ParsedHotkey parseHotkey(const std::string& spec);
bool isMouseKey(unsigned int vk);
bool hotkeysEquivalent(const std::string& a, const std::string& b);
std::string pollPressedHotkey(bool& cancelled_out);

} // namespace undek::bindings
