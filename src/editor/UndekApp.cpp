#include "editor/UndekApp.h"

#include "app/AppPaths.h"
#include "app/AppSettings.h"
#include "app/ScriptRunner.h"
#include "app/SoundFx.h"
#include "bindings/HotkeyParser.h"
#include "bindings/HotkeyRegistry.h"
#include "core/Cancel.h"
#include "core/Pause.h"
#include "core/RunSlot.h"
#include "input/InputRecorder.h"
#include "screen/ScreenSampler.h"
#include "script/BuiltinFunctions.h"
#include "script/Checker.h"
#include "script/DebugController.h"
#include "script/Lexer.h"
#include "script/Parser.h"
#include "script/Vm.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <shellapi.h>
#include <windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <utility>

namespace undek::editor {
namespace {

std::string normScriptPath(std::string p) {
    for (char& c : p) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (c == '\\') c = '/';
    }
    return p;
}

bool sameScriptPath(const std::string& a, const std::string& b) {
    const std::string na = normScriptPath(a);
    const std::string nb = normScriptPath(b);
    if (na == nb) return true;

    // Only match on the bare filename when one side is an unqualified name,
    // so macros/AutoClicker.undek and examples/AutoClicker.undek stay apart.
    const bool a_plain = na.find('/') == std::string::npos;
    const bool b_plain = nb.find('/') == std::string::npos;
    if (!a_plain && !b_plain) return false;

    const auto fname = [](const std::string& p) {
        const size_t s = p.rfind('/');
        return (s == std::string::npos) ? p : p.substr(s + 1);
    };
    return fname(na) == fname(nb);
}

// The few buttons that start something get a colour of their own, so the eye
// finds them without having to read every label.
void pushButtonColor(float r, float g, float b) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(r, g, b, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(r * 1.18f, g * 1.18f, b * 1.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(r * 0.82f, g * 0.82f, b * 0.82f, 1.0f));
}

void popButtonColor() {
    ImGui::PopStyleColor(3);
}

// Hint text that runs long goes to the next line instead of being cut off at
// the right edge of the window.
void textDisabledWrapped(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

#ifdef _WIN32
// ShellExecuteA answers with a value of 32 or less when it couldn't start
// anything, which is the difference between "it opened" and a menu item that
// looks dead.
bool shellOpen(const std::string& target, const std::string& args = std::string()) {
    const HINSTANCE result =
        ShellExecuteA(nullptr, "open", target.c_str(), args.empty() ? nullptr : args.c_str(),
                      nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}
#else
bool shellOpen(const std::string&, const std::string& = std::string()) {
    return false;
}
#endif

// Plenty of Windows machines have no program registered for .md, so the shell
// can refuse a file that is sitting right there. Notepad is always there.
bool openDoc(const std::string& path) {
    if (path.empty() || !std::filesystem::exists(path)) return false;
    if (shellOpen(path)) return true;
    return shellOpen("notepad.exe", "\"" + path + "\"");
}

bool isKeywordWord(const std::string& w) {
    static const std::unordered_set<std::string> kws = {
        "if",  "else",    "loop",    "while", "repeat_until", "break",
        "var", "profile", "include", "not",   "tolerance"};
    return kws.count(w) > 0;
}

bool isCommandWord(const std::string& w) {
    static const std::unordered_set<std::string> cmds = {
        "Click", "PressKey",   "KeyDown", "KeyUp",       "MouseDown", "MouseUp",
        "Wait",  "RandomWait", "PixelAt", "WindowTitle", "Point",     "Log"};
    return cmds.count(w) > 0;
}

void buildLineTokens(const std::string& line, std::vector<EditorLineToken>& out) {
    out.clear();
    const ImU32 col_default = IM_COL32(236, 240, 248, 255);
    const ImU32 col_comment = IM_COL32(128, 134, 148, 255);
    const ImU32 col_keyword = IM_COL32(198, 145, 255, 255);
    const ImU32 col_command = IM_COL32(110, 200, 255, 255);
    const ImU32 col_string = IM_COL32(158, 226, 142, 255);
    const ImU32 col_number = IM_COL32(255, 186, 115, 255);

    auto add = [&](size_t begin, size_t end, ImU32 color) {
        EditorLineToken tok;
        tok.text = line.substr(begin, end - begin);
        tok.color = color;
        tok.width = ImGui::CalcTextSize(tok.text.c_str()).x;
        out.push_back(std::move(tok));
    };

    size_t i = 0;
    const size_t n = line.size();

    while (i < n) {
        if (line[i] == '/' && i + 1 < n && line[i + 1] == '/') {
            add(i, n, col_comment);
            return;
        }

        if (line[i] == '"') {
            size_t j = i + 1;
            while (j < n && line[j] != '"')
                ++j;
            if (j < n) ++j;
            add(i, j, col_string);
            i = j;
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(line[i]))) {
            size_t j = i;
            while (j < n && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '.'))
                ++j;
            add(i, j, col_number);
            i = j;
            continue;
        }

        if (std::isalpha(static_cast<unsigned char>(line[i])) || line[i] == '_') {
            size_t j = i;
            while (j < n && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '_'))
                ++j;
            const std::string word = line.substr(i, j - i);
            ImU32 col = col_default;
            if (isKeywordWord(word))
                col = col_keyword;
            else if (isCommandWord(word))
                col = col_command;
            else if (word == "true" || word == "false")
                col = col_number;
            add(i, j, col);
            i = j;
            continue;
        }

        size_t j = i + 1;
        while (j < n && !std::isalnum(static_cast<unsigned char>(line[j])) && line[j] != '_' &&
               line[j] != '"' && line[j] != '/') {
            ++j;
        }
        add(i, j, col_default);
        i = j;
    }
}

} // namespace

UndekApp::UndekApp() :
    rng_(HumanizationProfile::defaults()), paths_(HumanizationProfile::defaults(), rng_) {
    bindings_path_ = AppPaths::bindingsFile();
    profile_path_ = AppPaths::relativeToRoot(AppPaths::resolve("profiles/default.json"));

    library_.refresh();
    hotkeys_.load(bindings_path_);
    std::strncpy(stop_hk_buf_, hotkeys_.config().stop_hotkey.c_str(), sizeof(stop_hk_buf_) - 1);
    std::strncpy(profile_buf_, profile_path_.c_str(), sizeof(profile_buf_) - 1);

    const auto& s = AppSettings::get();
    std::strncpy(show_hk_buf_, s.show_hotkey.c_str(), sizeof(show_hk_buf_) - 1);
    std::strncpy(stealth_hk_buf_, s.stealth_hotkey.c_str(), sizeof(stealth_hk_buf_) - 1);
    std::strncpy(picker_hk_buf_, s.picker_hotkey.c_str(), sizeof(picker_hk_buf_) - 1);
    std::strncpy(pause_hk_buf_, s.pause_hotkey.c_str(), sizeof(pause_hk_buf_) - 1);

    hotkeys_.setStatusCallback([this](const std::string& msg) { appendLog(msg, true); });

    if (!library_.entries().empty()) {
        sel_macro_ = 0;
        loadScript(library_.entries()[0].path);
    } else {
        script_path_ = "macros/new_macro.undek";
        script_source_ = MacroLibrary::defaultTemplate();
        dirty_ = true;
    }
    setProfilePath(profile_path_);
    hotkeys_.registerToggleHotkey(bindings::HotkeyRegistry::window());
    InputRecorder::setHostWindow(bindings::HotkeyRegistry::window());
}

void UndekApp::appendLog(const std::string& msg, bool update_status) {
    std::lock_guard<std::mutex> lock(log_mtx_);
    if (update_status) {
        status_msg_ = msg;
    }
    logs_.push_back(msg);
    ++log_version_;
    if (logs_.size() > 250) {
        logs_.pop_front();
    }
}

void UndekApp::clearLogs() {
    std::lock_guard<std::mutex> lock(log_mtx_);
    logs_.clear();
    ++log_version_;
}

void UndekApp::rebuildEditorLayout() {
    editor_layout_source_ = script_source_;
    editor_layout_.clear();

    std::string line;
    for (size_t i = 0; i <= script_source_.size(); ++i) {
        if (i == script_source_.size() || script_source_[i] == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            editor_layout_.emplace_back();
            buildLineTokens(line, editor_layout_.back());
            line.clear();
        } else {
            line.push_back(script_source_[i]);
        }
    }
}

void UndekApp::setStatus(const std::string& msg) {
    std::lock_guard<std::mutex> lock(log_mtx_);
    status_msg_ = msg;
}

void UndekApp::startDetect(int target) {
    detect_target_ = target;
    detect_start_ = std::chrono::steady_clock::now();
    setStatus("Press any key or mouse button to bind (Esc to cancel)...");
}

void UndekApp::pollDetectBind() {
    if (detect_target_ == 0) return;

    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - detect_start_)
                                .count();
    if (elapsed_ms < 220) return;

    bool cancelled = false;
    const std::string key = bindings::pollPressedHotkey(cancelled);
    if (cancelled) {
        detect_target_ = 0;
        setStatus("Bind detection cancelled");
        return;
    }
    if (key.empty()) return;

    const int tgt = detect_target_;
    detect_target_ = 0;

    if (tgt == 1) {
        std::strncpy(bind_hk_buf_, key.c_str(), sizeof(bind_hk_buf_) - 1);
        if (sel_macro_ >= 0 && sel_macro_ < static_cast<int>(library_.entries().size())) {
            addBindingForMacro(library_.entries()[sel_macro_].path);
        }
    } else if (tgt == 2) {
        std::strncpy(stop_hk_buf_, key.c_str(), sizeof(stop_hk_buf_) - 1);
        setStatus("Detected toggle hotkey: " + key);
    } else if (tgt == 3) {
        std::strncpy(show_hk_buf_, key.c_str(), sizeof(show_hk_buf_) - 1);
        setStatus("Detected show/hide hotkey: " + key);
    } else if (tgt == 4) {
        std::strncpy(stealth_hk_buf_, key.c_str(), sizeof(stealth_hk_buf_) - 1);
        setStatus("Detected stealth hide hotkey: " + key);
    } else if (tgt == 5) {
        std::strncpy(picker_hk_buf_, key.c_str(), sizeof(picker_hk_buf_) - 1);
        setStatus("Detected picker hotkey: " + key);
    } else if (tgt == 6) {
        std::strncpy(pause_hk_buf_, key.c_str(), sizeof(pause_hk_buf_) - 1);
        setStatus("Detected pause hotkey: " + key);
    } else if (tgt >= 100) {
        const int idx = tgt - 100;
        auto& cfg = hotkeys_.config();
        if (idx >= 0 && idx < static_cast<int>(cfg.bindings.size())) {
            const std::string script = cfg.bindings[idx].script;
            cfg.bindings[idx].hotkey = key;
            for (int j = static_cast<int>(cfg.bindings.size()) - 1; j >= 0; --j) {
                if (j != idx && bindings::hotkeysEquivalent(cfg.bindings[j].hotkey, key)) {
                    cfg.bindings.erase(cfg.bindings.begin() + j);
                }
            }
            hotkeys_.save(bindings_path_);
            refreshBindingsIfListening();
            setStatus("Bound " + key + " -> " + script);
        }
    }
}

void UndekApp::captureCursorAndPixel() {
    const Vec2 pos = backend_.cursorPosition();
    const int x = static_cast<int>(pos.x);
    const int y = static_cast<int>(pos.y);
    const auto sampled = ScreenSampler::pixelColor(x, y);
    if (!sampled) {
        // A colour that was never read would end up in the script as a real one.
        setStatus("Couldn't read the pixel at (" + std::to_string(x) + ", " + std::to_string(y) +
                  ")");
        return;
    }
    const unsigned int col = *sampled;

    std::ostringstream hex;
    hex << "0x" << std::uppercase << std::hex << std::setw(6) << std::setfill('0') << col;

    std::ostringstream snippet;
    snippet << "\n// Picked (" << x << ", " << y << ") color " << hex.str() << "\n"
            << "var spot = Point(" << x << ", " << y << ")\n"
            << "// if PixelAt(" << x << ", " << y << ") == " << hex.str()
            << " tolerance 15 { Click(spot, radius=4, humanize=true) }\n";

    script_source_ += snippet.str();
    dirty_ = true;
    ImGui::SetClipboardText(snippet.str().c_str());
    appendLog("[PICKER] Point(" + std::to_string(x) + ", " + std::to_string(y) + ") color " +
                  hex.str() + " (added to script & clipboard)",
              true);
}

void UndekApp::loadScript(const std::string& path) {
    script_path_ = path;
    script_source_ = script::readFile(AppPaths::resolve(path));
    dirty_ = false;
    error_line_ = -1;
    debug_line_ = -1;
    breakpoints_.clear();
    setStatus("Loaded " + path);
}

void UndekApp::setProfilePath(const std::string& path) {
    profile_path_ = AppPaths::relativeToRoot(AppPaths::resolve(path));
    // The runner validates a profile before it builds a VM, but the editor's own
    // preview and sampling read profile_ straight, so a broken file used to get
    // in through here. Loading into a copy keeps the one in use untouched.
    HumanizationProfile loaded = profile_;
    std::string error;
    if (!HumanizationProfile::loadFromFile(AppPaths::resolve(profile_path_), loaded)) {
        setStatus("Profile not loaded: " + profile_path_);
        return;
    }
    if (!loaded.validate(error)) {
        setStatus("Profile not loaded: " + error);
        return;
    }
    profile_ = loaded;
    std::strncpy(req_win_buf_, profile_.require_window.c_str(), sizeof(req_win_buf_) - 1);
    std::strncpy(backend_buf_, profile_.input_backend.c_str(), sizeof(backend_buf_) - 1);
    rng_.setProfile(profile_);
    paths_.setProfile(profile_);
    setStatus("Profile loaded: " + profile_path_);
}

bool UndekApp::saveCurrentScript() {
    if (script_path_.empty()) script_path_ = "macros/untitled.undek";
    if (!library_.save(script_path_, script_source_)) {
        setStatus("Save failed: " + script_path_);
        return false;
    }
    dirty_ = false;
    library_.refresh();
    setStatus("Saved " + script_path_);
    return true;
}

void UndekApp::runScript(bool dry_run) {
    if (!bindings::HotkeyService::tryStartScript()) {
        setStatus("A script is already running");
        return;
    }
    running_ = true;

    const std::string src = script_source_;
    const std::string active_prof = hotkeys_.config().resolveProfileForWindow(profile_path_);
    const std::string prof = AppPaths::resolve(active_prof);
    const std::string label = script_path_.empty() ? "editor script" : script_path_;
    // Pictures are looked for beside the macro, which is where the check looks
    // for them too.
    std::string dir;
    if (!script_path_.empty())
        dir = std::filesystem::path(AppPaths::resolve(script_path_)).parent_path().string();

    appendLog(std::string(dry_run ? "[DRY-RUN] " : "[RUN] ") + label + " (profile: " + active_prof +
                  ")",
              true);
    error_line_ = -1;

    if (worker_.joinable()) worker_.join();
    worker_ = std::thread([this, src, prof, dry_run, dir]() {
        std::string err;
        undek::LogCallback log_cb = [this](const std::string& msg) {
            appendLog("  " + msg, false);
        };
        const int code = ScriptRunner::runSource(src, prof, dry_run, &err, log_cb, nullptr, dir);
        if (code == 0) {
            appendLog(dry_run ? "[OK] Dry-run complete" : "[OK] Run complete", true);
        } else {
            if (err.rfind("line ", 0) == 0) {
                error_line_ = std::atoi(err.c_str() + 5);
            }
            appendLog("[ERROR] " + err, true);
        }
        running_ = false;
        bindings::HotkeyService::endScript();
    });
}

void UndekApp::runDebug() {
    if (!bindings::HotkeyService::tryStartScript()) {
        setStatus("A script is already running");
        return;
    }
    running_ = true;

    const std::string src = script_source_;
    const std::string active_prof = hotkeys_.config().resolveProfileForWindow(profile_path_);
    const std::string prof = AppPaths::resolve(active_prof);
    const std::string label = script_path_.empty() ? "editor script" : script_path_;
    std::string dir;
    if (!script_path_.empty())
        dir = std::filesystem::path(AppPaths::resolve(script_path_)).parent_path().string();

    appendLog("[DEBUG] " + label + " (profile: " + active_prof + ")", true);
    error_line_ = -1;
    debug_line_ = -1;

    dbg_ = std::make_shared<script::DebugController>(true);
    dbg_->setBreakpoints(breakpoints_);
    const auto dbg = dbg_;
    show_debugger_ = true;

    if (worker_.joinable()) worker_.join();
    worker_ = std::thread([this, src, prof, dbg, dir]() {
        std::string err;
        undek::LogCallback log_cb = [this](const std::string& msg) {
            appendLog("  " + msg, false);
        };
        const int code = ScriptRunner::runSource(src, prof, false, &err, log_cb, dbg.get(), dir);
        if (code == 0) {
            appendLog("[OK] Debug run complete", true);
        } else {
            if (err.rfind("line ", 0) == 0) {
                error_line_ = std::atoi(err.c_str() + 5);
            }
            appendLog("[ERROR] " + err, true);
        }
        debug_line_ = -1;
        running_ = false;
        bindings::HotkeyService::endScript();
    });
}

void UndekApp::checkCurrentScript() {
    std::string base_dir;
    if (!script_path_.empty()) {
        base_dir = std::filesystem::path(AppPaths::resolve(script_path_)).parent_path().string();
    }
    const auto problems = script::checkScript(script_source_, base_dir);
    if (problems.empty()) {
        appendLog("[check] nothing to report", true);
        error_line_.store(-1);
        return;
    }

    for (const auto& p : problems) {
        appendLog("[check] line " + std::to_string(p.line) + ": " + p.message, false);
    }
    // Put the cursor on the first one, since that's the one to fix next.
    error_line_.store(problems.front().line);
    setStatus("Check found " + std::to_string(problems.size()) + " problem(s)");
}

void UndekApp::togglePause() {
    if (!running_.load()) {
        // Anything still set has outlived its run, so it's cleared rather than
        // waiting to pause the next one.
        runs::clearPause();
        setStatus("Nothing is running to pause");
        return;
    }
    if (pause::pending()) {
        runs::clearPause();
        setStatus("Resumed");
        return;
    }
    if (!runs::requestPause()) {
        // running_ lags the run by the tail of the worker; by now the slot is
        // free and a pause would belong to nothing.
        setStatus("Nothing is running to pause");
        return;
    }
    setStatus("Paused at the next step - press the pause hotkey again to carry on");
}

void UndekApp::stopExecution() {
    if (dbg_) dbg_->stop();
    cancel::request();
}

void UndekApp::toggleBreakpoint(int line) {
    if (line < 1) return;
    if (!breakpoints_.erase(line)) breakpoints_.insert(line);
    if (dbg_) dbg_->setBreakpoints(breakpoints_);
}

void UndekApp::dryRunPathPreview() {
    if (!pv_init_) {
        pv_curve_ = static_cast<float>(profile_.bezier_curve_strength_max);
        pv_wobble_ = static_cast<float>(profile_.path_wobble_px);
        pv_overshoot_ = static_cast<float>(profile_.overshoot_probability);
        pv_steps_ = profile_.steps_per_100px;
        pv_init_ = true;
    }

    HumanizationProfile pv = profile_;
    pv.bezier_curve_strength_min = pv_curve_ * 0.7f;
    pv.bezier_curve_strength_max = pv_curve_;
    pv.path_wobble_px = pv_wobble_;
    pv.overshoot_probability = pv_overshoot_;
    pv.steps_per_100px = pv_steps_;

    const undek::Vec2 from = (prev_sx_ != 0.0f || prev_sy_ != 0.0f)
                                 ? undek::Vec2{prev_sx_, prev_sy_}
                                 : backend_.cursorPosition();
    const undek::Vec2 to = (prev_tx_ > 0.0f || prev_ty_ > 0.0f)
                               ? undek::Vec2{prev_tx_, prev_ty_}
                               : undek::Vec2{from.x + 200, from.y + 120};

    paths_.setProfile(pv);
    preview_steps_ = paths_.generatePath(from, to, &preview_c1_, &preview_c2_);
    show_preview_ = true;
    double total = 0.0;
    for (const auto& s : preview_steps_)
        total += s.delay_ms;
    setStatus("Path preview: " + std::to_string(preview_steps_.size()) + " steps, ~" +
              std::to_string(static_cast<int>(total)) + " ms");
}

void UndekApp::toggleMacros() {
    if (macros_on_) {
        hotkeys_.stop();
        macros_on_ = false;
        soundfx::macrosDisabled();
    } else {
        void* hwnd = bindings::HotkeyRegistry::window();
        if (!hwnd) {
            setStatus("Window not ready for hotkeys");
            return;
        }
        hotkeys_.config().stop_hotkey = stop_hk_buf_;
        hotkeys_.save(bindings_path_);
        if (hotkeys_.start(hwnd)) {
            macros_on_ = true;
            soundfx::macrosEnabled();
        }
    }
}

void UndekApp::refreshBindingsIfListening() {
    if (!macros_on_) return;
    hotkeys_.stop();
    hotkeys_.start(bindings::HotkeyRegistry::window());
}

void UndekApp::createNewMacro() {
    const std::string path = library_.createNew(new_name_buf_);
    loadScript(path);
    for (size_t i = 0; i < library_.entries().size(); ++i) {
        if (library_.entries()[i].path == path) {
            sel_macro_ = static_cast<int>(i);
            break;
        }
    }
}

void UndekApp::requestNewMacro() {
    if (dirty_) {
        pending_new_ = true;
        pending_sel_ = -1;
        show_unsaved_ = true;
        return;
    }
    createNewMacro();
}

void UndekApp::completePendingAction() {
    const bool wanted_new = pending_new_;
    const int idx = pending_sel_;
    pending_new_ = false;
    pending_sel_ = -1;
    if (wanted_new) {
        createNewMacro();
    } else if (idx >= 0 && idx < static_cast<int>(library_.entries().size())) {
        doSelectMacro(idx);
    }
}

void UndekApp::deleteMacro() {
    if (sel_macro_ < 0 || sel_macro_ >= static_cast<int>(library_.entries().size())) return;
    const std::string path = library_.entries()[sel_macro_].path;
    if (!library_.deleteMacro(path)) {
        setStatus("Failed to delete macro");
        return;
    }

    auto& cfg = hotkeys_.config();
    const bool unbound = cfg.forgetScript(path) > 0;
    if (unbound) {
        hotkeys_.save(bindings_path_);
        refreshBindingsIfListening();
    }

    setStatus(std::string("Deleted macro") + (unbound ? " (bindings removed)" : ""));
    sel_macro_ = -1;
    // Only clear the editor if it was showing the file that just went away.
    if (sameScriptPath(script_path_, path)) {
        script_path_ = "";
        script_source_ = "";
        dirty_ = false;
    }
}

void UndekApp::duplicateMacro() {
    if (sel_macro_ < 0 || sel_macro_ >= static_cast<int>(library_.entries().size())) return;
    const auto& e = library_.entries()[sel_macro_];
    const std::string newPath = library_.duplicate(e.path, e.name + "_copy");
    if (!newPath.empty()) {
        loadScript(newPath);
        for (size_t i = 0; i < library_.entries().size(); ++i) {
            if (library_.entries()[i].path == newPath) {
                sel_macro_ = static_cast<int>(i);
                break;
            }
        }
        setStatus("Duplicated macro");
    }
}

void UndekApp::renameMacro() {
    if (rename_idx_ < 0 || rename_idx_ >= static_cast<int>(library_.entries().size())) return;
    const std::string oldPath = library_.entries()[rename_idx_].path;
    const std::string newPath = library_.rename(oldPath, rename_buf_);
    if (newPath.empty()) {
        setStatus("Failed to rename macro");
        return;
    }

    if (sameScriptPath(script_path_, oldPath)) {
        script_path_ = newPath;
    }

    auto& cfg = hotkeys_.config();
    const bool touched = cfg.renameScript(oldPath, newPath) > 0;
    if (touched) {
        hotkeys_.save(bindings_path_);
        refreshBindingsIfListening();
    }

    for (size_t i = 0; i < library_.entries().size(); ++i) {
        if (normScriptPath(library_.entries()[i].path) == normScriptPath(newPath)) {
            sel_macro_ = static_cast<int>(i);
            break;
        }
    }

    setStatus(std::string("Renamed macro") + (touched ? " (bindings updated)" : ""));
    rename_idx_ = -1;
    rename_buf_[0] = '\0';
}

void UndekApp::trySelectMacro(int idx) {
    if (idx < 0 || idx >= static_cast<int>(library_.entries().size()) || idx == sel_macro_) return;
    if (dirty_) {
        pending_new_ = false;
        pending_sel_ = idx;
        show_unsaved_ = true;
        return;
    }
    doSelectMacro(idx);
}

void UndekApp::doSelectMacro(int idx) {
    const auto& e = library_.entries()[idx];
    sel_macro_ = idx;
    loadScript(e.path);
    for (const auto& b : hotkeys_.config().bindings) {
        if (sameScriptPath(b.script, e.path)) {
            std::strncpy(bind_hk_buf_, b.hotkey.c_str(), sizeof(bind_hk_buf_) - 1);
            break;
        }
    }
}

void UndekApp::showDeleteConfirmation() {
    del_confirm_name_.clear();
    if (sel_macro_ >= 0 && sel_macro_ < static_cast<int>(library_.entries().size()))
        del_confirm_name_ = library_.entries()[sel_macro_].name;
    show_del_confirm_ = true;
}

void UndekApp::clearSearch() {
    search_buf_[0] = '\0';
}

std::vector<int> UndekApp::getFilteredIndices() const {
    const std::string key = std::string(search_buf_) + "#" + std::to_string(library_.generation());
    if (key == filtered_key_) return filtered_cache_;

    std::vector<int> indices;
    const std::string filter = search_buf_;
    for (int i = 0; i < static_cast<int>(library_.entries().size()); ++i) {
        if (filter.empty() || library_.entries()[i].name.find(filter) != std::string::npos) {
            indices.push_back(i);
        }
    }
    filtered_cache_ = std::move(indices);
    filtered_key_ = key;
    return filtered_cache_;
}

void UndekApp::addBindingForMacro(const std::string& path) {
    const std::string abs = AppPaths::resolve(path);
    if (!std::filesystem::exists(abs)) {
        setStatus("Error: Macro file not found: " + path);
        return;
    }

    std::string norm_path = AppPaths::relativeToRoot(abs);
    for (char& c : norm_path)
        if (c == '\\') c = '/';

    auto& cfg = hotkeys_.config();
    const std::string hk = bind_hk_buf_;
    if (!bindings::parseHotkey(hk).valid) {
        setStatus("Invalid hotkey: " + hk);
        return;
    }

    for (auto& b : cfg.bindings) {
        if (bindings::hotkeysEquivalent(b.hotkey, hk)) {
            const std::string prev = b.script;
            b.script = norm_path;
            b.profile = profile_path_;
            b.enabled = true;
            hotkeys_.save(bindings_path_);
            refreshBindingsIfListening();
            if (sameScriptPath(prev, norm_path)) {
                setStatus("Bound " + hk + " -> " + norm_path);
            } else {
                setStatus("Rebound " + hk + ": " + prev + " -> " + norm_path);
            }
            return;
        }
    }

    for (auto& b : cfg.bindings) {
        if (sameScriptPath(b.script, norm_path)) {
            const std::string prev_hk = b.hotkey;
            b.hotkey = hk;
            b.profile = profile_path_;
            hotkeys_.save(bindings_path_);
            refreshBindingsIfListening();
            setStatus("Rebound " + norm_path + ": " + prev_hk + " -> " + hk);
            return;
        }
    }

    cfg.bindings.push_back({hk, norm_path, profile_path_});
    hotkeys_.save(bindings_path_);
    refreshBindingsIfListening();
    setStatus("Bound " + hk + " -> " + norm_path);
}

void UndekApp::startRecording() {
    if (runs::active()) {
        // Once a stroke leaves the driver it looks like hardware, so a take
        // started mid-run would record the macro and the user as one pile.
        setStatus("Can't record while a macro is running - stop it first");
        return;
    }

    std::string err;
    if (!InputRecorder::start(err)) {
        setStatus("Recording didn't start: " + err);
        return;
    }
    setStatus("Recording global keyboard & mouse input... Click Stop when done");
}

void UndekApp::stopRecording() {
    if (!InputRecorder::isRecording()) return;
    InputRecorder::stop();
    rec_steps_ = parseRecordedSteps(InputRecorder::takeActions());
    show_rec_ = true;
    setStatus("Recorded " + std::to_string(rec_steps_.size()) +
              " steps - review the take, then insert");
}

void UndekApp::insertRecordedScript() {
    if (rec_steps_.empty()) {
        setStatus("Nothing recorded to insert");
        return;
    }

    const std::string body = formatRecordedSteps(rec_steps_);
    if (body.empty()) {
        setStatus("Every recorded step is unticked");
        return;
    }

    // A take that ends mid-hold would play back with a key stuck down.
    const std::string imbalance = checkRecordingBalance(rec_steps_);
    if (!imbalance.empty()) {
        setStatus("Not inserted: " + imbalance);
        return;
    }

    int kept = 0;
    for (const auto& s : rec_steps_)
        if (s.enabled) ++kept;

    script_source_ += "\n// Recorded sequence\n" + body;
    dirty_ = true;
    setStatus("Inserted " + std::to_string(kept) + " recorded steps");
}

void UndekApp::addBinding() {
    auto& cfg = hotkeys_.config();
    std::string hk = "Mouse4";
    std::string sc = script_path_.empty() ? "examples/combo_humanized.undek" : script_path_;
    cfg.bindings.push_back({hk, sc, profile_path_, true});
    hotkeys_.save(bindings_path_);
    refreshBindingsIfListening();
    setStatus("Added binding " + hk);
}

void UndekApp::removeBinding(int index) {
    auto& cfg = hotkeys_.config();
    if (index < 0 || index >= static_cast<int>(cfg.bindings.size())) return;
    cfg.bindings.erase(cfg.bindings.begin() + index);
    hotkeys_.save(bindings_path_);
    refreshBindingsIfListening();
}

void UndekApp::pollHotkeys() {
    pollDetectBind();
    hotkeys_.poll();
    macros_on_ = hotkeys_.isListening();

#ifdef _WIN32
    if (prev_pick_armed_ || prev_pick_start_) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - prev_pick_time_)
                                 .count();
        if (elapsed > 200) {
            if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0) {
                prev_pick_armed_ = false;
                prev_pick_start_ = false;
                setStatus("Point pick cancelled");
            } else if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) {
                const Vec2 cur = backend_.cursorPosition();
                if (prev_pick_start_) {
                    prev_sx_ = static_cast<float>(cur.x);
                    prev_sy_ = static_cast<float>(cur.y);
                    prev_pick_start_ = false;
                } else {
                    prev_tx_ = static_cast<float>(cur.x);
                    prev_ty_ = static_cast<float>(cur.y);
                    prev_pick_armed_ = false;
                }
                dryRunPathPreview();
            }
        }
    }

    if (builder_pick_point_ || builder_pick_pixel_) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - prev_pick_time_)
                                 .count();
        if (elapsed > 200) {
            if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0) {
                builder_pick_point_ = false;
                builder_pick_pixel_ = false;
                setStatus("Builder pick cancelled");
            } else if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) {
                const Vec2 cur = backend_.cursorPosition();
                const int x = static_cast<int>(cur.x);
                const int y = static_cast<int>(cur.y);
                if (builder_pick_point_) {
                    builder_draft_.x = x;
                    builder_draft_.y = y;
                    builder_pick_point_ = false;
                    setStatus("Builder point set to (" + std::to_string(x) + ", " +
                              std::to_string(y) + ")");
                } else {
                    const auto sampled = ScreenSampler::pixelColor(x, y);
                    builder_pick_pixel_ = false;
                    if (!sampled) {
                        setStatus("Couldn't read the pixel at (" + std::to_string(x) + ", " +
                                  std::to_string(y) + ")");
                    } else {
                        builder_pixel_x_ = x;
                        builder_pixel_y_ = y;
                        builder_color_ = static_cast<int>(*sampled & 0xFFFFFFu);
                        setStatus("Pixel check set to (" + std::to_string(x) + ", " +
                                  std::to_string(y) + ")");
                    }
                }
            }
        }
    }
#endif
}

void UndekApp::onHotkeyMessage(int id) {
    hotkeys_.handleHotkey(id);
    macros_on_ = hotkeys_.isListening();
}

void UndekApp::shutdown() {
    if (InputRecorder::isRecording()) InputRecorder::stop();
    if (dbg_) dbg_->stop();
    cancel::requestShutdown();
    hotkeys_.shutdown();
    if (worker_.joinable()) worker_.join();
    macros_on_ = false;
}

void UndekApp::renderMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New macro", "Ctrl+N")) requestNewMacro();
            if (ImGui::MenuItem("Save", "Ctrl+S", false, !script_path_.empty()))
                saveCurrentScript();
            ImGui::Separator();

            ImGui::TextDisabled("Recent:");
            int count = 0;
            for (const auto& e : library_.entries()) {
                if (count >= 5) break;
                if (ImGui::MenuItem(e.name.c_str())) {
                    for (size_t i = 0; i < library_.entries().size(); ++i) {
                        if (library_.entries()[i].path == e.path) {
                            trySelectMacro(static_cast<int>(i));
                            break;
                        }
                    }
                }
                count++;
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Refresh macro list")) library_.refresh();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
#ifdef _WIN32
                PostQuitMessage(0);
#endif
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Pick cursor & color", AppSettings::get().picker_hotkey.c_str())) {
                captureCursorAndPixel();
            }
            if (ImGui::MenuItem("Clear log")) clearLogs();
            ImGui::Separator();
            ImGui::TextDisabled("Macro file actions are on right-click in the list.");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Show log panel", nullptr, &show_log_);
            ImGui::MenuItem("Show path preview", nullptr, &show_preview_);
            ImGui::MenuItem("Debugger", nullptr, &show_debugger_);
            ImGui::MenuItem("Visual builder", nullptr, &show_builder_);
            ImGui::MenuItem("Hotkey bindings & auto-profiles", nullptr, &show_hotkeys_win_);
            ImGui::MenuItem("Settings", nullptr, &show_settings_);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Run")) {
            if (ImGui::MenuItem("Run now", "F5")) runScript(false);
            if (ImGui::MenuItem("Dry-run", "F6")) runScript(true);
            if (ImGui::MenuItem("Check script")) checkCurrentScript();
            if (ImGui::MenuItem("Pause / resume", AppSettings::get().pause_hotkey.c_str(),
                                pause::pending(), running_.load())) {
                togglePause();
            }
            if (ImGui::MenuItem("Debug run", "F7")) runDebug();
            ImGui::Separator();
            if (ImGui::MenuItem("Stop running macro", nullptr, false, running_.load())) {
                stopExecution();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Scripting reference", "F1")) {
                openHelpFile("README.md",
                             "https://github.com/Tw1kslv/Undek-Macro/blob/main/README.md");
            }
            if (ImGui::MenuItem("Language reference")) {
                openHelpFile("docs/language.md",
                             "https://github.com/Tw1kslv/Undek-Macro/blob/main/docs/language.md");
            }
            if (ImGui::MenuItem("Keyboard shortcuts")) show_shortcuts_ = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Open macros folder")) openFolder(AppPaths::macrosDir());
            if (ImGui::MenuItem("Open examples folder")) openFolder(AppPaths::examplesDir());
            ImGui::Separator();
            if (ImGui::MenuItem("GitHub repository")) {
                openUrl("https://github.com/Tw1kslv/Undek-Macro");
            }
            if (ImGui::MenuItem("Report an issue")) {
                openUrl("https://github.com/Tw1kslv/Undek-Macro/issues");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("About")) show_about_ = true;
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
}

void UndekApp::renderMacroList(float width, float height) {
    ImGui::BeginChild("macros", ImVec2(width, height), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec4 on_col{0.20f, 0.68f, 0.32f, 1.0f};
    const ImVec4 off_col{0.68f, 0.24f, 0.24f, 1.0f};
    ImGui::PushStyleColor(ImGuiCol_Button, macros_on_ ? on_col : off_col);
    if (ImGui::Button(macros_on_ ? "MACROS: ON" : "MACROS: OFF", ImVec2(-1, 32))) {
        toggleMacros();
    }
    ImGui::PopStyleColor();
    ImGui::TextDisabled(macros_on_ ? "Active (%s toggles)" : "Off (%s toggles)",
                        hotkeys_.config().stop_hotkey.c_str());

    ImGui::Separator();

    const float third_w =
        (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;

    if (InputRecorder::isRecording()) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.20f, 0.20f, 1.0f));
        if (ImGui::Button("Stop Rec", ImVec2(third_w, 0))) stopRecording();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button(InputRecorder::isPaused() ? "Resume" : "Pause", ImVec2(third_w, 0))) {
            if (InputRecorder::isPaused()) {
                InputRecorder::resume();
                setStatus("Recording resumed");
            } else {
                InputRecorder::pause();
                setStatus("Recording paused");
            }
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                           InputRecorder::isPaused() ? "PAUSED (%zu)" : "REC (%zu)",
                           InputRecorder::actionCount());
    } else {
        pushButtonColor(0.55f, 0.22f, 0.24f);
        if (ImGui::Button("Record", ImVec2(third_w, 0))) startRecording();
        popButtonColor();
        ImGui::SetItemTooltip("Records what you do until you press Stop Rec");
        ImGui::SameLine();
        if (ImGui::Button("Insert Rec", ImVec2(third_w, 0))) insertRecordedScript();
        ImGui::SetItemTooltip("Writes the recording into the open script");
        ImGui::SameLine();
        if (ImGui::Button("Review Take", ImVec2(third_w, 0))) show_rec_ = true;
        ImGui::SetItemTooltip("Goes through the recording step by step before inserting it");
    }

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##search", "Search macros...", search_buf_, sizeof(search_buf_));

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##name", "New macro name...", new_name_buf_, sizeof(new_name_buf_));
    pushButtonColor(0.22f, 0.38f, 0.60f);
    if (ImGui::Button("New", ImVec2(third_w, 0))) requestNewMacro();
    popButtonColor();
    ImGui::SetItemTooltip("Creates a macro with the name above (Ctrl+N)");
    ImGui::SameLine();
    if (ImGui::Button("Refresh", ImVec2(third_w, 0))) library_.refresh();
    ImGui::SetItemTooltip("Re-reads the macros folder from disk");
    ImGui::SameLine();
    if (ImGui::Button("Pick XY", ImVec2(third_w, 0))) captureCursorAndPixel();
    ImGui::SetItemTooltip("Picks the cursor position and pixel colour (%s)",
                          AppSettings::get().picker_hotkey.c_str());

    if (sel_macro_ >= 0 && sel_macro_ < static_cast<int>(library_.entries().size())) {
        ImGui::Separator();
        const auto& sel = library_.entries()[sel_macro_];

        std::string sel_norm = normScriptPath(sel.path);
        std::string cur_hk;
        for (const auto& b : hotkeys_.config().bindings) {
            if (normScriptPath(b.script) == sel_norm) {
                cur_hk = b.hotkey;
                break;
            }
        }

        ImGui::Text("Bind: %s", cur_hk.empty() ? "(none)" : cur_hk.c_str());
        ImGui::SetNextItemWidth(72);
        ImGui::InputText("##hotkey", bind_hk_buf_, sizeof(bind_hk_buf_));
        ImGui::SameLine();
        if (detect_target_ == 1) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.45f, 0.15f, 1.0f));
            if (ImGui::Button("Press...")) detect_target_ = 0;
            ImGui::PopStyleColor();
        } else {
            if (ImGui::Button("Detect")) startDetect(1);
        }
        ImGui::SameLine();
        if (ImGui::Button("Bind")) {
            addBindingForMacro(sel.path);
        }
        if (!cur_hk.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("X")) {
                auto& cfg = hotkeys_.config();
                cfg.bindings.erase(std::remove_if(cfg.bindings.begin(), cfg.bindings.end(),
                                                  [&sel_norm](const auto& b) {
                                                      return normScriptPath(b.script) == sel_norm;
                                                  }),
                                   cfg.bindings.end());
                hotkeys_.save(bindings_path_);
                refreshBindingsIfListening();
                setStatus("Unbound " + sel.name);
            }
        }

        if (rename_idx_ == sel_macro_) {
            ImGui::SetNextItemWidth(120);
            ImGui::InputText("##rename", rename_buf_, sizeof(rename_buf_));
            ImGui::SameLine();
            if (ImGui::Button("OK")) renameMacro();
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                rename_idx_ = -1;
                rename_buf_[0] = '\0';
            }
        }
    }

    if (show_del_confirm_) {
        ImGui::OpenPopup("Confirm Delete");
        show_del_confirm_ = false;
    }
    // A window that sizes itself only knows its size once it has been drawn, so
    // the centre has to be applied every frame until it settles there.
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Confirm Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (del_confirm_name_.empty())
            ImGui::Text("Are you sure you wanna delete this macro? This cannot be undone.");
        else
            ImGui::Text("Are you sure you wanna delete \"%s\"? This cannot be undone.",
                        del_confirm_name_.c_str());
        if (ImGui::Button("Delete", ImVec2(80, 0))) {
            deleteMacro();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (show_unsaved_) {
        ImGui::OpenPopup("Unsaved Changes");
        show_unsaved_ = false;
    }
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (pending_new_)
            ImGui::Text("Starting a new macro drops the changes to \"%s\".", script_path_.c_str());
        else
            ImGui::Text("You have unsaved changes.");
        if (ImGui::Button("Save", ImVec2(90, 0))) {
            // Staying here on a failed save is the point: the edits are still
            // only in memory, so switching away would lose them.
            if (saveCurrentScript()) {
                ImGui::CloseCurrentPopup();
                completePendingAction();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(90, 0))) {
            dirty_ = false;
            ImGui::CloseCurrentPopup();
            completePendingAction();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90, 0))) {
            pending_sel_ = -1;
            pending_new_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();
    const std::vector<int>& indices = getFilteredIndices();
    ImGui::TextDisabled("Macros (%zu / %zu)", indices.size(), library_.entries().size());

    const float footer_reserve = (ImGui::GetFrameHeightWithSpacing() * 2.0f) + 4.0f;
    const float list_h = (std::max)(60.0f, ImGui::GetContentRegionAvail().y - footer_reserve);
    ImGui::BeginChild("macro_list_scroll", ImVec2(-1, list_h), false);

    for (int idx : indices) {
        const auto& e = library_.entries()[idx];
        const bool selected = (sel_macro_ == idx);

        ImGui::PushID(idx);

        bool has_bind = false;
        bool bind_en = false;
        size_t bind_idx = 0;
        std::string bound_key;
        auto& cfg = hotkeys_.config();
        for (size_t i = 0; i < cfg.bindings.size(); ++i) {
            // Whole-name matching, so cat.undek can't borrow bobcat.undek's key.
            if (sameScriptPath(cfg.bindings[i].script, e.path)) {
                has_bind = true;
                bind_en = cfg.bindings[i].enabled;
                bind_idx = i;
                bound_key = cfg.bindings[i].hotkey;
                break;
            }
        }

        if (has_bind) {
            bool en = bind_en;
            if (ImGui::Checkbox("##en", &en)) {
                cfg.bindings[bind_idx].enabled = en;
                hotkeys_.save(bindings_path_);
                refreshBindingsIfListening();
            }
            ImGui::SameLine();
        } else {
            ImGui::Dummy(ImVec2(24, 0));
            ImGui::SameLine();
        }

        ImVec4 col =
            selected ? ImVec4(0.72f, 0.58f, 1.0f, 1.0f) : ImVec4(0.93f, 0.94f, 0.97f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, col);

        std::string label = bound_key.empty() ? e.name : (e.name + "  [" + bound_key + "]");
        if (ImGui::Selectable(label.c_str(), selected)) {
            trySelectMacro(idx);
        }
        if (ImGui::BeginPopupContextItem("##macro_menu")) {
            if (!selected) ImGui::TextDisabled("%s", e.name.c_str());
            const bool ready = selected;
            if (ImGui::MenuItem("Duplicate", nullptr, false, ready)) duplicateMacro();
            if (ImGui::MenuItem("Rename", nullptr, false, ready)) {
                rename_idx_ = idx;
                std::strncpy(rename_buf_, e.name.c_str(), sizeof(rename_buf_) - 1);
            }
            if (ImGui::MenuItem("Delete", nullptr, false, ready)) showDeleteConfirmation();
            if (!ready) {
                ImGui::Separator();
                ImGui::TextDisabled("Click the macro first, then right-click it again.");
            }
            ImGui::EndPopup();
        }

        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (ImGui::Button("Hotkey Bindings", ImVec2(-1, 0))) {
        show_hotkeys_win_ = !show_hotkeys_win_;
    }
    if (ImGui::Button("Humanization Settings", ImVec2(-1, 0))) {
        show_settings_ = !show_settings_;
    }
    ImGui::EndChild();
}

void UndekApp::renderEditor(float width, float height) {
    const float panel_w = (width <= 0.0f) ? ImGui::GetContentRegionAvail().x : width;
    ImGui::BeginChild("editor_panel", ImVec2(panel_w, height), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Script: %s%s", script_path_.c_str(), dirty_ ? " *" : "");
    ImGui::SameLine();
    if (dirty_) pushButtonColor(0.20f, 0.42f, 0.30f);
    if (ImGui::Button("Save")) saveCurrentScript();
    if (dirty_) popButtonColor();
    ImGui::SetItemTooltip(dirty_ ? "Saves the script (Ctrl+S) - it has unsaved changes"
                                 : "Saves the script (Ctrl+S)");
    ImGui::SameLine();
    pushButtonColor(0.20f, 0.46f, 0.32f);
    if (ImGui::Button("Run (F5)")) runScript(false);
    popButtonColor();
    ImGui::SetItemTooltip("Runs the script and sends real input (F5)");
    ImGui::SameLine();
    if (ImGui::Button("Dry-run (F6)")) runScript(true);
    ImGui::SetItemTooltip("Walks the script without sending anything (F6)");
    ImGui::SameLine();
    if (ImGui::Button("Debug (F7)")) runDebug();
    ImGui::SetItemTooltip("Runs until a breakpoint, then steps line by line (F7)");
    if (running_.load()) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.22f, 0.22f, 1.0f));
        if (ImGui::Button("Stop")) {
            stopExecution();
        }
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    // The checkbox is the whole path plot: ticking it draws the next move when
    // there's nothing plotted yet, and the pane stays under the script.
    bool show_path = show_preview_;
    if (ImGui::Checkbox("Show path", &show_path)) {
        show_preview_ = show_path;
        if (show_path && preview_steps_.empty()) dryRunPathPreview();
    }
    ImGui::SetItemTooltip("Plots the next move under the script and keeps it open");
    ImGui::SameLine();
    // Ticking Log while the panel was collapsed used to show nothing, since the
    // collapsed flag was left set.
    if (ImGui::Checkbox("Log", &show_log_) && show_log_) log_collapsed_ = false;
    ImGui::SetItemTooltip("Shows the Execution Log below the script");
    ImGui::SameLine();
    if (ImGui::Button("Clear Log")) clearLogs();
    ImGui::SetItemTooltip("Empties the Execution Log");

    const float spacing_y = ImGui::GetStyle().ItemSpacing.y;
    const float avail_y = ImGui::GetContentRegionAvail().y;
    const bool has_preview = show_preview_ && !preview_steps_.empty();
    const float preview_h = has_preview ? 80.0f : 0.0f;
    const float preview_space =
        has_preview ? (preview_h + ImGui::GetTextLineHeightWithSpacing() * 3.0f + spacing_y) : 0.0f;

    // The log starts a little shorter than it used to and the bar above it can be
    // dragged, so a long script gets the room and an idle log stops hogging it.
    const float splitter_h = 7.0f;
    // The floors shrink with a short window instead of pushing the log out of
    // view: on a 200 px pane, 140 px of log and 120 px of script cannot both fit.
    // The log's own spacing is part of what it takes up, or the script's
    // minimum would be two gaps short of what the layout actually needs.
    const float usable_y =
        (std::max)(0.0f, avail_y - preview_space - splitter_h - spacing_y * 2.0f);
    const float log_min = (std::min)(140.0f, usable_y * 0.35f);
    const float code_min = (std::min)(120.0f, usable_y * 0.4f);
    const float max_log = (std::max)(log_min, usable_y - code_min);
    const bool log_visible = show_log_ && !log_collapsed_;
    float log_h = 0.0f;
    if (log_visible) {
        if (log_h_ <= 0.0f) log_h_ = std::clamp(avail_y * 0.28f, log_min, max_log);
        log_h_ = std::clamp(log_h_, log_min, max_log);
        log_h = log_h_;
    }
    // The splitter keeps its strip even when the log is collapsed; shrinking the
    // script into that space left nothing to drag the log back with.
    const float log_space = show_log_ ? (log_h + splitter_h + spacing_y * 2.0f) : 0.0f;
    const float code_h = (std::max)(0.0f, avail_y - preview_space - log_space);

    ImGui::BeginChild("editor_code", ImVec2(-1, code_h), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const float gutter_w = 42.0f;
    const ImVec2 code_origin = ImGui::GetCursorScreenPos();
    const ImVec2 code_avail = ImGui::GetContentRegionAvail();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(code_origin,
                      ImVec2(code_origin.x + gutter_w - 4.0f, code_origin.y + code_avail.y),
                      IM_COL32(18, 19, 25, 255), 4.0f);

    ImGui::SetCursorScreenPos(ImVec2(code_origin.x + gutter_w, code_origin.y));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.16f, 1.0f));
    if (ImGui::InputTextMultiline("##source", &script_source_,
                                  ImVec2(code_avail.x - gutter_w, code_avail.y),
                                  ImGuiInputTextFlags_AllowTabInput)) {
        dirty_ = true;
        error_line_ = -1;
    }
    const ImGuiID src_id = ImGui::GetItemID();
    // Ask "is the text field focused" here, while it is still the last item.
    // Further down we add the gutter InvisibleButton, and IsItemActive() would
    // then answer for that button instead - which is why the caret never drew.
    const bool source_active = ImGui::IsItemActive();
    ImGui::PopStyleColor(2);

    float scroll_x = 0.0f;
    float scroll_y = 0.0f;
    ImGuiWindow* code_win = ImGui::GetCurrentWindow();
    if (code_win && !code_win->DC.ChildWindows.empty()) {
        ImGuiWindow* input_win = code_win->DC.ChildWindows[0];
        if (input_win) {
            scroll_x = input_win->Scroll.x;
            scroll_y = input_win->Scroll.y;
        }
    }

    const ImVec2 pad = ImGui::GetStyle().FramePadding;
    const float line_h = ImGui::GetTextLineHeight();
    const ImVec2 text_base(code_origin.x + gutter_w + pad.x - scroll_x,
                           code_origin.y + pad.y - scroll_y);

    ImGui::SetCursorScreenPos(code_origin);
    ImGui::InvisibleButton("##bp_gutter", ImVec2(gutter_w - 4.0f, code_avail.y));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        const int total_lines =
            static_cast<int>(std::count(script_source_.begin(), script_source_.end(), '\n')) + 1;
        const int ln = static_cast<int>((ImGui::GetIO().MousePos.y - text_base.y) / line_h) + 1;
        if (ln >= 1 && ln <= total_lines) toggleBreakpoint(ln);
    }

    dl->PushClipRect(ImVec2(code_origin.x, code_origin.y),
                     ImVec2(code_origin.x + code_avail.x, code_origin.y + code_avail.y), true);

    // Tokenised and measured only when the text changes; drawing a frame just
    // replays what is already there.
    if (editor_layout_source_ != script_source_) rebuildEditorLayout();
    for (size_t i = 0; i < editor_layout_.size(); ++i) {
        const float y = text_base.y + static_cast<float>(i) * line_h;
        if (y + line_h < code_origin.y || y > code_origin.y + code_avail.y) continue;
        const int line_no = static_cast<int>(i) + 1;
        char num_buf[16];
        std::snprintf(num_buf, sizeof(num_buf), "%3d", line_no);
        dl->AddText(ImVec2(code_origin.x + 6.0f, y), IM_COL32(105, 110, 128, 255), num_buf);
        if (breakpoints_.count(line_no)) {
            dl->AddCircleFilled(ImVec2(code_origin.x + gutter_w - 10.0f, y + line_h * 0.5f), 3.5f,
                                IM_COL32(235, 85, 85, 255));
        }
    }
    dl->PopClipRect();

    dl->PushClipRect(ImVec2(code_origin.x + gutter_w, code_origin.y),
                     ImVec2(code_origin.x + code_avail.x - 14.0f, code_origin.y + code_avail.y),
                     true);
    const int err_line = error_line_.load();
    const int dbg_line = debug_line_.load();
    for (size_t i = 0; i < editor_layout_.size(); ++i) {
        const float y = text_base.y + static_cast<float>(i) * line_h;
        if (y + line_h < code_origin.y || y > code_origin.y + code_avail.y) continue;
        const int line_no = static_cast<int>(i) + 1;
        if (err_line == line_no) {
            dl->AddRectFilled(ImVec2(code_origin.x + gutter_w, y),
                              ImVec2(code_origin.x + code_avail.x - 14.0f, y + line_h),
                              IM_COL32(170, 40, 40, 70));
        }
        if (dbg_line == line_no) {
            dl->AddRectFilled(ImVec2(code_origin.x + gutter_w, y),
                              ImVec2(code_origin.x + code_avail.x - 14.0f, y + line_h),
                              IM_COL32(50, 150, 90, 80));
        }
        float x = text_base.x;
        for (const auto& tok : editor_layout_[i]) {
            dl->AddText(ImVec2(x, y), tok.color, tok.text.c_str());
            x += tok.width;
        }
    }
    dl->PopClipRect();

    edit_ln_ = edit_col_ = -1;
    if (source_active) {
        if (ImGuiInputTextState* istate = ImGui::GetInputTextState(src_id)) {
            const int cursor =
                std::clamp(istate->GetCursorPos(), 0, static_cast<int>(script_source_.size()));
            const char* buf = script_source_.c_str();
            const char* line_start = buf;
            int ln = 1;
            for (int i = 0; i < cursor; ++i) {
                if (buf[i] == '\n') {
                    ++ln;
                    line_start = buf + i + 1;
                }
            }
            edit_ln_ = ln;
            edit_col_ = static_cast<int>(buf + cursor - line_start) + 1;

            if (!istate->HasSelection() && ImFmod(istate->CursorAnim, 1.20f) <= 0.80f) {
                ImFont* font = ImGui::GetFont();
                const float cx = text_base.x + font->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX,
                                                                   -1.0f, line_start, buf + cursor)
                                                   .x;
                const float cy = text_base.y + (ln - 1) * line_h;
                dl->PushClipRect(ImVec2(code_origin.x + gutter_w, code_origin.y),
                                 ImVec2(code_origin.x + code_avail.x, code_origin.y + code_avail.y),
                                 true);
                dl->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + 1.5f, cy + line_h),
                                  IM_COL32(235, 235, 240, 255));
                dl->PopClipRect();
            }
        }
    }

    ImGui::EndChild();

    if (has_preview) {
        const float w = ImGui::GetContentRegionAvail().x;
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList* draw = ImGui::GetWindowDrawList();

        double min_x = preview_steps_[0].position.x, max_x = min_x;
        double min_y = preview_steps_[0].position.y, max_y = min_y;
        auto grow = [&](const Vec2& p) {
            min_x = (std::min)(min_x, p.x);
            max_x = (std::max)(max_x, p.x);
            min_y = (std::min)(min_y, p.y);
            max_y = (std::max)(max_y, p.y);
        };
        for (const auto& s : preview_steps_)
            grow(s.position);
        grow(preview_c1_);
        grow(preview_c2_);

        const double rw = (std::max)(1.0, max_x - min_x);
        const double rh = (std::max)(1.0, max_y - min_y);
        auto on_screen = [&](const Vec2& p) {
            const float nx = static_cast<float>((p.x - min_x) / rw);
            const float ny = static_cast<float>((p.y - min_y) / rh);
            return ImVec2{origin.x + nx * w, origin.y + ny * preview_h};
        };

        double total_ms = 0.0;
        double slowest = 1.0;
        for (const auto& s : preview_steps_) {
            total_ms += s.delay_ms;
            slowest = (std::max)(slowest, s.delay_ms);
        }

        const ImU32 guide = IM_COL32(150, 160, 190, 70);
        draw->AddLine(on_screen(preview_steps_.front().position), on_screen(preview_c1_), guide,
                      1.0f);
        draw->AddLine(on_screen(preview_steps_.back().position), on_screen(preview_c2_), guide,
                      1.0f);

        ImVec2 prev = on_screen(preview_steps_[0].position);
        for (size_t i = 1; i < preview_steps_.size(); ++i) {
            const ImVec2 p = on_screen(preview_steps_[i].position);
            // More orange means the step waited longer.
            const float t = static_cast<float>(preview_steps_[i].delay_ms / slowest);
            const ImU32 col =
                IM_COL32(100 + static_cast<int>(140.0f * t), 200 - static_cast<int>(90.0f * t),
                         255 - static_cast<int>(160.0f * t), 255);
            draw->AddLine(prev, p, col, 2.0f);
            prev = p;
        }

        draw->AddCircleFilled(on_screen(preview_steps_.front().position), 4.0f,
                              IM_COL32(110, 220, 130, 255));
        draw->AddCircleFilled(on_screen(preview_steps_.back().position), 4.0f,
                              IM_COL32(235, 110, 110, 255));
        draw->AddCircle(on_screen(preview_c1_), 3.5f, IM_COL32(230, 200, 110, 200));
        draw->AddCircle(on_screen(preview_c2_), 3.5f, IM_COL32(230, 200, 110, 200));

        ImGui::Dummy(ImVec2(w, preview_h));
        ImGui::TextDisabled("%zu steps, ~%.0f ms - warmer line = slower step",
                            preview_steps_.size(), total_ms);

        ImGui::SetNextItemWidth(110);
        ImGui::SliderFloat("curve##pv", &pv_curve_, 0.0f, 0.8f, "%.2f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        ImGui::SliderFloat("wobble##pv", &pv_wobble_, 0.0f, 4.0f, "%.1f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        ImGui::SliderFloat("overshoot##pv", &pv_overshoot_, 0.0f, 0.5f, "%.2f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        ImGui::SliderInt("steps##pv", &pv_steps_, 4, 120, "%d");
        ImGui::SameLine();
        if (ImGui::SmallButton("Regen")) dryRunPathPreview();

        ImGui::SetNextItemWidth(140);
        ImGui::InputFloat2("start##pv", &prev_sx_, "%.0f");
        ImGui::SameLine();
        if (prev_pick_start_) {
            if (ImGui::SmallButton("Picking start...")) prev_pick_start_ = false;
        } else if (ImGui::SmallButton("Pick start")) {
            prev_pick_start_ = true;
            prev_pick_armed_ = false;
            prev_pick_time_ = std::chrono::steady_clock::now();
            setStatus("Click anywhere to set the start point (Esc to cancel)");
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140);
        ImGui::InputFloat2("target##pv", &prev_tx_, "%.0f");
        ImGui::SameLine();
        if (prev_pick_armed_) {
            if (ImGui::SmallButton("Picking target...")) prev_pick_armed_ = false;
        } else if (ImGui::SmallButton("Pick target")) {
            prev_pick_armed_ = true;
            prev_pick_start_ = false;
            prev_pick_time_ = std::chrono::steady_clock::now();
            setStatus("Click anywhere to set the target (Esc to cancel)");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Use profile")) {
            pv_init_ = false;
            dryRunPathPreview();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset")) {
            prev_sx_ = 0.0f;
            prev_sy_ = 0.0f;
            prev_tx_ = 0.0f;
            prev_ty_ = 0.0f;
            prev_pick_armed_ = false;
            prev_pick_start_ = false;
            dryRunPathPreview();
        }
    }

    if (show_log_) {
        const ImVec2 split_pos = ImGui::GetCursorScreenPos();
        const float split_w = ImGui::GetContentRegionAvail().x;
        ImGui::InvisibleButton("##log_splitter", ImVec2(-1, splitter_h));
        const bool held = ImGui::IsItemActive();
        const bool hot = ImGui::IsItemHovered() || held;
        if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (held) {
            const float delta = ImGui::GetIO().MouseDelta.y;
            if (log_collapsed_) {
                // Dragging up off a collapsed log brings it back at its default.
                if (delta < 0.0f) {
                    log_collapsed_ = false;
                    log_h_ = 0.0f;
                }
            } else {
                log_h_ = (std::max)(0.0f, log_h_ - delta);
                // Pulling it all the way down puts the log away rather than
                // leaving a two-line sliver that can't show anything.
                if (log_h_ < 24.0f) {
                    log_h_ = 0.0f;
                    log_collapsed_ = true;
                }
            }
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            log_h_ = 0.0f; // back to the default height
            log_collapsed_ = false;
        }
        const float mid = split_pos.y + splitter_h * 0.5f;
        const float grip_w = hot ? 46.0f : 22.0f;
        const float cx = split_pos.x + split_w * 0.5f;
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(cx - grip_w * 0.5f, mid - 1.0f), ImVec2(cx + grip_w * 0.5f, mid + 1.0f),
            hot ? IM_COL32(125, 150, 215, 210) : IM_COL32(88, 93, 112, 130), 1.0f);

        if (log_collapsed_) {
            ImGui::EndChild();
            return;
        }

        ImGui::BeginChild("log_panel", ImVec2(-1, log_h), true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        {
            std::lock_guard<std::mutex> lock(log_mtx_);
            if (log_snapshot_version_ != log_version_) {
                log_snapshot_ = logs_;
                log_snapshot_version_ = log_version_;
            }
        }

        ImGui::Text("Execution Log");
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu entries)", log_snapshot_.size());
        ImGui::Separator();

        if (ImGui::BeginChild("log_scroll", ImVec2(-1, ImGui::GetContentRegionAvail().y), false,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            for (const auto& entry : log_snapshot_) {
                ImGui::TextUnformatted(entry.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
        ImGui::EndChild();
    }
    ImGui::EndChild();
}

void UndekApp::renderHotkeysWindow() {
    if (!show_hotkeys_win_) return;
    ImGui::SetNextWindowSize(ImVec2(540, 460), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Hotkey Bindings & Auto-Profiles", &show_hotkeys_win_,
                      ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::SetNextItemWidth(160);
    ImGui::InputText("Toggle hotkey", stop_hk_buf_, sizeof(stop_hk_buf_));
    ImGui::SameLine();
    if (detect_target_ == 2) {
        if (ImGui::Button("Press key...##t2")) detect_target_ = 0;
    } else {
        if (ImGui::Button("Detect##t2")) startDetect(2);
    }

    ImGui::SetNextItemWidth(220);
    ImGui::InputText("Default profile", profile_buf_, sizeof(profile_buf_));
    ImGui::SameLine();
    if (ImGui::Button("Apply")) {
        hotkeys_.config().stop_hotkey = stop_hk_buf_;
        hotkeys_.config().default_profile = profile_buf_;
        profile_path_ = profile_buf_;
        setProfilePath(profile_path_);
        hotkeys_.save(bindings_path_);
        hotkeys_.registerToggleHotkey(bindings::HotkeyRegistry::window());
        refreshBindingsIfListening();
        setStatus("Hotkey settings applied");
    }

    ImGui::Separator();
    ImGui::Text("Macro Bindings (supports keyboard & Mouse4 / Mouse5 / Mouse3)");
    if (ImGui::Button("Add binding")) addBinding();
    ImGui::SameLine();
    if (ImGui::Button("Save all")) {
        hotkeys_.save(bindings_path_);
        refreshBindingsIfListening();
        setStatus("Bindings saved");
    }

    auto& cfg = hotkeys_.config();
    for (int i = 0; i < static_cast<int>(cfg.bindings.size()); ++i) {
        ImGui::PushID(i);

        bool en = cfg.bindings[i].enabled;
        if (ImGui::Checkbox("##en", &en)) {
            cfg.bindings[i].enabled = en;
            hotkeys_.save(bindings_path_);
            refreshBindingsIfListening();
        }
        ImGui::SameLine();

        char hk[64]{};
        std::strncpy(hk, cfg.bindings[i].hotkey.c_str(), sizeof(hk) - 1);
        char sc[256]{};
        std::strncpy(sc, cfg.bindings[i].script.c_str(), sizeof(sc) - 1);

        ImGui::SetNextItemWidth(85);
        if (ImGui::InputText("##hk", hk, sizeof(hk))) cfg.bindings[i].hotkey = hk;
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            hotkeys_.save(bindings_path_);
            refreshBindingsIfListening();
        }
        ImGui::SameLine();
        if (detect_target_ == 100 + i) {
            if (ImGui::Button("Press...")) detect_target_ = 0;
        } else {
            if (ImGui::Button("Detect")) startDetect(100 + i);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(210);
        if (ImGui::InputText("##sc", sc, sizeof(sc))) cfg.bindings[i].script = sc;
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            hotkeys_.save(bindings_path_);
            refreshBindingsIfListening();
        }
        ImGui::SameLine();
        if (ImGui::Button("X")) {
            removeBinding(i);
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::Text("Per-Window Auto-Profile Switching");
    ImGui::TextDisabled(
        "Automatically switches humanization profile when a matching window is focused:");

    ImGui::SetNextItemWidth(130);
    ImGui::InputTextWithHint("##rwin", "Window title...", rule_win_buf_, sizeof(rule_win_buf_));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(190);
    ImGui::InputTextWithHint("##rprof", "Profile path...", rule_prof_buf_, sizeof(rule_prof_buf_));
    ImGui::SameLine();
    if (ImGui::Button("Add Rule") && rule_win_buf_[0] != '\0' && rule_prof_buf_[0] != '\0') {
        cfg.window_profiles.push_back({rule_win_buf_, rule_prof_buf_});
        hotkeys_.save(bindings_path_);
        setStatus("Added auto-profile rule for " + std::string(rule_win_buf_));
    }

    for (int i = 0; i < static_cast<int>(cfg.window_profiles.size()); ++i) {
        ImGui::PushID(2000 + i);
        auto& r = cfg.window_profiles[i];
        ImGui::Text("Window contains \"%s\" -> %s", r.window_title.c_str(), r.profile.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) {
            cfg.window_profiles.erase(cfg.window_profiles.begin() + i);
            hotkeys_.save(bindings_path_);
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }

    ImGui::End();
}

void UndekApp::renderDebuggerWindow() {
    if (!show_debugger_) return;
    ImGui::SetNextWindowSize(ImVec2(440, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Debugger", &show_debugger_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    script::DebugController::Snapshot snap;
    std::vector<script::DebugController::HistoryEntry> hist;
    bool has_session = (dbg_ != nullptr);
    if (has_session) {
        snap = dbg_->snapshot();
        hist = dbg_->history();
    }
    const auto st = snap.state;
    const bool paused = has_session && st == script::DebugController::State::Paused;
    const bool live = has_session && st != script::DebugController::State::Finished;

    if (!has_session) {
        ImGui::TextDisabled("No debug session (F7 / Debug run starts one)");
    } else if (paused) {
        ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.30f, 1.0f), "PAUSED @ line %d",
                           snap.current_line);
    } else if (st == script::DebugController::State::Running) {
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "RUNNING");
    } else {
        ImGui::TextDisabled("FINISHED");
    }
    if (!snap.current_stmt.empty() && st != script::DebugController::State::Finished) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", snap.current_stmt.c_str());
    }

    if (!live) {
        if (ImGui::Button("Debug run (F7)") && !running_.load()) runDebug();
        ImGui::SameLine();
    }
    if (paused) {
        if (ImGui::Button("Step (F10)")) dbg_->step();
        ImGui::SameLine();
        if (ImGui::Button("Continue (F8)")) dbg_->resume();
        ImGui::SameLine();
    } else if (live) {
        if (ImGui::Button("Pause")) dbg_->pause();
        ImGui::SameLine();
    }
    if (live) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.22f, 0.22f, 1.0f));
        if (ImGui::Button("Stop")) stopExecution();
        ImGui::PopStyleColor();
    }
    if (has_session && !live) {
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            dbg_.reset();
            debug_line_ = -1;
        }
    }

    ImGui::Separator();
    ImGui::Text("Breakpoints (%zu)", breakpoints_.size());
    if (breakpoints_.empty()) {
        ImGui::TextDisabled("Click a line number in the editor (or F9 at caret)");
    } else {
        int rm = -1;
        for (const int bp : breakpoints_) {
            ImGui::BulletText("line %d", bp);
            ImGui::SameLine();
            ImGui::PushID(bp);
            if (ImGui::SmallButton("x")) rm = bp;
            ImGui::PopID();
        }
        if (rm > 0) toggleBreakpoint(rm);
    }

    ImGui::Separator();
    ImGui::Text("Variables");
    if (snap.vars.empty()) {
        ImGui::TextDisabled("(none)");
    } else {
        for (const auto& v : snap.vars) {
            ImGui::BulletText("%s = (%.1f, %.1f)", v.first.c_str(), v.second.x, v.second.y);
        }
    }

    ImGui::Text("Held inputs");
    if (snap.held_inputs.empty()) {
        ImGui::TextDisabled("(none)");
    } else {
        for (const auto& h : snap.held_inputs) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "[%s]", h.c_str());
        }
        ImGui::NewLine();
    }

    ImGui::Separator();
    double run_ms = 0.0;
    for (const auto& h : hist)
        run_ms += h.elapsed_ms;

    ImGui::Text("Execution history (%zu)", hist.size());
    ImGui::SameLine();
    ImGui::TextDisabled("%.0f ms total", run_ms);
    ImGui::BeginChild("dbg_hist", ImVec2(-1, ImGui::GetContentRegionAvail().y), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    for (size_t i = 0; i < hist.size(); ++i) {
        const bool cur = paused && (i + 1 == hist.size());
        const ImVec4 row =
            cur ? ImVec4(0.95f, 0.80f, 0.30f, 1.0f) : ImVec4(0.62f, 0.65f, 0.72f, 1.0f);
        ImGui::TextColored(row, "%7.1f ms  %4d  %s", hist[i].elapsed_ms, hist[i].line,
                           hist[i].text.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    ImGui::End();
}

void UndekApp::renderSettingsWindow() {
    if (!show_settings_) return;
    ImGui::SetNextWindowSize(ImVec2(560, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Settings", &show_settings_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    auto& s = AppSettings::get();
    if (ImGui::CollapsingHeader("Window, tray & global hotkeys", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Start minimized", &s.start_minimized);
        ImGui::Checkbox("Minimize to tray", &s.minimize_to_tray);
        ImGui::Checkbox("Close button hides to tray", &s.close_to_tray);
        ImGui::Checkbox("Macro ON/OFF sounds (toggle hotkey)", &s.toggle_sound);
        float vol = static_cast<float>(s.toggle_sound_volume);
        if (ImGui::SliderFloat("Toggle sound volume", &vol, 0.0f, 100.0f, "%.0f%%")) {
            s.toggle_sound_volume = vol;
        }

        ImGui::SetNextItemWidth(150);
        ImGui::InputText("Show/hide tray hotkey", show_hk_buf_, sizeof(show_hk_buf_));
        ImGui::SameLine();
        if (detect_target_ == 3) {
            if (ImGui::Button("Press...##d3")) detect_target_ = 0;
        } else {
            if (ImGui::Button("Detect##d3")) startDetect(3);
        }

        ImGui::SetNextItemWidth(150);
        ImGui::InputText("Stealth hide hotkey", stealth_hk_buf_, sizeof(stealth_hk_buf_));
        ImGui::SameLine();
        if (detect_target_ == 4) {
            if (ImGui::Button("Press...##d4")) detect_target_ = 0;
        } else {
            if (ImGui::Button("Detect##d4")) startDetect(4);
        }
        textDisabledWrapped("Stealth hide removes window, taskbar, and tray icon; pressing "
                            "again restores previous state.");

        ImGui::SetNextItemWidth(150);
        ImGui::InputText("Pixel/coord picker hotkey", picker_hk_buf_, sizeof(picker_hk_buf_));
        ImGui::SameLine();
        if (detect_target_ == 5) {
            if (ImGui::Button("Press...##d5")) detect_target_ = 0;
        } else {
            if (ImGui::Button("Detect##d5")) startDetect(5);
        }

        ImGui::SetNextItemWidth(150);
        ImGui::InputText("Pause/resume hotkey", pause_hk_buf_, sizeof(pause_hk_buf_));
        ImGui::SameLine();
        if (detect_target_ == 6) {
            if (ImGui::Button("Press...##d6")) detect_target_ = 0;
        } else {
            if (ImGui::Button("Detect##d6")) startDetect(6);
        }

        if (ImGui::Button("Save window & hotkey settings")) {
            s.show_hotkey = show_hk_buf_;
            s.stealth_hotkey = stealth_hk_buf_;
            s.picker_hotkey = picker_hk_buf_;
            s.pause_hotkey = pause_hk_buf_;
            s.save(AppPaths::resolve("settings.json"));
            refreshGlobalAppHotkeys();
            setStatus("Window & global hotkey settings saved and applied");
        }
    }

    // Everything past this line belongs to the profile file, not to the
    // window's own settings, and only Save profile writes it.
    ImGui::Spacing();
    ImGui::SeparatorText("Profile - humanization settings");

    if (ImGui::CollapsingHeader("Timing", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& p = profile_;
        ImGui::TextDisabled("Profile: %s", profile_path_.c_str());

        auto usePreset = [&](ProfilePreset which) {
            profile_.applyPreset(which);
            rng_.setProfile(profile_);
            paths_.setProfile(profile_);
            setStatus(std::string("Preset: ") + HumanizationProfile::presetName(which) +
                      " (Save profile to keep it)");
        };
        ImGui::TextUnformatted("Presets");
        ImGui::SameLine();
        if (ImGui::Button("Safe")) usePreset(ProfilePreset::Safe);
        ImGui::SameLine();
        if (ImGui::Button("Balanced")) usePreset(ProfilePreset::Balanced);
        ImGui::SameLine();
        if (ImGui::Button("Aggressive")) usePreset(ProfilePreset::Aggressive);
        ImGui::SameLine();
        if (ImGui::Button("Off")) usePreset(ProfilePreset::Off);
        textDisabledWrapped("Fills in the sliders below. Safety limits, required window and "
                            "backend stay as they are.");

        if (ImGui::Button("Sample this profile")) {
            timing_preview_ = describeDelays(sampleDelays(profile_));
            appendLog("[timing] " + timing_preview_);
        }
        if (!timing_preview_.empty()) ImGui::TextWrapped("%s", timing_preview_.c_str());
        textDisabledWrapped("Runs a typical 40 ms wait through the timers 400 times, stats "
                            "only - nothing is sent.");

        auto slider = [](const char* label, double& v, float lo, float hi,
                         const char* fmt = "%.2f") {
            float f = static_cast<float>(v);
            if (ImGui::SliderFloat(label, &f, lo, hi, fmt)) v = f;
        };
        slider("Delay sigma (ms)", p.delay_sigma_ms, 0.0f, 100.0f, "%.1f");
        slider("Delay min (ms)", p.delay_min_ms, 0.0f, 200.0f, "%.1f");
        slider("Delay max (ms)", p.delay_max_ms, 100.0f, 10000.0f, "%.0f");
    }

    if (ImGui::CollapsingHeader("Movement", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& p = profile_;
        auto slider = [](const char* label, double& v, float lo, float hi,
                         const char* fmt = "%.2f") {
            float f = static_cast<float>(v);
            if (ImGui::SliderFloat(label, &f, lo, hi, fmt)) v = f;
        };
        slider("Move base time (ms)", p.path_base_ms, 0.0f, 600.0f, "%.0f");
        slider("Move time per log2 dist (ms)", p.path_fitts_ms, 0.0f, 300.0f, "%.0f");
        slider("Curve strength min", p.bezier_curve_strength_min, 0.0f, 0.6f);
        slider("Curve strength max", p.bezier_curve_strength_max, 0.0f, 0.6f);
        slider("Path wobble (px)", p.path_wobble_px, 0.0f, 4.0f);
        slider("Overshoot probability", p.overshoot_probability, 0.0f, 0.5f);
        slider("Overshoot pixels min", p.overshoot_pixels_min, 0.0f, 30.0f, "%.1f");
        slider("Overshoot pixels max", p.overshoot_pixels_max, 0.0f, 40.0f, "%.1f");
        slider("Mid-path pause probability", p.pause_probability, 0.0f, 0.3f);
        slider("Pause min (ms)", p.pause_ms_min, 0.0f, 500.0f, "%.0f");
        slider("Pause max (ms)", p.pause_ms_max, 0.0f, 800.0f, "%.0f");
    }

    if (ImGui::CollapsingHeader("Session", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& p = profile_;
        auto slider = [](const char* label, double& v, float lo, float hi,
                         const char* fmt = "%.3f") {
            float f = static_cast<float>(v);
            if (ImGui::SliderFloat(label, &f, lo, hi, fmt)) v = f;
        };
        ImGui::Checkbox("Session drift", &p.session_drift_enabled);
        slider("Drift min", p.session_drift_min, 0.5f, 1.0f, "%.2f");
        slider("Drift max", p.session_drift_max, 1.0f, 1.5f, "%.2f");
        slider("Drift period (sec)", p.session_drift_period_sec, 30.0f, 3600.0f, "%.0f");

        ImGui::Separator();
        ImGui::Checkbox("Fatigue & focus dynamic cycles", &p.fatigue_focus_enabled);
        slider("Cycle min (sec)", p.fatigue_cycle_min_sec, 10.0f, 600.0f, "%.0f");
        slider("Cycle max (sec)", p.fatigue_cycle_max_sec, 10.0f, 600.0f, "%.0f");
        slider("Delay shift min", p.fatigue_shift_min, 0.0f, 0.30f, "%.2f");
        slider("Delay shift max", p.fatigue_shift_max, 0.0f, 0.40f, "%.2f");
        slider("Sigma widen min", p.fatigue_sigma_widen_min, 1.0f, 2.0f, "%.2f");
        slider("Sigma widen max", p.fatigue_sigma_widen_max, 1.0f, 2.5f, "%.2f");

        ImGui::Separator();
        ImGui::Checkbox("Micro stalls (distraction factor)", &p.micro_stall_enabled);
        slider("Anomaly chance", p.micro_stall_chance, 0.0f, 0.10f, "%.3f");
        slider("Stall min (ms)", p.micro_stall_min_ms, 50.0f, 500.0f, "%.0f");
        slider("Stall max (ms)", p.micro_stall_max_ms, 100.0f, 800.0f, "%.0f");
    }

    if (ImGui::CollapsingHeader("Key holds & input variation", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& p = profile_;
        auto slider = [](const char* label, double& v, float lo, float hi,
                         const char* fmt = "%.3f") {
            float f = static_cast<float>(v);
            if (ImGui::SliderFloat(label, &f, lo, hi, fmt)) v = f;
        };
        ImGui::Checkbox("Rarely missed inputs", &p.miss_input_enabled);
        slider("Miss probability", p.miss_probability, 0.0f, 0.10f, "%.3f");

        ImGui::Separator();
        ImGui::Checkbox("Key hold variability & panic stretch", &p.hold_variability_enabled);
        slider("Hold scale min", p.hold_scale_min, 0.5f, 1.0f, "%.2f");
        slider("Hold scale max", p.hold_scale_max, 1.0f, 2.0f, "%.2f");
        slider("Hold panic chance", p.hold_panic_chance, 0.0f, 0.30f, "%.2f");
        slider("Panic stretch min", p.hold_panic_mult_min, 1.0f, 3.0f, "%.2f");
        slider("Panic stretch max", p.hold_panic_mult_max, 1.0f, 4.0f, "%.2f");

        ImGui::Separator();
        ImGui::Checkbox("Finger overlap (MouseDown/KeyDown before KeyUp)", &p.key_overlap_enabled);
        slider("Overlap probability", p.key_overlap_probability, 0.0f, 0.50f, "%.2f");
        slider("Overlap min (ms)", p.key_overlap_min_ms, 1.0f, 50.0f, "%.0f");
        slider("Overlap max (ms)", p.key_overlap_max_ms, 2.0f, 80.0f, "%.0f");
    }

    if (ImGui::CollapsingHeader("Input backend & packet delay")) {
        auto& p = profile_;
        auto slider = [](const char* label, double& v, float lo, float hi,
                         const char* fmt = "%.1f") {
            float f = static_cast<float>(v);
            if (ImGui::SliderFloat(label, &f, lo, hi, fmt)) v = f;
        };
        ImGui::InputText("Input backend", backend_buf_, sizeof(backend_buf_));
        ImGui::TextDisabled("auto | interception | sendinput");
        slider("Minimum press (ms)", p.min_press_ms, 0.0f, 200.0f, "%.0f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A humanized hold never comes out shorter than this, so a "
                              "press is long enough for the target to notice.");
        ImGui::Checkbox("Min inter-packet delay", &p.min_packet_delay_enabled);
        slider("Packet gap min (ms)", p.min_packet_delay_ms, 0.0f, 100.0f, "%.0f");
        slider("Packet gap jitter (ms)", p.min_packet_delay_jitter_ms, 0.0f, 50.0f, "%.0f");
    }

    if (ImGui::CollapsingHeader("Safety limits")) {
        auto& p = profile_;
        float rt = static_cast<float>(p.max_runtime_sec);
        if (ImGui::InputFloat("Max runtime (sec, 0 = off)", &rt, 10.0f, 60.0f, "%.0f")) {
            p.max_runtime_sec = (std::max)(0.0f, rt);
        }
        ImGui::InputInt("Max loop iterations", &p.max_loop_iterations);
        if (p.max_loop_iterations < 1) p.max_loop_iterations = 1;
        ImGui::InputText("Require window title", req_win_buf_, sizeof(req_win_buf_));
        ImGui::Checkbox("Log to file (logs/)", &p.log_to_file);
    }

    if (ImGui::Button("Save profile")) {
        profile_.require_window = req_win_buf_;
        profile_.input_backend = backend_buf_;
        std::string error;
        // Saving one the runner would refuse only moves the problem to the next
        // run, so the value is checked before it is written out.
        if (!profile_.validate(error)) {
            setStatus("Profile not saved: " + error);
        } else {
            rng_.setProfile(profile_);
            paths_.setProfile(profile_);
            setStatus(profile_.saveToFile(AppPaths::resolve(profile_path_))
                          ? "Profile saved: " + profile_path_
                          : "Failed to save profile");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload profile")) setProfilePath(profile_path_);
    ImGui::SameLine();
    if (ImGui::Button("Reset to defaults")) {
        profile_ = HumanizationProfile::defaults();
        profile_.require_window = req_win_buf_;
        profile_.input_backend = backend_buf_;
        rng_.setProfile(profile_);
        paths_.setProfile(profile_);
        setStatus("Profile reset to defaults (use Save profile to persist)");
    }

    ImGui::End();
}

void UndekApp::insertBuilderScript() {
    const std::string body = formatBuilderScript(builder_steps_);
    if (body.empty()) {
        setStatus("Nothing to insert - add a step first");
        return;
    }

    script_source_ += "\n// Built with the visual builder\n" + body;
    dirty_ = true;
    setStatus("Inserted " + std::to_string(builder_steps_.size()) + " built steps");
}

void UndekApp::renderBuilderWindow() {
    if (!show_builder_) return;
    ImGui::SetNextWindowSize(ImVec2(560, 430), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Visual builder", &show_builder_)) {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled(
        "Pick an action, fill in the fields, then Add. Insert writes plain .undek code.");
    ImGui::Separator();

    if (ImGui::Combo("Action", &builder_action_idx_,
                     "Wait\0Move cursor\0Click\0Press key\0Hold key down\0Release key\0"
                     "Hold mouse button\0Release mouse button\0Scroll wheel\0")) {
        builder_draft_.action = static_cast<BuilderAction>(builder_action_idx_);
    }
    const BuilderAction action = builder_draft_.action;

    if (builderActionNeedsPoint(action)) {
        ImGui::SetNextItemWidth(110);
        ImGui::InputInt("X", &builder_draft_.x, 0, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        ImGui::InputInt("Y", &builder_draft_.y, 0, 0);
        ImGui::SameLine();
        // The click on this button is where the cursor already is, so the next
        // click on the screen is the one that counts.
        if (builder_pick_point_) {
            if (ImGui::SmallButton("Click a point...")) builder_pick_point_ = false;
        } else if (ImGui::SmallButton("Use cursor")) {
            builder_pick_point_ = true;
            builder_pick_pixel_ = false;
            prev_pick_armed_ = false;
            prev_pick_start_ = false;
            prev_pick_time_ = std::chrono::steady_clock::now();
            setStatus("Click anywhere to use that point (Esc to cancel)");
        }
    }

    if (action == BuilderAction::Wait) {
        ImGui::SetNextItemWidth(110);
        ImGui::InputDouble("Milliseconds", &builder_draft_.wait_ms, 1.0, 10.0, "%.0f");
    }

    if (action == BuilderAction::Scroll) {
        ImGui::SetNextItemWidth(110);
        ImGui::InputInt("Amount", &builder_draft_.amount, 120, 120);
    }

    if (builderActionNeedsText(action)) {
        if (builder_draft_.text.empty()) builder_draft_.text = builder_text_buf_;
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputText("Name", builder_text_buf_, sizeof(builder_text_buf_))) {
            builder_draft_.text = builder_text_buf_;
        }
        if (builderActionIsButton(action)) {
            ImGui::SameLine();
            if (ImGui::SmallButton("left")) builder_draft_.text = "left";
            ImGui::SameLine();
            if (ImGui::SmallButton("right")) builder_draft_.text = "right";
            ImGui::SameLine();
            if (ImGui::SmallButton("middle")) builder_draft_.text = "middle";
            std::strncpy(builder_text_buf_, builder_draft_.text.c_str(),
                         sizeof(builder_text_buf_) - 1);
        }
    }

    if (action == BuilderAction::Wait || action == BuilderAction::MoveTo ||
        action == BuilderAction::Click || action == BuilderAction::PressKey) {
        ImGui::Checkbox("Humanize", &builder_draft_.humanize);
    }

    const std::string preview = formatBuilderStep(builder_draft_);
    ImGui::TextDisabled("%s", preview.empty() ? "(fill in the fields)" : preview.c_str());

    ImGui::BeginDisabled(preview.empty());
    if (ImGui::Button("Add step")) {
        auto* target = builderTarget(builder_steps_, builder_target_);
        if (!target) {
            builder_target_.clear();
            target = &builder_steps_;
        }
        target->push_back(builder_draft_);
        setStatus("Added builder step");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(builder_steps_.empty());
    pushButtonColor(0.22f, 0.38f, 0.60f);
    if (ImGui::Button("Insert into script")) insertBuilderScript();
    popButtonColor();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(builder_steps_.empty());
    if (ImGui::Button("Clear")) builder_steps_.clear();
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::TextDisabled("Tick steps below, then wrap them in a block");
    // All three wrap buttons line up in one right hand column, so the rows read
    // as one control block however wide their fields are.
    const float wrap_btn_w = 155.0f;
    const float wrap_btn_x = ImGui::GetWindowContentRegionMax().x - wrap_btn_w;

    ImGui::SetNextItemWidth(90);
    ImGui::InputInt("Loop count", &builder_loop_count_, 1, 5);
    ImGui::SameLine(wrap_btn_x);
    if (ImGui::Button("Wrap in loop", ImVec2(wrap_btn_w, 0))) {
        const int wrapped =
            wrapSelectedAnywhere(builder_steps_, BuilderAction::Loop, {}, builder_loop_count_);
        setStatus(wrapped > 0 ? "Wrapped " + std::to_string(wrapped) + " group(s) in a loop"
                              : "Tick the steps to wrap first");
    }

    ImGui::SetNextItemWidth(150);
    ImGui::InputText("Window title", builder_window_buf_, sizeof(builder_window_buf_));
    ImGui::SameLine(wrap_btn_x);
    if (ImGui::Button("Wrap in if window", ImVec2(wrap_btn_w, 0))) {
        const int wrapped =
            wrapSelectedAnywhere(builder_steps_, BuilderAction::IfWindow, builder_window_buf_);
        setStatus(wrapped > 0 ? "Wrapped " + std::to_string(wrapped) + " group(s) in a window check"
                              : "Tick the steps to wrap first");
    }

    ImGui::SetNextItemWidth(52);
    ImGui::InputInt("X##wrap_px", &builder_pixel_x_, 0, 0);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(52);
    ImGui::InputInt("Y##wrap_py", &builder_pixel_y_, 0, 0);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(72);
    ImGui::InputInt("##wrap_colour", &builder_color_, 0, 0, ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SetItemTooltip("Expected colour, six hex digits");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(52);
    ImGui::InputInt("##wrap_tol", &builder_tolerance_, 0, 0);
    ImGui::SetItemTooltip("Colour tolerance");
    ImGui::SameLine();
    // Saves typing the coordinates and then hunting for the colour in another
    // tool: one click on the screen fills in all four fields.
    if (builder_pick_pixel_) {
        if (ImGui::SmallButton("Click a pixel...")) builder_pick_pixel_ = false;
    } else if (ImGui::SmallButton("Pick")) {
        builder_pick_pixel_ = true;
        builder_pick_point_ = false;
        prev_pick_armed_ = false;
        prev_pick_start_ = false;
        prev_pick_time_ = std::chrono::steady_clock::now();
        setStatus("Click anywhere to take that pixel's position and colour (Esc to cancel)");
    }
    ImGui::SameLine(wrap_btn_x);
    if (ImGui::Button("Wrap in if pixel", ImVec2(wrap_btn_w, 0))) {
        const unsigned int colour = static_cast<unsigned int>(builder_color_ & 0xFFFFFF);
        const int wrapped =
            wrapSelectedAnywhere(builder_steps_, BuilderAction::IfPixel, {}, 3, builder_pixel_x_,
                                 builder_pixel_y_, colour, builder_tolerance_);
        setStatus(wrapped > 0 ? "Wrapped " + std::to_string(wrapped) + " group(s) in a pixel check"
                              : "Tick the steps to wrap first");
    }

    ImGui::Separator();
    if (builder_steps_.empty()) {
        ImGui::TextDisabled("No steps yet. A step looks like the preview line above.");
    }

    ImGui::TextDisabled("Adding to: %s", builderTargetLabel().c_str());
    ImGui::SameLine();
    // Nothing to go back to at the top, and a coloured button is a reminder
    // that adding steps is going somewhere other than the top level.
    const bool nested = !builder_target_.empty();
    ImGui::BeginDisabled(!nested);
    if (nested) pushButtonColor(0.70f, 0.52f, 0.20f);
    if (ImGui::SmallButton("Top level")) builder_target_.clear();
    if (nested) popButtonColor();
    ImGui::EndDisabled();

    ImGui::BeginChild("builder_list", ImVec2(-1, -1), false);
    renderBuilderRows(builder_steps_, {});
    ImGui::EndChild();
    ImGui::End();
}

std::string UndekApp::builderTargetLabel() const {
    if (builder_target_.empty()) return "top level";
    std::string out;
    for (size_t i = 0; i < builder_target_.size(); ++i) {
        if (i) out += " > ";
        out += "step " + std::to_string(builder_target_[i] + 1);
    }
    return out;
}

void UndekApp::renderBuilderRows(std::vector<BuilderStep>& steps, std::vector<int> path) {
    for (int i = 0; i < static_cast<int>(steps.size()); ++i) {
        BuilderStep& s = steps[i];
        ImGui::PushID(i);
        ImGui::Checkbox("##wrap_sel", &s.selected);
        ImGui::SameLine();
        ImGui::Text("%2d", i + 1);
        ImGui::SameLine();
        ImGui::TextUnformatted(builderStepSummary(s).c_str());
        ImGui::SameLine();
        // Moving a row stops the loop here for this frame: after the swap the
        // rest of the row would be drawing a step that has moved.
        ImGui::BeginDisabled(i == 0);
        if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
            std::swap(steps[i], steps[i - 1]);
            ImGui::EndDisabled();
            ImGui::PopID();
            return;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(i + 1 >= static_cast<int>(steps.size()));
        if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
            std::swap(steps[i], steps[i + 1]);
            ImGui::EndDisabled();
            ImGui::PopID();
            return;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            steps.erase(steps.begin() + i);
            ImGui::PopID();
            return;
        }

        if (builderActionIsBlock(s.action)) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Add here")) {
                builder_target_ = path;
                builder_target_.push_back(i);
                setStatus("New steps go inside that block");
            }
            std::vector<int> child = path;
            child.push_back(i);
            ImGui::Indent(24.0f);
            renderBuilderRows(s.body, child);
            ImGui::Unindent(24.0f);
        }
        ImGui::PopID();
    }
}

void UndekApp::renderRecorderWindow() {
    if (!show_rec_) return;
    ImGui::SetNextWindowSize(ImVec2(600, 440), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Recorded actions", &show_rec_)) {
        ImGui::End();
        return;
    }

    if (rec_steps_.empty()) {
        ImGui::TextDisabled("Nothing recorded yet. Hit Record, do your thing, then Stop Rec.");
        ImGui::End();
        return;
    }

    int kept = 0;
    for (const auto& s : rec_steps_)
        if (s.enabled) ++kept;

    ImGui::Text("%zu steps, %d kept", rec_steps_.size(), kept);
    ImGui::SameLine();
    if (ImGui::SmallButton("Insert into script")) insertRecordedScript();
    ImGui::SameLine();
    if (ImGui::SmallButton("Keep all")) {
        for (auto& s : rec_steps_)
            s.enabled = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) rec_steps_.clear();
    ImGui::Separator();

    int remove_idx = -1;
    int remove_group = -1;
    int group_end = -1;

    ImGui::BeginChild("rec_list", ImVec2(-1, -1), false);
    for (int i = 0; i < static_cast<int>(rec_steps_.size()); ++i) {
        auto& s = rec_steps_[i];
        ImGui::PushID(i);

        const bool group_start =
            s.drag_group >= 0 && (i == 0 || rec_steps_[i - 1].drag_group != s.drag_group);
        if (group_start) {
            group_end = i;
            while (group_end + 1 < static_cast<int>(rec_steps_.size()) &&
                   rec_steps_[group_end + 1].drag_group == s.drag_group) {
                ++group_end;
            }
            ImGui::TextDisabled("left drag - %d steps", group_end - i + 1);
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove group")) remove_group = s.drag_group;
            ImGui::Indent(16.0f);
        }

        bool en = s.enabled;
        if (ImGui::Checkbox("##keep", &en)) s.enabled = en;
        ImGui::SameLine();

        int wait = s.wait_ms;
        ImGui::SetNextItemWidth(70);
        if (ImGui::InputInt("##wait", &wait, 0, 0)) s.wait_ms = (std::max)(0, wait);
        ImGui::SameLine();
        ImGui::TextDisabled("ms");
        ImGui::SameLine();

        char text_buf[192] = {};
        std::strncpy(text_buf, s.text.c_str(), sizeof(text_buf) - 1);
        ImGui::SetNextItemWidth(-120.0f);
        if (ImGui::InputText("##text", text_buf, sizeof(text_buf))) s.text = text_buf;
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) remove_idx = i;

        // Grouped steps only move as a block, so keep the arrows off them.
        if (s.drag_group < 0) {
            ImGui::SameLine();
            ImGui::BeginDisabled(i == 0);
            if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
                std::swap(rec_steps_[i - 1], rec_steps_[i]);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(i + 1 >= static_cast<int>(rec_steps_.size()));
            if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
                std::swap(rec_steps_[i], rec_steps_[i + 1]);
            }
            ImGui::EndDisabled();
        }

        if (group_end == i) ImGui::Unindent(16.0f);
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (remove_group >= 0) {
        rec_steps_.erase(
            std::remove_if(rec_steps_.begin(), rec_steps_.end(),
                           [&](const RecordedStep& s) { return s.drag_group == remove_group; }),
            rec_steps_.end());
    }
    if (remove_idx >= 0 && remove_idx < static_cast<int>(rec_steps_.size())) {
        rec_steps_.erase(rec_steps_.begin() + remove_idx);
    }

    ImGui::End();
}

void UndekApp::renderRecorderOverlay() {
    if (!InputRecorder::isRecording()) return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 16.0f, viewport->WorkPos.y + 16.0f),
        ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.85f);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("##recorder_overlay", nullptr, flags)) {
        const bool paused = InputRecorder::isPaused();
        ImGui::TextColored(paused ? ImVec4(0.95f, 0.75f, 0.30f, 1.0f)
                                  : ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                           paused ? "RECORDER PAUSED" : "RECORDER ON");
        ImGui::SameLine();
        ImGui::TextDisabled("%zu actions", InputRecorder::actionCount());
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop")) stopRecording();
    }
    ImGui::End();
}

void UndekApp::renderStatusBar(float height) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 7.0f));
    ImGui::BeginChild("status", ImVec2(-1, height), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::AlignTextToFramePadding();
    // MACROS ON/OFF is on the big button now, so this bar is only what is
    // happening this second: state, profile, cursor.
    if (running_.load() && pause::pending()) {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1.0f), "PAUSED");
    } else if (running_.load()) {
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "RUNNING");
    } else {
        ImGui::TextColored(ImVec4(0.60f, 0.62f, 0.70f, 1.0f), "IDLE");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    std::string profile_name = std::filesystem::path(profile_path_).filename().string();
    if (profile_name.empty()) profile_name = "default";
    ImGui::TextDisabled("Profile: %s", profile_name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    const Vec2 cur = backend_.cursorPosition();
    ImGui::TextDisabled("XY: (%d, %d) [%s to pick]", static_cast<int>(cur.x),
                        static_cast<int>(cur.y), AppSettings::get().picker_hotkey.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    std::string status_copy;
    {
        std::lock_guard<std::mutex> lock(log_mtx_);
        status_copy = status_msg_;
    }
    ImGui::TextUnformatted(status_copy.c_str());

    char cursor_buf[32] = {};
    if (edit_ln_ >= 0) {
        std::snprintf(cursor_buf, sizeof(cursor_buf), "Ln %d, Col %d", edit_ln_, edit_col_);
    }
    const char* ver_text = "v" UNDEK_VERSION;
    const char* link_text = "GitHub";
    const float sx = ImGui::GetStyle().ItemSpacing.x;
    const float sep_w = ImGui::CalcTextSize("|").x + sx;
    float right_w = ImGui::CalcTextSize(ver_text).x + sep_w + ImGui::CalcTextSize(link_text).x + sx;
    if (cursor_buf[0]) right_w += ImGui::CalcTextSize(cursor_buf).x + sep_w;
    ImGui::SameLine();
    ImGui::SetCursorPosX(
        (std::max)(ImGui::GetCursorPosX() + 8.0f, ImGui::GetContentRegionMax().x - right_w - 6.0f));
    if (cursor_buf[0]) {
        ImGui::TextDisabled("%s", cursor_buf);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    }
    ImGui::TextDisabled("%s", ver_text);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextLinkOpenURL(link_text, "https://github.com/Tw1kslv/Undek-Macro");
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void UndekApp::openHelpFile(const std::string& relative, const char* fallback_url) {
    const std::string path = AppPaths::resolve(relative);
    if (openDoc(path)) {
        appendLog("[help] opened " + relative);
        return;
    }
    if (fallback_url && shellOpen(fallback_url)) {
        appendLog("[help] " + relative + " isn't next to the app - opened it on GitHub");
        return;
    }
    setStatus("Couldn't open " + relative + " - it should be next to undek.exe");
}

void UndekApp::openUrl(const std::string& url) {
    if (!shellOpen(url)) setStatus("Couldn't open " + url);
}

void UndekApp::openFolder(const std::string& path) {
    if (!shellOpen(path)) setStatus("Couldn't open " + path);
}

void UndekApp::renderShortcutsWindow() {
    if (!show_shortcuts_) return;

    ImGui::SetNextWindowSize(ImVec2(470, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Keyboard shortcuts", &show_shortcuts_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    const auto& s = AppSettings::get();
    const std::string toggle_key = hotkeys_.config().stop_hotkey;

    auto row = [](const std::string& keys, const char* what) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(keys.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("%s", what);
    };

    if (ImGui::BeginTable("##shortcuts", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Keys", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);

        row("F5", "Run the open script");
        row("F6", "Dry-run it without sending anything");
        row("F7", "Start a debugger session");
        row("F9", "Breakpoint on the caret line");
        row("F10 / F8", "Step / continue while paused");
        row("Ctrl+S", "Save the script");
        row("Ctrl+N", "New macro");
        row("F1", "Open the scripting reference");
        row(toggle_key.empty() ? "(unbound)" : toggle_key, "Turn all macros on and off");
        row(s.show_hotkey, "Show or hide this window");
        row(s.stealth_hotkey, "Stealth hide (window, taskbar and tray)");
        row(s.picker_hotkey, "Grab the cursor position and pixel colour");
        row(s.pause_hotkey, "Pause or resume a running macro");

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Global hotkeys are set in Settings; macro bindings in Hotkey Bindings.");
    ImGui::TextDisabled("Run, Dry-run, Save and the debugger keys are fixed.");

    ImGui::End();
}

void UndekApp::renderAboutWindow() {
    if (!show_about_) return;

    ImGui::SetNextWindowSize(ImVec2(420, 240), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("About Undek Macro", &show_about_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::Text("Undek Macro %s", UNDEK_VERSION);
    ImGui::TextDisabled("A macro tool that moves and types like a person.");
    ImGui::Spacing();
    ImGui::TextWrapped("Macros live in %s", AppPaths::macrosDir().c_str());
    ImGui::TextWrapped("Examples live in %s", AppPaths::examplesDir().c_str());
    ImGui::Spacing();
    ImGui::TextUnformatted("Scripting help:");
    ImGui::BulletText("Every command is listed in the README, with its arguments");
    ImGui::BulletText("docs/language.md has the full grammar");
    ImGui::Spacing();
    if (ImGui::Button("Scripting reference")) {
        openHelpFile("README.md", "https://github.com/Tw1kslv/Undek-Macro/blob/main/README.md");
    }
    ImGui::SameLine();
    if (ImGui::Button("Shortcuts")) show_shortcuts_ = true;
    ImGui::SameLine();
    if (ImGui::Button("Macros folder")) openFolder(AppPaths::macrosDir());
    ImGui::Spacing();
    ImGui::TextLinkOpenURL("github.com/Tw1kslv/Undek-Macro",
                           "https://github.com/Tw1kslv/Undek-Macro");

    ImGui::Spacing();
    if (ImGui::Button("Close", ImVec2(100, 0))) show_about_ = false;

    ImGui::End();
}

void UndekApp::showWelcome(const FirstRunReport& report, const std::string& version) {
    if (!report.fresh) return;
    show_welcome_ = true;
    welcome_version_ = version;
    welcome_error_ = report.error;
    welcome_created_ = report.created;
}

void UndekApp::renderWelcomeWindow() {
    if (!show_welcome_) return;

    ImGui::SetNextWindowSize(ImVec2(540, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Welcome to Undek Macro", &show_welcome_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::Text("Version %s", welcome_version_.c_str());
    ImGui::TextWrapped(
        "Macros are small scripts. Record one with the recorder, write one by hand, or build "
        "one from the visual builder - all three end up as the same .undek text you can edit "
        "later.");

    ImGui::Spacing();
    ImGui::TextUnformatted("First things to try:");
    ImGui::BulletText("F5 runs the macro that's open, F6 does a dry run with no input sent.");
    ImGui::BulletText("F7 starts the debugger: breakpoints, step, and variables as they change.");
    ImGui::BulletText(
        "Bind a macro to a hotkey in the bindings list, then toggle it from anywhere.");
    ImGui::BulletText("Settings has the backend choice - if the game ignores SendInput, try "
                      "Interception.");

    if (!welcome_created_.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Set up for you on this first start:");
        for (const auto& item : welcome_created_)
            ImGui::BulletText("%s", item.c_str());
    }

    if (!welcome_error_.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.0f), "One thing didn't work: %s",
                           welcome_error_.c_str());
    }

    ImGui::Spacing();
    ImGui::TextWrapped("Example scripts live in: %s", AppPaths::examplesDir().c_str());

    ImGui::Spacing();
    if (ImGui::Button("Got it", ImVec2(120, 0))) show_welcome_ = false;
    ImGui::SameLine();
    ImGui::TextDisabled("(shown once, on the first start only)");

    ImGui::End();
}

void UndekApp::renderFrame() {
    if (dbg_) {
        const auto snap = dbg_->snapshot();
        debug_line_ =
            (snap.state == script::DebugController::State::Paused) ? snap.current_line : -1;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##mainwindow", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    renderMenuBar();

    const float spacing_y = ImGui::GetStyle().ItemSpacing.y;
    const float status_bar_h = 44.0f;
    const float content_h =
        (std::max)(100.0f, ImGui::GetContentRegionAvail().y - status_bar_h - spacing_y);
    const float sidebar_w = 295.0f;

    ImGui::BeginChild("##content_row", ImVec2(-1, content_h), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    renderMacroList(sidebar_w, ImGui::GetContentRegionAvail().y);
    ImGui::SameLine(0, 10);
    renderEditor(0, ImGui::GetContentRegionAvail().y);
    ImGui::EndChild();

    renderStatusBar(ImGui::GetContentRegionAvail().y);

    ImGui::End();
    ImGui::PopStyleVar(2);

    renderHotkeysWindow();
    renderSettingsWindow();
    renderDebuggerWindow();
    renderRecorderWindow();
    renderRecorderOverlay();
    renderBuilderWindow();
    renderWelcomeWindow();
    renderAboutWindow();
    renderShortcutsWindow();

    // A shortcut pressed while a dialog is up belongs to the dialog, not to
    // the script behind it.
    const bool dialog_open = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    if (!dialog_open) {
        if (ImGui::IsKeyPressed(ImGuiKey_S, false) && ImGui::GetIO().KeyCtrl) saveCurrentScript();
        if (ImGui::IsKeyPressed(ImGuiKey_N, false) && ImGui::GetIO().KeyCtrl) requestNewMacro();
        if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
            openHelpFile("README.md", "https://github.com/Tw1kslv/Undek-Macro/blob/main/README.md");
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) runScript(false);
        if (ImGui::IsKeyPressed(ImGuiKey_F6, false)) runScript(true);
        if (ImGui::IsKeyPressed(ImGuiKey_F7, false)) runDebug();
        if (ImGui::IsKeyPressed(ImGuiKey_F9, false) && edit_ln_ > 0) toggleBreakpoint(edit_ln_);
        if (dbg_ && dbg_->state() == script::DebugController::State::Paused) {
            if (ImGui::IsKeyPressed(ImGuiKey_F10, false)) dbg_->step();
            if (ImGui::IsKeyPressed(ImGuiKey_F8, false)) dbg_->resume();
        }
    }
}

} // namespace undek::editor
