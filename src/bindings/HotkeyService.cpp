#include "bindings/HotkeyService.h"

#include "app/ScriptRunner.h"
#include "app/SoundFx.h"
#include "bindings/HotkeyParser.h"
#include "bindings/HotkeyRegistry.h"
#include "bindings/MouseHotkeyWatcher.h"
#include "core/Cancel.h"
#include "script/BuiltinFunctions.h"

#include <atomic>
#include <filesystem>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace undek::bindings {
namespace {

constexpr int kStopHotkeyId = 0;
constexpr int kBindingIdBase = 1;

std::atomic<bool> g_running_script{false};
std::atomic<bool> g_pending_resume{false};
std::unordered_map<int, bool> g_mouse_was_down;

void notify(const HotkeyService::StatusCallback& cb, const std::string& msg) {
    if (cb) cb(msg);
}

std::string getExecutableDirectory() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return "";
    std::filesystem::path exePath(buffer);
    return exePath.parent_path().string();
#else
    return "";
#endif
}

std::string resolveScriptPath(const std::string& scriptPath) {
    if (std::filesystem::exists(scriptPath)) {
        return scriptPath;
    }
    std::string exeDir = getExecutableDirectory();
    if (!exeDir.empty()) {
        std::filesystem::path resolved = std::filesystem::path(exeDir) / scriptPath;
        if (std::filesystem::exists(resolved)) {
            return resolved.string();
        }
    }
    std::filesystem::path cwd = std::filesystem::current_path();
    std::filesystem::path resolved = cwd / ".." / ".." / scriptPath;
    resolved = std::filesystem::weakly_canonical(resolved);
    if (std::filesystem::exists(resolved)) {
        return resolved.string();
    }
    return scriptPath;
}

#ifdef _WIN32
bool modifiersMatch(unsigned int mods) {
    const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    if (((mods & MOD_CONTROL) != 0) != ctrl) return false;
    if (((mods & MOD_SHIFT) != 0) != shift) return false;
    if (((mods & MOD_ALT) != 0) != alt) return false;
    return true;
}
#endif

} // namespace

bool HotkeyService::load(const std::string& path) {
    config_path_ = path;
    BindingConfig loaded = BindingConfig::defaults();
    if (BindingConfig::loadFromFile(path, loaded)) {
        config_ = std::move(loaded);
        return true;
    }
    config_ = BindingConfig::defaults();
    return false;
}

bool HotkeyService::save(const std::string& path) {
    config_path_ = path;
    return config_.saveToFile(path);
}

HotkeyService::RegistryState HotkeyService::rebuildRegistry(std::string& error_out) {
    HotkeyRegistry::clear();
    g_mouse_was_down.clear();

    int next_id = kBindingIdBase;
    for (const auto& b : config_.bindings) {
        if (!b.enabled) {
            continue;
        }
        const ParsedHotkey hk = parseHotkey(b.hotkey);
        if (!hk.valid) {
            error_out = "Invalid hotkey: " + b.hotkey;
            return RegistryState::Failed;
        }
        RegisteredHotkey entry;
        entry.id = next_id++;
        entry.modifiers = hk.modifiers;
        entry.virtual_key = hk.virtual_key;
        entry.hotkey_spec = b.hotkey;
        entry.binding = b;
        HotkeyRegistry::add(std::move(entry));
    }

    if (!HotkeyRegistry::registerStopHotkey(config_.stop_hotkey, error_out)) {
        error_out = "Toggle hotkey failed: " + error_out;
        return RegistryState::Failed;
    }
    if (!HotkeyRegistry::registerAll(error_out)) return RegistryState::Partial;
    return RegistryState::Ok;
}

bool HotkeyService::registerToggleHotkey(void* hwnd) {
    if (!hwnd) return false;
    HotkeyRegistry::setWindow(hwnd);
    std::string err;
    if (!HotkeyRegistry::registerStopHotkey(config_.stop_hotkey, err)) {
        notify(on_status_, "[REGISTRY ERROR] Toggle hotkey failed: " + err);
        return false;
    }
    return true;
}

bool HotkeyService::start(void* hwnd) {
    std::string err;
    HotkeyRegistry::setWindow(hwnd);
    // A binding another app already owns shouldn't stop the rest of the set, so
    // only a rebuild with nothing usable in it is fatal.
    const RegistryState state = rebuildRegistry(err);
    if (state == RegistryState::Failed) {
        notify(on_status_, "Failed to start hotkeys: " + err);
        return false;
    }
    if (state == RegistryState::Partial)
        notify(on_status_, "[REGISTRY] Some hotkeys didn't register: " + err);

    std::vector<WatchedButton> watched;
    for (const auto& e : HotkeyRegistry::entries()) {
        if (isMouseKey(e.virtual_key)) watched.push_back({e.id, e.virtual_key, e.modifiers});
    }
    std::string mouse_err;
    if (!MouseHotkeyWatcher::start(watched, mouse_err))
        notify(on_status_, "[REGISTRY] Side buttons fall back to the frame poll: " + mouse_err);

    listening_ = true;
    notify(on_status_, "Macros enabled (hotkeys active)");
    return true;
}

void HotkeyService::stop() {
    MouseHotkeyWatcher::stop();
    if (!listening_) return;
    HotkeyRegistry::unregisterBindings();
    g_mouse_was_down.clear();
    listening_ = false;
    notify(on_status_, "Macros disabled (press " + config_.stop_hotkey + " to re-enable)");
}

void HotkeyService::shutdown() {
    cancel::requestShutdown();
    MouseHotkeyWatcher::stop();
    HotkeyRegistry::unregisterAll();
    g_mouse_was_down.clear();
    listening_ = false;
    if (worker_.joinable()) worker_.join();
}

bool HotkeyService::scriptRunning() {
    return g_running_script.load();
}

bool HotkeyService::tryStartScript() {
    bool expected = false;
    return g_running_script.compare_exchange_strong(expected, true);
}

void HotkeyService::endScript() {
    g_running_script = false;
}

void HotkeyService::runBindingAsync(const Binding& binding) {
    const std::string active_profile = config_.resolveProfileForWindow(binding.profile);
    if (worker_.joinable()) worker_.join();
    worker_ = std::thread([this, binding, active_profile]() {
        notify(on_status_, "Running: " + binding.script + " [" + binding.hotkey +
                               "] (profile: " + active_profile + ")");

        std::string err;
        std::string resolvedPath = resolveScriptPath(binding.script);
        std::string resolvedProfile = resolveScriptPath(active_profile);

        const std::string source = script::readFile(resolvedPath);
        if (source.empty()) {
            notify(on_status_, "[ERROR] Failed to read script: " + binding.script);
        } else {
            undek::LogCallback log_cb = [this](const std::string& msg) {
                notify(on_status_, "  " + msg);
            };
            // The macro's own folder, so a picture or include kept beside it is
            // found here too instead of only when the editor runs the script.
            const std::string script_dir =
                std::filesystem::path(resolvedPath).parent_path().string();
            int result = ScriptRunner::runSource(source, resolvedProfile, false, &err, log_cb,
                                                 nullptr, script_dir);
            if (result != 0) {
                notify(on_status_, "[ERROR] " + binding.script + ": " + err);
            } else {
                notify(on_status_, "[OK] Finished " + binding.script);
            }
        }
        g_pending_resume = true;
        g_running_script = false;
    });
}

void HotkeyService::handleHotkey(int hotkey_id) {
    if (hotkey_id == kStopHotkeyId) {
        if (g_running_script.load()) {
            notify(on_status_, "[HOTKEY] Aborting macro and disabling hotkeys");
            cancel::request();
            stop();
            soundfx::macrosDisabled();
            return;
        }
        if (listening_) {
            stop();
            soundfx::macrosDisabled();
        } else {
            if (start(HotkeyRegistry::window())) {
                soundfx::macrosEnabled();
            }
        }
        return;
    }

    if (!listening_ || HotkeyRegistry::macroHotkeysSuspended()) {
        return;
    }

    for (const auto& rb : HotkeyRegistry::entries()) {
        if (rb.id != hotkey_id) continue;

        if (!tryStartScript()) {
            notify(on_status_, "Macro already running");
            return;
        }

        HotkeyRegistry::suspendMacroHotkeys();
        runBindingAsync(rb.binding);
        return;
    }
}

void HotkeyService::poll() {
#ifdef _WIN32
    HWND hwnd = static_cast<HWND>(HotkeyRegistry::window());
    if (!hwnd) return;

    if (g_pending_resume.exchange(false)) {
        if (listening_) {
            HotkeyRegistry::resumeMacroHotkeys();
        }
    }

    if (MouseHotkeyWatcher::running()) {
        // The hook catches every press; the queue only decides when to act on one.
        const std::vector<int> presses = MouseHotkeyWatcher::takePressedIds();
        if (!listening_ || HotkeyRegistry::macroHotkeysSuspended()) return;
        for (int id : presses) {
            handleHotkey(id);
        }
        return;
    }

    if (!listening_) return;

    // Fallback for when the hook isn't available, so a quick tap can still slip
    // between frames here. The latch follows the button even while a macro runs,
    // otherwise the press after it gets swallowed.
    const bool suspended = HotkeyRegistry::macroHotkeysSuspended();
    for (const auto& rb : HotkeyRegistry::entries()) {
        if (!isMouseKey(rb.virtual_key)) continue;
        const bool down = (GetAsyncKeyState(static_cast<int>(rb.virtual_key)) & 0x8000) != 0 &&
                          modifiersMatch(rb.modifiers);
        bool& was_down = g_mouse_was_down[rb.id];
        const bool pressed = down && !was_down;
        was_down = down;
        if (pressed && !suspended) {
            handleHotkey(rb.id);
            break;
        }
    }
#endif
}

} // namespace undek::bindings
