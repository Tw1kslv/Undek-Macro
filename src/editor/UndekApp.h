#pragma once

#include "app/FirstRun.h"
#include "bindings/HotkeyService.h"
#include "core/HumanizationProfile.h"
#include "core/PathGenerator.h"
#include "core/ProfileStats.h"
#include "core/RandomizationEngine.h"
#include "editor/MacroBuilder.h"
#include "editor/MacroLibrary.h"
#include "editor/RecordedActions.h"
#include "input/Win32SendInputBackend.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace undek::script {
class DebugController;
}

namespace undek::editor {

void refreshGlobalAppHotkeys();

// One coloured run of a line, measured once so drawing a frame doesn't have to
// tokenise and measure the whole script again.
struct EditorLineToken {
    std::string text;
    unsigned int color = 0;
    float width = 0.0f;
};

class UndekApp {
public:
    UndekApp();
    void renderFrame();
    void pollHotkeys();
    void onHotkeyMessage(int id);
    void togglePause();
    void captureCursorAndPixel();
    void shutdown();
    // Called once at startup with what prepareFirstRun() did.
    void showWelcome(const FirstRunReport& report, const std::string& version);

private:
    void renderMenuBar();
    void renderMacroList(float width, float height);
    void renderEditor(float width, float height);
    void renderHotkeysWindow();
    void renderStatusBar(float height);
    void renderSettingsWindow();
    void renderDebuggerWindow();
    void renderWelcomeWindow();
    void renderAboutWindow();
    void renderShortcutsWindow();
    void openHelpFile(const std::string& relative, const char* fallback_url);
    void openUrl(const std::string& url);
    void openFolder(const std::string& path);

    void startDetect(int target);
    void pollDetectBind();

    void appendLog(const std::string& msg, bool update_status = true);
    void clearLogs();
    void rebuildEditorLayout();
    void setStatus(const std::string& msg);
    void loadScript(const std::string& path);
    void setProfilePath(const std::string& path);
    bool saveCurrentScript();
    void runScript(bool dry_run);
    void runDebug();
    void checkCurrentScript();
    void stopExecution();
    void toggleBreakpoint(int line);
    void dryRunPathPreview();
    void toggleMacros();
    void refreshBindingsIfListening();
    void requestNewMacro();
    void createNewMacro();
    void completePendingAction();
    void deleteMacro();
    void duplicateMacro();
    void renameMacro();
    void trySelectMacro(int idx);
    void doSelectMacro(int idx);
    void addBinding();
    void addBindingForMacro(const std::string& path);
    void removeBinding(int index);
    void showDeleteConfirmation();
    void clearSearch();
    std::vector<int> getFilteredIndices() const;
    void startRecording();
    void stopRecording();
    void insertRecordedScript();
    void insertBuilderScript();
    void renderRecorderWindow();
    void renderBuilderWindow();
    void renderBuilderRows(std::vector<BuilderStep>& steps, std::vector<int> path);
    std::string builderTargetLabel() const;
    void renderRecorderOverlay();

    MacroLibrary library_;
    bindings::HotkeyService hotkeys_;

    std::string script_path_;
    std::string profile_path_;
    std::string bindings_path_;
    std::string script_source_;
    std::string del_confirm_name_;
    std::string status_msg_;
    std::string timing_preview_;
    char new_name_buf_[128] = "new_macro";
    char stop_hk_buf_[64]{};
    char profile_buf_[256]{};
    char search_buf_[64]{};
    char rename_buf_[128]{};
    char bind_hk_buf_[64] = "Mouse4";
    char show_hk_buf_[64]{};
    char stealth_hk_buf_[64]{};
    char picker_hk_buf_[64]{};
    char pause_hk_buf_[64]{};
    char req_win_buf_[128]{};
    char backend_buf_[32]{};
    char rule_win_buf_[128] = "Minecraft";
    char rule_prof_buf_[256] = "profiles/default.json";
    bool show_welcome_ = false;
    bool show_about_ = false;
    bool show_shortcuts_ = false;
    std::string welcome_version_;
    std::string welcome_error_;
    std::vector<std::string> welcome_created_;
    bool show_settings_ = false;
    bool show_hotkeys_win_ = false;
    bool show_debugger_ = false;
    std::set<int> breakpoints_;
    std::shared_ptr<script::DebugController> dbg_;

    int detect_target_ = 0;
    std::chrono::steady_clock::time_point detect_start_;

    int sel_macro_ = -1;
    int rename_idx_ = -1;
    bool macros_on_ = false;
    bool show_preview_ = false;
    bool dirty_ = false;
    bool show_del_confirm_ = false;
    bool show_log_ = true;
    float log_h_ = 0.0f; // 0 means "work it out from the window height"
    bool log_collapsed_ = false;
    bool show_rec_ = false;
    int pending_sel_ = -1;
    bool pending_new_ = false;
    bool show_unsaved_ = false;
    int edit_ln_ = -1;
    int edit_col_ = -1;
    std::atomic<int> error_line_{-1};
    std::atomic<int> debug_line_{-1};
    float prev_tx_ = 0.0f;
    float prev_ty_ = 0.0f;
    bool prev_pick_armed_ = false;
    std::chrono::steady_clock::time_point prev_pick_time_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    mutable std::mutex log_mtx_;
    std::deque<std::string> logs_;
    unsigned long long log_version_ = 0;
    std::deque<std::string> log_snapshot_;
    unsigned long long log_snapshot_version_ = 0;
    std::vector<std::vector<EditorLineToken>> editor_layout_;
    std::string editor_layout_source_;
    mutable std::vector<int> filtered_cache_;
    mutable std::string filtered_key_;
    std::vector<RecordedStep> rec_steps_;
    bool show_builder_ = false;
    std::vector<BuilderStep> builder_steps_;
    BuilderStep builder_draft_;
    int builder_action_idx_ = 2; // Click
    char builder_text_buf_[64] = "e";
    char builder_window_buf_[128] = "Untitled";
    int builder_loop_count_ = 3;
    int builder_pixel_x_ = 0;
    int builder_pixel_y_ = 0;
    int builder_color_ = 0xFFFFFF;
    int builder_tolerance_ = 0;
    std::vector<int> builder_target_; // empty means the top level
    // The next click on the screen fills the builder in; the click that armed
    // it is the one on the button, so it can't be the one that counts.
    bool builder_pick_point_ = false;
    bool builder_pick_pixel_ = false;

    HumanizationProfile profile_;
    RandomizationEngine rng_;
    PathGenerator paths_;
    Win32SendInputBackend backend_;
    std::vector<undek::PathStep> preview_steps_;
    Vec2 preview_c1_{};
    Vec2 preview_c2_{};
    float prev_sx_ = 0.0f;
    float prev_sy_ = 0.0f;
    bool prev_pick_start_ = false;
    float pv_curve_ = 0.35f;
    float pv_wobble_ = 0.8f;
    float pv_overshoot_ = 0.05f;
    int pv_steps_ = 25;
    bool pv_init_ = false;
};

} // namespace undek::editor
