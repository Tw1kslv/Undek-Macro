#include "bindings/HotkeyHost.h"

#include "app/AppSettings.h"
#include "app/ScriptRunner.h"
#include "app/SoundFx.h"
#include "bindings/HotkeyParser.h"
#include "bindings/HotkeyRegistry.h"
#include "bindings/MouseHotkeyWatcher.h"
#include "core/Cancel.h"

#include <atomic>
#include <iostream>
#include <thread>

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
namespace {

constexpr int kStopHotkeyId = 0;
constexpr int kBindingIdBase = 1;

std::atomic<bool> g_running_script{false};
std::atomic<bool> g_macros_enabled{true};
std::thread g_script_thread;

void runBindingScript(RegisteredHotkey entry) {
    HotkeyRegistry::suspendMacroHotkeys();
    ScriptRunner::run(entry.binding.script, entry.binding.profile, false);
    HotkeyRegistry::resumeMacroHotkeys();
    g_running_script = false;
}

void onHotkeyTriggered(int hotkey_id) {
    if (hotkey_id == kStopHotkeyId) {
        if (g_running_script.load()) {
            cancel::request();
            std::cout << "[stop] Cancelling the running macro\n";
            return;
        }
        if (g_macros_enabled.load()) {
            HotkeyRegistry::suspendMacroHotkeys();
            g_macros_enabled = false;
            std::cout << "[toggle] Macros disabled (press toggle hotkey again to re-enable)\n";
            undek::soundfx::macrosDisabled();
        } else {
            HotkeyRegistry::resumeMacroHotkeys();
            g_macros_enabled = true;
            std::cout << "[toggle] Macros enabled\n";
            undek::soundfx::macrosEnabled();
        }
        return;
    }

    if (!g_macros_enabled.load() || HotkeyRegistry::macroHotkeysSuspended()) {
        return;
    }

    for (const auto& rb : HotkeyRegistry::entries()) {
        if (rb.id != hotkey_id) continue;

        bool expected = false;
        if (!g_running_script.compare_exchange_strong(expected, true)) {
            std::cout << "[bind] Already running, ignored: " << rb.hotkey_spec << "\n";
            return;
        }

        std::cout << "[bind] " << rb.hotkey_spec << " -> " << rb.binding.script << "\n";

        // Run it on a worker so the message pump keeps turning; otherwise the
        // stop hotkey sits in the queue until the script has already finished.
        if (g_script_thread.joinable()) g_script_thread.join();
        g_script_thread = std::thread(runBindingScript, rb);
        return;
    }
}

} // namespace

HotkeyHost::HotkeyHost(BindingConfig config) : config_(std::move(config)) {}

int HotkeyHost::run() {
#ifdef _WIN32
    AppSettings::get().load("settings.json");
    HotkeyRegistry::clear();

    std::string err;
    if (!HotkeyRegistry::registerStopHotkey(config_.stop_hotkey, err)) {
        std::cerr << "Failed to register stop hotkey (" << config_.stop_hotkey << "): " << err
                  << "\n";
        return 1;
    }

    int next_id = kBindingIdBase;
    for (const auto& b : config_.bindings) {
        const ParsedHotkey hk = parseHotkey(b.hotkey);
        if (!hk.valid) {
            std::cerr << "Failed to parse hotkey: " << b.hotkey << "\n";
            HotkeyRegistry::unregisterAll();
            return 1;
        }

        RegisteredHotkey entry;
        entry.id = next_id++;
        entry.modifiers = hk.modifiers;
        entry.virtual_key = hk.virtual_key;
        entry.hotkey_spec = b.hotkey;
        entry.binding = b;
        HotkeyRegistry::add(std::move(entry));
    }

    if (!HotkeyRegistry::registerAll(err)) {
        std::cerr << "Failed to register bindings: " << err << "\n";
        HotkeyRegistry::unregisterAll();
        return 1;
    }

    std::vector<WatchedButton> watched;
    for (const auto& e : HotkeyRegistry::entries())
        if (isMouseKey(e.virtual_key)) watched.push_back({e.id, e.virtual_key, e.modifiers});

    std::string mouse_err;
    if (!MouseHotkeyWatcher::start(watched, mouse_err))
        std::cerr << "Side buttons unavailable: " << mouse_err << "\n";

    for (const auto& rb : HotkeyRegistry::entries()) {
        std::cout << "  " << rb.hotkey_spec << " -> " << rb.binding.script << "\n";
    }

    std::cout << "Listening... Press " << config_.stop_hotkey << " to toggle macros on/off.\n";
    std::cout << "(Macro hotkeys are disabled while a script is running.)\n";

    MSG msg{};
    HANDLE wake = static_cast<HANDLE>(MouseHotkeyWatcher::wakeHandle());
    bool quit = false;
    while (!quit) {
        // Wake for the hook's queue as well, not just window messages.
        MsgWaitForMultipleObjects(wake ? 1 : 0, wake ? &wake : nullptr, FALSE, 50, QS_ALLINPUT);

        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_HOTKEY) {
                onHotkeyTriggered(static_cast<int>(msg.wParam));
            } else if (msg.message == WM_QUIT) {
                quit = true;
                break;
            } else {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        if (quit) break;

        for (int id : MouseHotkeyWatcher::takePressedIds()) {
            onHotkeyTriggered(id);
        }
    }

    MouseHotkeyWatcher::stop();

    if (g_script_thread.joinable()) {
        cancel::request();
        g_script_thread.join();
    }

    HotkeyRegistry::unregisterAll();
    return static_cast<int>(msg.wParam);
#else
    std::cerr << "Hotkey listening is only supported on Windows.\n";
    (void)config_;
    return 1;
#endif
}

} // namespace undek::bindings
