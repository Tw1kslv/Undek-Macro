#pragma once

#include "core/Vec2.h"
#include "script/Ast.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace undek::script {

// Shared between the GUI thread (controls + snapshot reads) and the VM
// worker thread (onStatement hook). A null DebugController* means the VM
// runs with zero debug overhead.
class DebugController {
public:
    enum class State { Paused, Running, Finished };
    enum class Mode { Step, Continue };

    struct HistoryEntry {
        int line = 0;
        std::string text;
        // Wall time since the previous statement, ignoring debug pauses.
        double elapsed_ms = 0.0;
    };

    struct Snapshot {
        State state = State::Running;
        int current_line = 0;
        std::string current_stmt;
        std::vector<std::pair<std::string, Vec2>> vars;
        std::vector<std::string> held_inputs;
        int loop_iteration = 0;
    };

    // pause_on_entry: stop before executing the first statement.
    explicit DebugController(bool pause_on_entry = true);

    // VM thread -------------------------------------------------------
    // Called before each statement executes. Blocks while paused;
    // throws cancel::Cancelled when stopped.
    void onStatement(const Stmt& stmt, const std::unordered_map<std::string, Vec2>& vars,
                     const std::set<std::string>& held_keys,
                     const std::set<std::string>& held_buttons, int loop_iteration);
    void finish();

    // GUI thread ------------------------------------------------------
    void setBreakpoints(std::set<int> lines);
    void step();   // execute one statement, then pause again
    void resume(); // run until next breakpoint or end
    void pause();  // pause before the next statement
    void stop();   // unblock the worker and cancel the run

    Snapshot snapshot() const;
    std::vector<HistoryEntry> history() const;
    State state() const;

private:
    mutable std::mutex mtx_;
    std::condition_variable cv_;

    Mode mode_ = Mode::Continue;
    State state_ = State::Running;
    bool stop_ = false;
    bool pause_next_ = false;
    bool unpause_ = false;
    std::set<int> breakpoints_;
    Snapshot snap_;
    std::deque<HistoryEntry> history_;
    std::chrono::steady_clock::time_point last_stamp_;
};

// One-line summary of a statement, e.g. KeyDown("E"), Wait(100ms).
std::string describeStmt(const Stmt& stmt);

} // namespace undek::script
