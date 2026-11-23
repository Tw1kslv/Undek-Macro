#pragma once

#include "bindings/BindingConfig.h"

#include <functional>
#include <string>
#include <thread>

namespace undek::bindings {

class HotkeyService {
public:
    using StatusCallback = std::function<void(const std::string&)>;

    void setStatusCallback(StatusCallback cb) { on_status_ = std::move(cb); }

    bool load(const std::string& path);
    bool save(const std::string& path);

    BindingConfig& config() { return config_; }
    const BindingConfig& config() const { return config_; }

    bool registerToggleHotkey(void* hwnd);
    bool start(void* hwnd);
    void stop();
    void shutdown();
    bool isListening() const { return listening_; }

    void poll();
    void handleHotkey(int hotkey_id);

    static bool scriptRunning();
    static bool tryStartScript();
    static void endScript();

private:
    void runBindingAsync(const Binding& binding);
    // Some hotkeys are often already taken by another app, so a partial rebuild
    // is a state of its own rather than a failure with a logged excuse.
    enum class RegistryState { Ok, Partial, Failed };
    RegistryState rebuildRegistry(std::string& error_out);

    BindingConfig config_;
    std::string config_path_ = "bindings/default.json";
    bool listening_ = false;
    StatusCallback on_status_;
    std::thread worker_;
};

} // namespace undek::bindings
