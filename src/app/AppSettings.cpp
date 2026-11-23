#include "app/AppSettings.h"

#include "util/AtomicFile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace undek {

AppSettings& AppSettings::get() {
    static AppSettings instance;
    return instance;
}

bool AppSettings::load(const std::string& path) {
    std::ifstream file(path);
    if (!file) return false;

    nlohmann::json j;
    try {
        file >> j;
    } catch (...) {
        return false;
    }
    if (!j.is_object()) return false;

    auto getb = [&](const char* k, bool& v) {
        if (j.contains(k) && j[k].is_boolean()) v = j[k].get<bool>();
    };
    // An empty hotkey can't be registered, so it would leave the app looking
    // enabled with a hotkey that never fires.
    auto get_hotkey = [&](const char* k, std::string& v) {
        if (!j.contains(k) || !j[k].is_string()) return;
        const std::string value = j[k].get<std::string>();
        if (value.find_first_not_of(" \t\r\n") == std::string::npos) return;
        v = value;
    };

    getb("start_minimized", start_minimized);
    getb("minimize_to_tray", minimize_to_tray);
    getb("close_to_tray", close_to_tray);
    get_hotkey("show_hotkey", show_hotkey);
    get_hotkey("stealth_hotkey", stealth_hotkey);
    get_hotkey("picker_hotkey", picker_hotkey);
    get_hotkey("pause_hotkey", pause_hotkey);
    getb("toggle_sound", toggle_sound);
    if (j.contains("toggle_sound_volume") && j["toggle_sound_volume"].is_number()) {
        // The panel's slider is 0-100; a hand-edited file could say anything.
        toggle_sound_volume = std::clamp(j["toggle_sound_volume"].get<double>(), 0.0, 100.0);
    }
    return true;
}

bool AppSettings::save(const std::string& path) const {
    nlohmann::json j;
    j["start_minimized"] = start_minimized;
    j["minimize_to_tray"] = minimize_to_tray;
    j["close_to_tray"] = close_to_tray;
    j["show_hotkey"] = show_hotkey;
    j["stealth_hotkey"] = stealth_hotkey;
    j["picker_hotkey"] = picker_hotkey;
    j["pause_hotkey"] = pause_hotkey;
    j["toggle_sound"] = toggle_sound;
    j["toggle_sound_volume"] = toggle_sound_volume;

    return writeFileAtomically(path, j.dump(2) + "\n");
}

} // namespace undek
