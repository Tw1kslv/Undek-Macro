#pragma once

#include <string>
#include <vector>

namespace undek::bindings {

struct Binding {
    std::string hotkey;
    std::string script;
    std::string profile = "profiles/default.json";
    bool enabled = true;
};

struct WindowProfileRule {
    std::string window_title;
    std::string profile = "profiles/default.json";
};

struct BindingConfig {
    std::vector<Binding> bindings;
    std::vector<WindowProfileRule> window_profiles;
    std::string stop_hotkey = "Ctrl+Shift+Q";
    std::string default_profile = "profiles/default.json";

    std::string resolveProfileForWindow(const std::string& fallback_profile) const;

    // Renaming or deleting a macro used to leave its hotkeys pointing at a file
    // that no longer exists. These keep the list honest and return the number
    // of entries they touched.
    int renameScript(const std::string& from, const std::string& to);
    int forgetScript(const std::string& path);

    static BindingConfig defaults();
    static bool loadFromFile(const std::string& path, BindingConfig& out);
    bool saveToFile(const std::string& path) const;
};

} // namespace undek::bindings
