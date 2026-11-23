#include "bindings/BindingConfig.h"

#include "screen/ScreenSampler.h"
#include "util/AtomicFile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

#include <fstream>

namespace undek::bindings {

BindingConfig BindingConfig::defaults() {
    BindingConfig cfg;
    return cfg;
}

namespace {

// Same idea as the editor's path compare: slashes one way, case folded.
std::string normalizedScriptPath(std::string p) {
    for (char& c : p) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (c == '\\') c = '/';
    }
    while (p.size() > 1 && p.front() == '.' && p[1] == '/')
        p.erase(0, 2);
    return p;
}

bool sameScript(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    return normalizedScriptPath(a) == normalizedScriptPath(b);
}

} // namespace

int BindingConfig::renameScript(const std::string& from, const std::string& to) {
    if (from.empty() || to.empty()) return 0;
    int changed = 0;
    for (auto& b : bindings) {
        if (!sameScript(b.script, from)) continue;
        b.script = to;
        ++changed;
    }
    return changed;
}

int BindingConfig::forgetScript(const std::string& path) {
    if (path.empty()) return 0;
    const size_t before = bindings.size();
    bindings.erase(std::remove_if(bindings.begin(), bindings.end(),
                                  [&path](const Binding& b) { return sameScript(b.script, path); }),
                   bindings.end());
    return static_cast<int>(before - bindings.size());
}

std::string BindingConfig::resolveProfileForWindow(const std::string& fallback_profile) const {
    for (const auto& rule : window_profiles) {
        if (!rule.window_title.empty() && !rule.profile.empty() &&
            WindowInfo::titleContains(rule.window_title)) {
            return rule.profile;
        }
    }
    return fallback_profile.empty() ? default_profile : fallback_profile;
}

bool BindingConfig::loadFromFile(const std::string& path, BindingConfig& out) {
    std::ifstream file(path);
    if (!file) return false;

    nlohmann::json j;
    try {
        file >> j;
    } catch (...) {
        return false;
    }
    if (!j.is_object()) return false;

    auto gets = [](const nlohmann::json& obj, const char* k, std::string& v) {
        if (obj.contains(k) && obj[k].is_string()) v = obj[k].get<std::string>();
    };

    // Building into a copy keeps a reload all-or-nothing, and clearing the
    // lists stops a second load from appending duplicates.
    BindingConfig parsed = out;
    parsed.window_profiles.clear();
    parsed.bindings.clear();

    gets(j, "stop_hotkey", parsed.stop_hotkey);
    gets(j, "default_profile", parsed.default_profile);

    if (j.contains("window_profiles") && j["window_profiles"].is_array()) {
        for (const auto& e : j["window_profiles"]) {
            if (!e.is_object()) continue;
            WindowProfileRule rule;
            gets(e, "window_title", rule.window_title);
            gets(e, "window_profile", rule.profile);
            if (rule.profile.empty()) gets(e, "profile", rule.profile);
            if (!rule.window_title.empty() && !rule.profile.empty()) {
                parsed.window_profiles.push_back(rule);
            }
        }
    }

    if (j.contains("bindings") && j["bindings"].is_array()) {
        for (const auto& e : j["bindings"]) {
            if (!e.is_object()) continue;
            Binding b;
            gets(e, "hotkey", b.hotkey);
            gets(e, "script", b.script);
            gets(e, "profile", b.profile);
            if (e.contains("enabled") && e["enabled"].is_boolean()) {
                b.enabled = e["enabled"].get<bool>();
            }
            if (!b.hotkey.empty() && !b.script.empty()) {
                parsed.bindings.push_back(b);
            }
        }
    }

    for (auto& b : parsed.bindings) {
        if (b.profile.empty()) b.profile = parsed.default_profile;
    }

    out = std::move(parsed);
    return true;
}

bool BindingConfig::saveToFile(const std::string& path) const {
    auto normalizePath = [](std::string p) {
        for (char& c : p)
            if (c == '\\') c = '/';
        return p;
    };

    nlohmann::json j;
    j["stop_hotkey"] = stop_hotkey;
    j["default_profile"] = default_profile;

    if (!window_profiles.empty()) {
        nlohmann::json rules = nlohmann::json::array();
        for (const auto& r : window_profiles) {
            rules.push_back({
                {"window_title", r.window_title},
                {"window_profile", normalizePath(r.profile)},
            });
        }
        j["window_profiles"] = std::move(rules);
    }

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& b : bindings) {
        arr.push_back({
            {"hotkey", b.hotkey},
            {"script", normalizePath(b.script)},
            {"profile", normalizePath(b.profile)},
            {"enabled", b.enabled},
        });
    }
    j["bindings"] = std::move(arr);

    return writeFileAtomically(path, j.dump(2) + "\n");
}

} // namespace undek::bindings
