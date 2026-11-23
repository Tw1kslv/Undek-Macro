#pragma once

#include <string>

namespace undek {

struct AppSettings {
    bool start_minimized = false;
    bool minimize_to_tray = true;
    bool close_to_tray = false;
    std::string show_hotkey = "Ctrl+Shift+H";
    std::string stealth_hotkey = "Ctrl+Shift+X";
    std::string picker_hotkey = "Ctrl+Shift+C";
    std::string pause_hotkey = "Ctrl+Shift+P";
    bool toggle_sound = true;
    double toggle_sound_volume = 80.0;

    static AppSettings& get();
    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

} // namespace undek
