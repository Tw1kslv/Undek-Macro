#include "script/DebugController.h"

#include "core/Cancel.h"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace undek::script {
namespace {

constexpr size_t kMaxHistory = 64;

std::string fmtNum(double v) {
    std::ostringstream oss;
    oss << v;
    return oss.str();
}

std::string fmtPoint(const Vec2& p) {
    return "(" + fmtNum(p.x) + ", " + fmtNum(p.y) + ")";
}

std::string describeCond(const Condition& c) {
    std::string s;
    if (c.negate) s += "not ";
    if (c.type == Condition::Type::WindowTitle) {
        s += "WindowTitle(\"" + c.text + "\")";
        return s;
    }
    if (c.type == Condition::Type::Image) {
        s += "ImageExists(\"" + c.image_path + "\"";
        if (c.tolerance > 0) s += ", tolerance=" + std::to_string(c.tolerance);
        s += ")";
        return s;
    }
    s += "PixelAt(" + std::to_string(c.pixel_x) + ", " + std::to_string(c.pixel_y) + ")";
    if (c.has_color_check) {
        std::ostringstream oss;
        oss << " == 0x" << std::hex << c.expected_color << std::dec;
        s += oss.str();
        if (c.tolerance > 0) s += " tolerance " + std::to_string(c.tolerance);
    }
    return s;
}

} // namespace

std::string describeStmt(const Stmt& s) {
    switch (s.kind) {
        case StmtKind::Profile:
            return "profile default_sigma=" +
                   fmtNum(static_cast<const ProfileStmt&>(s).default_sigma);
        case StmtKind::VarAssign: {
            const auto& v = static_cast<const VarAssignStmt&>(s);
            return "var " + v.name + " = " + fmtPoint(v.point);
        }
        case StmtKind::Loop:
            return "loop " + std::to_string(static_cast<const LoopStmt&>(s).count);
        case StmtKind::IfStmt:
            return "if " + describeCond(static_cast<const IfStmt&>(s).cond);
        case StmtKind::Click: {
            const auto& c = static_cast<const ClickStmt&>(s);
            return "Click(" + (c.use_var ? c.target_var : fmtPoint(c.point)) + ")";
        }
        case StmtKind::PressKey:
            return "PressKey(\"" + static_cast<const PressKeyStmt&>(s).key + "\")";
        case StmtKind::KeyDown:
            return "KeyDown(\"" + static_cast<const KeyDownStmt&>(s).key + "\")";
        case StmtKind::KeyUp:
            return "KeyUp(\"" + static_cast<const KeyUpStmt&>(s).key + "\")";
        case StmtKind::MouseDown:
            return "MouseDown(" + static_cast<const MouseDownStmt&>(s).button + ")";
        case StmtKind::MouseUp:
            return "MouseUp(" + static_cast<const MouseUpStmt&>(s).button + ")";
        case StmtKind::MoveTo: {
            const auto& m = static_cast<const MoveToStmt&>(s);
            return "MoveTo(" + (m.use_var ? m.target_var : fmtPoint(m.point)) + ")";
        }
        case StmtKind::Scroll:
            return "Scroll(" + std::to_string(static_cast<const ScrollStmt&>(s).delta) + ")";
        case StmtKind::Wait:
            return "Wait(" + fmtNum(static_cast<const WaitStmt&>(s).mean_ms) + "ms)";
        case StmtKind::While:
            return "while " + describeCond(static_cast<const WhileStmt&>(s).cond);
        case StmtKind::RepeatUntil:
            return "repeat_until " + describeCond(static_cast<const RepeatUntilStmt&>(s).cond);
        case StmtKind::Break:
            return "break";
        case StmtKind::Log:
            return "Log(\"" + static_cast<const LogStmt&>(s).message + "\")";
        case StmtKind::RandomWait: {
            const auto& r = static_cast<const RandomWaitStmt&>(s);
            return "RandomWait(" + fmtNum(r.min_ms) + ", " + fmtNum(r.max_ms) + ")";
        }
        case StmtKind::WaitUntil: {
            const auto& w = static_cast<const WaitUntilStmt&>(s);
            return "WaitUntil(" + describeCond(w.cond) + ", timeout=" + fmtNum(w.timeout_ms) +
                   "ms)";
        }
        case StmtKind::Include:
            return "include \"" + static_cast<const IncludeStmt&>(s).path + "\"";
        case StmtKind::Call: {
            const auto& call = static_cast<const CallStmt&>(s);
            return "Call(\"" + call.name + "\", " + std::to_string(call.args.size()) + " args)";
        }
    }
    return "?";
}

DebugController::DebugController(bool pause_on_entry) :
    pause_next_(pause_on_entry), last_stamp_(std::chrono::steady_clock::now()) {}

void DebugController::onStatement(const Stmt& stmt,
                                  const std::unordered_map<std::string, Vec2>& vars,
                                  const std::set<std::string>& held_keys,
                                  const std::set<std::string>& held_buttons, int loop_iteration) {
    std::unique_lock<std::mutex> lock(mtx_);

    const auto now = std::chrono::steady_clock::now();
    HistoryEntry e;
    e.line = stmt.line;
    e.text = describeStmt(stmt);
    if (stmt.source_id != 0) e.text += "  (include)";
    e.elapsed_ms = std::chrono::duration<double, std::milli>(now - last_stamp_).count();
    last_stamp_ = now;
    history_.push_back(std::move(e));
    if (history_.size() > kMaxHistory) history_.pop_front();

    const bool should_pause = pause_next_ || mode_ == Mode::Step ||
                              (stmt.source_id == 0 && breakpoints_.count(stmt.line) > 0);
    pause_next_ = false;

    if (should_pause && !stop_) {
        snap_.state = State::Paused;
        snap_.current_line = stmt.line;
        snap_.current_stmt = describeStmt(stmt);
        snap_.vars.assign(vars.begin(), vars.end());
        snap_.held_inputs.assign(held_keys.begin(), held_keys.end());
        snap_.held_inputs.insert(snap_.held_inputs.end(), held_buttons.begin(), held_buttons.end());
        snap_.loop_iteration = loop_iteration;
        state_ = State::Paused;

        cv_.wait(lock, [this] { return unpause_ || stop_; });
        unpause_ = false;
        // Don't charge the paused time to the next statement.
        last_stamp_ = std::chrono::steady_clock::now();
        if (!stop_) {
            state_ = State::Running;
            snap_.state = State::Running;
        }
    }

    if (stop_) throw cancel::Cancelled();
}

void DebugController::finish() {
    std::lock_guard<std::mutex> lock(mtx_);
    state_ = State::Finished;
    snap_.state = State::Finished;
    snap_.current_line = 0;
    cv_.notify_all();
}

void DebugController::setBreakpoints(std::set<int> lines) {
    std::lock_guard<std::mutex> lock(mtx_);
    breakpoints_ = std::move(lines);
}

void DebugController::step() {
    std::lock_guard<std::mutex> lock(mtx_);
    mode_ = Mode::Step;
    unpause_ = true;
    cv_.notify_all();
}

void DebugController::resume() {
    std::lock_guard<std::mutex> lock(mtx_);
    mode_ = Mode::Continue;
    unpause_ = true;
    cv_.notify_all();
}

void DebugController::pause() {
    std::lock_guard<std::mutex> lock(mtx_);
    mode_ = Mode::Step;
    pause_next_ = true;
}

void DebugController::stop() {
    cancel::request();
    std::lock_guard<std::mutex> lock(mtx_);
    stop_ = true;
    cv_.notify_all();
}

DebugController::Snapshot DebugController::snapshot() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return snap_;
}

std::vector<DebugController::HistoryEntry> DebugController::history() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return {history_.begin(), history_.end()};
}

DebugController::State DebugController::state() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return state_;
}

} // namespace undek::script
