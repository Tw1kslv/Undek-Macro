#pragma once

#include "bindings/BindingConfig.h"

#include <string>
#include <vector>

namespace undek::bindings {

struct RegisteredHotkey {
    int id = 0;
    unsigned int modifiers = 0;
    unsigned int virtual_key = 0;
    std::string hotkey_spec;
    Binding binding;
};

class HotkeyRegistry {
public:
    static void setWindow(void* hwnd);
    static void* window();

    static void clear();
    static void add(RegisteredHotkey entry);
    static const std::vector<RegisteredHotkey>& entries();

    static bool registerStopHotkey(const std::string& spec, std::string& error_out);
    static bool registerAll(std::string& error_out);
    static void unregisterBindings();
    static void unregisterAll();

    static void suspendMacroHotkeys();
    static void resumeMacroHotkeys();
    static void flushPendingMacroHotkeys();
    static bool macroHotkeysSuspended();
};

} // namespace undek::bindings
