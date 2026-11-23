#include "script/Vm.h"

#include "app/AppPaths.h"
#include "app/FileLog.h"
#include "core/Cancel.h"
#include "core/Pause.h"
#include "screen/ScreenSampler.h"
#include "script/DebugController.h"
#include "script/PicturePath.h"
#include "vision/ScreenGrab.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <random>
#include <sstream>

namespace undek::script {
namespace {

// A script can produce numbers a double can't hold (1e300 * 1e300 and friends).
// Nothing downstream - coordinates, delays, loop counts - can use those.
double finite(double value) {
    if (!std::isfinite(value)) throw std::runtime_error("Number is out of range");
    return value;
}

} // namespace

Vm::Vm(const Program& program, HumanizationProfile profile, IInputBackend& backend, bool dry_run,
       LogCallback log_cb, DebugController* dbg) :
    program_(program), profile_(profile),
    rng_(profile, static_cast<uint32_t>(std::max(0, profile.random_seed))), paths_(profile, rng_),
    backend_(backend), scheduler_(backend, rng_, paths_), dry_run_(dry_run),
    log_ostream_(std::cout), log_cb_(log_cb), dbg_(dbg),
    grab_([](ImageFrame& out, std::string& error) { return ScreenGrab::capture(out, error); }) {
    FileLog::setEnabled(profile_.log_to_file);
    // Humanized moves, clicks and holds send input through the scheduler, so it
    // needs to report failures too instead of just doing them quietly.
    scheduler_.setInputReporter(
        [this](const InputResult& result, const std::string& what) { checkInput(result, what); });
    // A PressKey holds a key just like KeyDown does, so the debugger and the
    // cleanup after a failed run have to see it.
    scheduler_.setPressTracking(
        [this](const std::string& key, bool confirmed) {
            if (confirmed) {
                ++key_holders_[key];
                held_keys_.insert(key);
            } else {
                maybe_held_keys_.insert(key);
            }
        },
        [this](const std::string& key, bool claimed) {
            if (claimed)
                dropKeyOwner(key);
            else
                maybe_held_keys_.erase(key);
        });
    // A press whose key another statement still holds must not send the
    // release that would end that hold, and a press that failed must not give
    // up a claim it never took.
    scheduler_.setReleaseQuery([this](const std::string& key) {
        const auto it = key_holders_.find(key);
        return it == key_holders_.end() ? 0 : it->second;
    });
    // A click whose release never confirmed may have left the button down, and
    // cleanup has to know about it before the failure stops the run.
    scheduler_.setUncertainPress(
        [this](const std::string& button) { maybe_held_buttons_.insert(button); });
}

const ImageFrame* Vm::templateImage(const std::string& path, std::string& error) {
    // The checker uses the same resolver, so a picture it says is there is the
    // one this opens.
    const std::string file = resolvePicturePath(path, script_dir_);
    // Keyed by the resolved path, so "shot.bmp" and the full path don't end up
    // as two copies and two lookups.
    const std::string key = file.empty() ? AppPaths::resolve(path) : file;
    const auto cached = templates_.find(key);
    if (cached != templates_.end()) return &cached->second;
    if (missing_templates_.count(key)) {
        error = "template not found: " + path;
        return nullptr;
    }

    if (file.empty()) {
        missing_templates_.insert(key);
        error = "template not found: " + path;
        return nullptr;
    }

    // "the file isn't there" and "the file isn't a usable picture" are
    // different problems and the log should say which one it is.
    ImageFrame frame;
    std::string load_error;
    if (!loadBmp(file, frame, load_error)) {
        missing_templates_.insert(key);
        error = "invalid image: " + path + " (" + load_error + ")";
        return nullptr;
    }
    return &templates_.emplace(key, std::move(frame)).first->second;
}

std::optional<ImageMatch> Vm::findOnScreen(const std::string& path, int tolerance,
                                           const SearchArea& area, std::string& error,
                                           bool first_match) {
    const ImageFrame* tpl = templateImage(path, error);
    if (!tpl) return std::nullopt;

    // The frame is reused between checks; capturing a desktop used to allocate a
    // new pixel buffer every time.
    const auto now = std::chrono::steady_clock::now();
    const bool stale =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - frame_stamp_).count() >=
        kCaptureReuseMs;
    if (!frame_fresh_ || stale) {
        if (!grab_(screen_frame_, error)) return std::nullopt;
        frame_stamp_ = now;
        frame_fresh_ = true;
    }

    // A stop request or the runtime limit has to break a big scan too.
    auto hit = findImage(
        screen_frame_, *tpl, tolerance,
        [this] {
            cancel::throwIfRequested();
            checkDeadline();
        },
        first_match, area);
    if (!hit) {
        error = "not on screen";
        return hit;
    }
    // The capture starts at the virtual desktop's top-left, so shift the match
    // into real desktop coordinates before anyone clicks on it.
    hit->x = screen_frame_.desktopX(hit->x);
    hit->y = screen_frame_.desktopY(hit->y);
    return hit;
}

void Vm::pauseAwareSleep(double ms) {
    // Left unchecked, an infinite duration made the hop loop below subtract 25
    // ms from a value that never changed, so the run sat there until Stop. The
    // ceiling keeps every value here small enough for the subtraction to finish.
    if (!std::isfinite(ms) || ms > kMaxWaitMs) {
        throw std::runtime_error("Duration is past the 86400000 ms limit");
    }
    // A long Wait used to ignore the pause hotkey until it was over. Sleeping in
    // short hops lets the pause land within one hop, and the rest of the wait is
    // still there to finish after the resume.
    constexpr double kHopMs = 25.0;
    double left = ms;
    while (left > 0.0) {
        const double piece = (std::min)(left, kHopMs);
        scheduler_.sleepMs(piece);
        left -= piece;
        checkDeadline();
    }
}

void Vm::log(const std::string& msg) {
    log_ostream_ << msg << "\n";
    FileLog::write(msg);
    if (log_cb_) {
        log_cb_(msg);
    }
}

void Vm::requirePixels(double value, const char* what) const {
    if (!pixelInRange(value))
        throw std::runtime_error(std::string(what) + " is out of range (limit is 1000000 px)");
}

int Vm::toPixels(double value, const char* what) const {
    requirePixels(value, what);
    return static_cast<int>(std::lround(value));
}

double Vm::evalExpr(const Expr& expr) {
    switch (expr.kind) {
        case Expr::Kind::Number:
            return finite(expr.number);
        case Expr::Kind::Variable: {
            const auto it = num_vars_.find(expr.name);
            if (it == num_vars_.end()) {
                throw std::runtime_error("Unknown variable: " + expr.name);
            }
            return finite(it->second);
        }
        case Expr::Kind::Negate:
            return finite(-evalExpr(*expr.lhs));
        case Expr::Kind::Add:
            return finite(evalExpr(*expr.lhs) + evalExpr(*expr.rhs));
        case Expr::Kind::Subtract:
            return finite(evalExpr(*expr.lhs) - evalExpr(*expr.rhs));
        case Expr::Kind::Multiply:
            return finite(evalExpr(*expr.lhs) * evalExpr(*expr.rhs));
        case Expr::Kind::Divide: {
            const double divisor = evalExpr(*expr.rhs);
            if (divisor == 0.0) throw std::runtime_error("Division by zero");
            return finite(evalExpr(*expr.lhs) / divisor);
        }
    }
    return 0.0;
}

double Vm::waitMeanMs(const WaitStmt& wait) {
    const double mean = wait.mean_expr ? evalExpr(*wait.mean_expr) : wait.mean_ms;
    // A literal is checked where it is parsed, but an expression can still add
    // up to something past the ceiling.
    if (!std::isfinite(mean) || mean > kMaxWaitMs) {
        throw std::runtime_error("Duration is past the 86400000 ms limit");
    }
    return mean;
}

Vec2 Vm::evalPoint(const PointExpr& point) {
    return {evalExpr(*point.x), evalExpr(*point.y)};
}

Vec2 Vm::resolvePoint(bool use_var, const std::string& name, const Vec2& point,
                      const PointExpr* expr) {
    if (expr) return evalPoint(*expr);
    if (!use_var) return point;
    auto it = vars_.find(name);
    if (it == vars_.end()) throw std::runtime_error("Unknown variable: " + name);
    return it->second;
}

Vec2 Vm::resolveTarget(const ClickStmt& stmt) {
    return resolvePoint(stmt.use_var, stmt.target_var, stmt.point, stmt.point_expr.get());
}

int Vm::loopCap() const {
    const int cap = std::max(1, profile_.max_loop_iterations);
    return dry_run_ ? std::min(cap, 5) : cap;
}

void Vm::checkLimits() {
    cancel::throwIfRequested();
    ++instruction_count_;
    if (profile_.max_total_instructions > 0 &&
        instruction_count_ > profile_.max_total_instructions) {
        throw std::runtime_error("Instruction budget reached (" +
                                 std::to_string(profile_.max_total_instructions) + ")");
    }
    checkDeadline();
    if (!dry_run_ && !profile_.require_window.empty() &&
        !WindowInfo::titleContains(profile_.require_window)) {
        throw std::runtime_error("Target window lost focus (expected title containing '" +
                                 profile_.require_window + "')");
    }
}

void Vm::checkDeadline() const {
    // Every statement and every row of a scan lands here, which is often enough
    // to pause where the user expects and cheap enough to leave in.
    pause::waitIfPaused();
    if (profile_.max_runtime_sec <= 0.0) return;
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time_).count();
    if (elapsed > profile_.max_runtime_sec) {
        throw std::runtime_error("Max runtime reached (" +
                                 std::to_string(profile_.max_runtime_sec) + "s)");
    }
}

bool Vm::evalCondition(const Condition& cond, bool quiet) {
    bool result = false;
    std::ostringstream oss;
    if (cond.type == Condition::Type::WindowTitle) {
        result = WindowInfo::titleContains(cond.text);
        oss << "[cond] WindowTitle(\"" << cond.text << "\") -> " << (result ? "true" : "false");
        if (!quiet) log(oss.str());
    } else if (cond.type == Condition::Type::Image) {
        std::string error;
        const auto found = findOnScreen(cond.image_path, cond.tolerance, cond.region, error, true);
        result = found.has_value();
        if (found) {
            oss << "[cond] ImageExists(\"" << cond.image_path << "\") found at (" << found->x
                << ", " << found->y << ") score " << std::fixed << std::setprecision(3)
                << found->score;
        } else {
            oss << "[cond] ImageExists(\"" << cond.image_path << "\") -> false (" << error << ")";
        }
        if (!quiet) log(oss.str());
    } else if (cond.has_color_check) {
        const auto c = pixel_sampler_(cond.pixel_x, cond.pixel_y);
        result =
            c.has_value() && ScreenSampler::colorMatches(*c, cond.expected_color, cond.tolerance);
        oss << "[cond] PixelAt(" << cond.pixel_x << "," << cond.pixel_y << ") = ";
        if (c)
            oss << "0x" << std::hex << *c << std::dec;
        else
            oss << "unreadable";
        oss << " expect 0x" << std::hex << cond.expected_color << std::dec;
        if (cond.tolerance > 0) oss << " tol " << cond.tolerance;
        oss << " -> " << (result ? "true" : "false");
        if (!quiet) log(oss.str());
    }
    return cond.negate ? !result : result;
}

bool Vm::stopsOnInputFailure() const {
    std::string policy = profile_.input_failure_policy;
    std::transform(policy.begin(), policy.end(), policy.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return policy != "continue";
}

void Vm::checkInput(const InputResult& result, const std::string& what, bool cleaning_up) {
    // Input is the one thing here that changes what's on screen, so the next look
    // has to be a fresh one.
    frame_fresh_ = false;
    if (result.ok) return;

    const std::string message = what + " failed: " + result.error;
    // Carrying on after a refused press is how a macro ends up running to the end
    // without ever having done the thing it was asked to do.
    if (cleaning_up || !stopsOnInputFailure()) {
        log("[input] " + message);
        return;
    }
    log("[input] " + message + " - stopping the macro");
    throw std::runtime_error(message);
}

bool Vm::lastKeyOwner(const std::string& key) const {
    const auto it = key_holders_.find(key);
    return it == key_holders_.end() || it->second <= 1;
}

void Vm::dropKeyOwner(const std::string& key) {
    const auto it = key_holders_.find(key);
    if (it != key_holders_.end() && --it->second <= 0) key_holders_.erase(it);
    if (key_holders_.count(key) == 0) held_keys_.erase(key);
    maybe_held_keys_.erase(key);
}

bool Vm::buttonHeld(const std::string& button) const {
    return button_holders_.count(button) > 0 || maybe_held_buttons_.count(button) > 0;
}

bool Vm::lastButtonOwner(const std::string& button) const {
    const auto it = button_holders_.find(button);
    return it == button_holders_.end() || it->second <= 1;
}

void Vm::dropButtonOwner(const std::string& button) {
    const auto it = button_holders_.find(button);
    if (it != button_holders_.end() && --it->second <= 0) button_holders_.erase(it);
    if (button_holders_.count(button) == 0) held_buttons_.erase(button);
    maybe_held_buttons_.erase(button);
}

void Vm::releaseHeldInputs() {
    if (!dry_run_) {
        // A key can be in both the confirmed and the uncertain set. One union
        // means each physical input gets exactly one release, so cleanup can't
        // hand the user a second key or button up.
        std::set<std::string> keys = held_keys_;
        keys.insert(maybe_held_keys_.begin(), maybe_held_keys_.end());
        std::set<std::string> buttons = held_buttons_;
        buttons.insert(maybe_held_buttons_.begin(), maybe_held_buttons_.end());

        // One stubborn release must not strand everything else that's held, and a
        // failed release during cleanup mustn't cut the cleanup short either.
        for (const auto& k : keys) {
            try {
                checkInput(releaseWithRetry(backend_, false, k), "keyUp(" + k + ")", true);
            } catch (const std::exception& ex) {
                log("[input] keyUp(" + k + ") threw: " + ex.what());
            }
        }
        for (const auto& b : buttons) {
            try {
                checkInput(releaseWithRetry(backend_, true, b), "mouseUp(" + b + ")", true);
            } catch (const std::exception& ex) {
                log("[input] mouseUp(" + b + ") threw: " + ex.what());
            }
        }
    }
    held_keys_.clear();
    key_holders_.clear();
    held_buttons_.clear();
    button_holders_.clear();
    maybe_held_keys_.clear();
    maybe_held_buttons_.clear();
    missed_keys_.clear();
    missed_buttons_.clear();
}

void Vm::execute(const Stmt& stmt) {
    if (dbg_) {
        dbg_->onStatement(stmt, vars_, held_keys_, held_buttons_, loop_iteration_);
    }
    checkLimits();
    switch (stmt.kind) {
        case StmtKind::Profile:
            profile_.delay_sigma_ms = static_cast<const ProfileStmt&>(stmt).default_sigma;
            rng_.setProfile(profile_);
            break;
        case StmtKind::VarAssign: {
            const auto& v = static_cast<const VarAssignStmt&>(stmt);
            std::ostringstream oss;
            if (v.is_number) {
                num_vars_[v.name] = evalExpr(*v.value);
                oss << "[var] " << v.name << " = " << num_vars_[v.name];
            } else if (v.is_image) {
                std::string error;
                const auto found =
                    findOnScreen(v.image_path, v.image_tolerance, v.image_region, error);
                if (!found) {
                    throw std::runtime_error("FindImage: " + error + " (" + v.image_path + ")");
                }
                const Vec2 p{static_cast<double>(found->centerX()),
                             static_cast<double>(found->centerY())};
                vars_[v.name] = p;
                oss << "[var] " << v.name << " = (" << p.x << ", " << p.y << ") from image \""
                    << v.image_path << "\" score " << std::fixed << std::setprecision(3)
                    << found->score;
            } else {
                const Vec2 p = v.point_expr ? evalPoint(*v.point_expr) : v.point;
                vars_[v.name] = p;
                oss << "[var] " << v.name << " = (" << p.x << ", " << p.y << ")";
            }
            log(oss.str());
            break;
        }
        case StmtKind::Loop: {
            const auto& loop = static_cast<const LoopStmt&>(stmt);
            const int count = loop.count_expr
                                  ? static_cast<int>(std::lround(std::clamp(
                                        evalExpr(*loop.count_expr), -kMaxLoopCount, kMaxLoopCount)))
                                  : loop.count;
            const int cap = loopCap();
            const int total = (std::max)(0, (std::min)(count, cap));
            if (count > cap) {
                log("[loop] count " + std::to_string(count) + " capped at " + std::to_string(cap));
            }
            for (int i = 0; i < total; ++i) {
                loop_iteration_ = i;
                executeBlock(loop.body);
                if (break_) {
                    break_ = false;
                    break;
                }
                checkLimits();
            }
            break;
        }
        case StmtKind::While: {
            const auto& w = static_cast<const WhileStmt&>(stmt);
            const int cap = loopCap();
            int iter = 0;
            while (evalCondition(w.cond)) {
                // Checked before the counter moves: with the largest accepted
                // cap, incrementing first would overflow the int.
                if (iter >= cap) {
                    log("[while] iteration cap reached (" + std::to_string(cap) + ")");
                    break;
                }
                ++iter;
                const auto iter_start = std::chrono::steady_clock::now();
                executeBlock(w.body);
                if (break_) {
                    break_ = false;
                    break;
                }
                checkLimits();
                // A body with no waits would otherwise spin at full speed.
                const double iter_ms = std::chrono::duration<double, std::milli>(
                                           std::chrono::steady_clock::now() - iter_start)
                                           .count();
                if (iter_ms < 1.0) scheduler_.sleepMs(1.0);
            }
            break;
        }
        case StmtKind::RepeatUntil: {
            const auto& r = static_cast<const RepeatUntilStmt&>(stmt);
            const int cap = loopCap();
            int iter = 0;
            for (;;) {
                const auto iter_start = std::chrono::steady_clock::now();
                executeBlock(r.body);
                if (break_) {
                    break_ = false;
                    break;
                }
                checkLimits();
                const double iter_ms = std::chrono::duration<double, std::milli>(
                                           std::chrono::steady_clock::now() - iter_start)
                                           .count();
                if (iter_ms < 1.0) scheduler_.sleepMs(1.0);
                ++iter;
                if (evalCondition(r.cond)) break;
                if (iter >= cap) {
                    log("[repeat_until] iteration cap reached (" + std::to_string(cap) + ")");
                    break;
                }
            }
            break;
        }
        case StmtKind::Break:
            log("[break]");
            break_ = true;
            break;
        case StmtKind::Log:
            log("[log] " + static_cast<const LogStmt&>(stmt).message);
            break;
        case StmtKind::Call: {
            const auto& call = static_cast<const CallStmt&>(stmt);
            FunctionRegistry& registry = functions_ ? *functions_ : FunctionRegistry::global();
            // Both halves come from one lookup, so a registration changing on
            // another thread can't hand this call a function and a declaration
            // that belong to different entries.
            FunctionInfo declared;
            ContextScriptFunction fn;
            if (!registry.findCall(call.name, declared, fn)) {
                throw std::runtime_error("Call: no script function named '" + call.name + "'");
            }

            std::vector<ScriptValue> args;
            args.reserve(call.args.size());
            for (const auto& arg : call.args) {
                args.push_back(arg.is_string ? ScriptValue::str(arg.text)
                                             : ScriptValue::num(evalExpr(*arg.number)));
            }

            // The declared arguments are checked before the function runs, so
            // a plugin can't be handed the wrong shape of call.
            std::string mismatch;
            if (!FunctionRegistry::checkSignature(declared, args, mismatch)) {
                log("[call] " + call.name + " failed: " + mismatch);
                break;
            }

            FunctionContext context;
            context.is_cancelled = [] {
                return cancel::requested();
            };
            context.elapsed_ms = [this] {
                return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                 start_time_)
                    .count();
            };
            context.remaining_ms = [this] {
                if (profile_.max_runtime_sec <= 0.0) return -1.0;
                const double elapsed = std::chrono::duration<double, std::milli>(
                                           std::chrono::steady_clock::now() - start_time_)
                                           .count();
                return (std::max)(0.0, profile_.max_runtime_sec * 1000.0 - elapsed);
            };
            context.log = [this](const std::string& message) {
                log("[call] " + message);
            };

            ScriptValue result;
            std::string error;
            bool ok = false;
            try {
                ok = fn(args, context, result, error);
            } catch (const std::exception& ex) {
                // A registered function is a guest here: its exception becomes a
                // failed call, not the end of the run.
                error = ex.what();
            } catch (...) {
                error = "threw something that isn't an exception with a message";
            }
            if (!ok) {
                log("[call] " + call.name +
                    " failed: " + (error.empty() ? std::string("no reason given") : error));
                break;
            }

            // A function that promised a number and handed back text is a bug
            // in the function, and the script gets told about it.
            if (!declared.returns.empty()) {
                const bool wanted_text = declared.returns == "text";
                const bool got_text = result.kind == ScriptValue::Kind::Text;
                if (wanted_text != got_text) {
                    log("[call] " + call.name + " failed: it declared " + declared.returns +
                        " but returned " + (got_text ? "text" : "a number"));
                    break;
                }
            }

            std::ostringstream oss;
            oss << "[call] " << call.name << " -> ";
            if (result.kind == ScriptValue::Kind::Text)
                oss << result.text;
            else
                oss << result.number;
            log(oss.str());
            break;
        }
        case StmtKind::Include: {
            const auto& inc = static_cast<const IncludeStmt&>(stmt);
            log("[include] " + inc.path);
            executeBlock(inc.body);
            break;
        }
        case StmtKind::WaitUntil: {
            const auto& w = static_cast<const WaitUntilStmt&>(stmt);
            // An expression can ask for a timeout no clock duration can hold.
            const double timeout = std::clamp(w.timeout_ms, 0.0, 1.0e9);
            const auto timeout_ms = static_cast<long long>(timeout);
            if (dry_run_) {
                const bool ok = evalCondition(w.cond);
                log(std::string("[wait_until] dry-run -> ") +
                    (ok ? "condition true" : "condition false"));
                break;
            }
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
            for (;;) {
                checkLimits();
                if (evalCondition(w.cond, true)) {
                    log("[wait_until] condition met");
                    break;
                }
                if (std::chrono::steady_clock::now() >= deadline) {
                    log("[wait_until] timeout (" + std::to_string(timeout_ms) + " ms)");
                    break;
                }
                scheduler_.sleepMs(30.0);
            }
            break;
        }
        case StmtKind::RandomWait: {
            const auto& rw = static_cast<const RandomWaitStmt&>(stmt);
            std::uniform_real_distribution<double> dist(rw.min_ms, rw.max_ms);
            const double delay = dist(rng_.rng());
            std::ostringstream oss;
            oss << "[random_wait] " << std::fixed << std::setprecision(1) << delay << " ms";
            log(oss.str());
            if (!dry_run_) pauseAwareSleep(delay);
            break;
        }
        case StmtKind::IfStmt: {
            const auto& iff = static_cast<const IfStmt&>(stmt);
            if (evalCondition(iff.cond))
                executeBlock(iff.then_branch);
            else
                executeBlock(iff.else_branch);
            break;
        }
        case StmtKind::Click: {
            const auto& click = static_cast<const ClickStmt&>(stmt);
            const Vec2 target = resolveTarget(click);
            // humanize=false is the escape hatch: no curve, no spread, and no
            // background cursor noise either, or the point wouldn't be the point.
            const Vec2 noise = click.humanize ? rng_.sampleNoiseOffset() : Vec2{};
            const Vec2 noisy = target + noise;
            // The humanized path never passes through toPixels, so the bound has
            // to be checked here or a curve would be planned to nowhere.
            requirePixels(noisy.x, "x");
            requirePixels(noisy.y, "y");
            std::ostringstream oss;
            oss << "[click] target (" << noisy.x << ", " << noisy.y
                << ") humanize=" << click.humanize << " radius=" << click.radius;
            log(oss.str());
            if (!dry_run_) {
                // A click is a press and a release of its own. While another
                // statement holds the left button, its release would end that
                // hold, so the click waits and says so.
                if (buttonHeld("left")) {
                    log("[click] skipped (the left button is held by another statement)");
                    break;
                }
                if (click.humanize) {
                    const auto outcome =
                        scheduler_.clickAt(noisy, click.radius, click.box_half_w, click.box_half_h);
                    if (outcome == ActionScheduler::ClickOutcome::MoveFailed)
                        log("[input] click skipped (the cursor didn't get there)");
                    else if (outcome == ActionScheduler::ClickOutcome::TargetOutOfRange)
                        log("[input] click skipped (the point would land out of range)");
                } else {
                    const InputResult moved =
                        backend_.moveAbsolute(toPixels(noisy.x, "x"), toPixels(noisy.y, "y"));
                    if (!moved.ok) {
                        checkInput(moved, "move to click target");
                        log("[input] click skipped (the cursor didn't get there)");
                    } else {
                        const InputResult clicked = backend_.clickLeft();
                        // A press whose release didn't land keeps the button on
                        // the cleanup list, so it is let go of on the way out.
                        if (clicked.ok)
                            maybe_held_buttons_.erase("left");
                        else if (clicked.maybe_down)
                            maybe_held_buttons_.insert("left");
                        checkInput(clicked, "click");
                    }
                }
            }
            break;
        }
        case StmtKind::PressKey: {
            const auto& pk = static_cast<const PressKeyStmt&>(stmt);
            if (!backend_.supportsKey(pk.key)) {
                log("[key] " + pk.key + " (unsupported key name on this layout/backend - skipped)");
                break;
            }
            if (!force_no_miss_ && held_keys_.find(pk.key) == held_keys_.end() &&
                rng_.shouldMissInput()) {
                log("[key] " + pk.key + " (missed - simulated double input skip)");
                break;
            }
            std::ostringstream oss;
            oss << "[key] " << pk.key << " hold " << pk.hold_min << "-" << pk.hold_max
                << " humanize=" << pk.humanize;
            log(oss.str());
            if (!dry_run_) {
                scheduler_.pressKey(pk.key, pk.hold_min, pk.hold_max, pk.humanize);
            }
            break;
        }
        case StmtKind::KeyDown: {
            const auto& kd = static_cast<const KeyDownStmt&>(stmt);
            if (!backend_.supportsKey(kd.key)) {
                log("[keydown] " + kd.key +
                    " (unsupported key name on this layout/backend - skipped)");
                break;
            }
            if (!force_no_miss_ && held_keys_.find(kd.key) == held_keys_.end() &&
                rng_.shouldMissInput()) {
                missed_keys_.insert(kd.key);
                log("[keydown] " + kd.key + " (missed - simulated input skip)");
                break;
            }
            missed_keys_.erase(kd.key);
            log("[keydown] " + kd.key);
            if (dry_run_) {
                ++key_holders_[kd.key];
                held_keys_.insert(kd.key);
                break;
            }
            const InputResult down = backend_.keyDown(kd.key);
            // Only a press the backend confirms counts as held, but a refused one
            // can still have landed halfway, so it is released on the way out.
            if (down.ok) {
                ++key_holders_[kd.key];
                held_keys_.insert(kd.key);
            } else {
                maybe_held_keys_.insert(kd.key);
            }
            checkInput(down, "keyDown(" + kd.key + ")");
            break;
        }
        case StmtKind::KeyUp: {
            const auto& ku = static_cast<const KeyUpStmt&>(stmt);
            if (!backend_.supportsKey(ku.key)) {
                log("[keyup] " + ku.key +
                    " (unsupported key name on this layout/backend - skipped)");
                break;
            }
            if (missed_keys_.erase(ku.key) > 0 && held_keys_.find(ku.key) == held_keys_.end()) {
                log("[keyup] " + ku.key + " (skipped - paired with missed keydown)");
                break;
            }
            if (dry_run_) {
                log("[keyup] " + ku.key);
                dropKeyOwner(ku.key);
                break;
            }
            const bool last = lastKeyOwner(ku.key);
            log("[keyup] " + ku.key + (last ? "" : " (another statement still holds it)"));
            // Another claim on the key is still there, so this statement only
            // gives up its own and the key stays down for the other.
            if (!last) {
                dropKeyOwner(ku.key);
                break;
            }
            const InputResult up = releaseWithRetry(backend_, false, ku.key);
            // A release that didn't land leaves the key down, so cleanup keeps
            // trying instead of pretending it's gone.
            if (up.ok) dropKeyOwner(ku.key);
            checkInput(up, "keyUp(" + ku.key + ")");
            break;
        }
        case StmtKind::MouseDown: {
            const auto& md = static_cast<const MouseDownStmt&>(stmt);
            if (!force_no_miss_ && held_buttons_.find(md.button) == held_buttons_.end() &&
                rng_.shouldMissInput()) {
                missed_buttons_.insert(md.button);
                log("[mousedown] " + md.button + " (missed - simulated input skip)");
                break;
            }
            missed_buttons_.erase(md.button);
            log("[mousedown] " + md.button);
            if (dry_run_) {
                ++button_holders_[md.button];
                held_buttons_.insert(md.button);
                break;
            }
            const InputResult down = backend_.mouseDown(md.button);
            if (down.ok) {
                ++button_holders_[md.button];
                held_buttons_.insert(md.button);
            } else {
                maybe_held_buttons_.insert(md.button);
            }
            checkInput(down, "mouseDown(" + md.button + ")");
            break;
        }
        case StmtKind::MouseUp: {
            const auto& mu = static_cast<const MouseUpStmt&>(stmt);
            if (missed_buttons_.erase(mu.button) > 0 &&
                held_buttons_.find(mu.button) == held_buttons_.end()) {
                log("[mouseup] " + mu.button + " (skipped - paired with missed mousedown)");
                break;
            }
            if (dry_run_) {
                log("[mouseup] " + mu.button);
                dropButtonOwner(mu.button);
                break;
            }
            const bool last = lastButtonOwner(mu.button);
            log("[mouseup] " + mu.button + (last ? "" : " (another statement still holds it)"));
            // The button stays down for the other claim, which releases it.
            if (!last) {
                dropButtonOwner(mu.button);
                break;
            }
            const InputResult up = releaseWithRetry(backend_, true, mu.button);
            if (up.ok) dropButtonOwner(mu.button);
            checkInput(up, "mouseUp(" + mu.button + ")");
            break;
        }
        case StmtKind::MoveTo: {
            const auto& m = static_cast<const MoveToStmt&>(stmt);
            const Vec2 target = resolvePoint(m.use_var, m.target_var, m.point, m.point_expr.get());
            // Checked before the branch, because the humanized one doesn't go
            // through toPixels.
            requirePixels(target.x, "x");
            requirePixels(target.y, "y");
            std::ostringstream oss;
            oss << "[move] target (" << target.x << ", " << target.y << ") humanize=" << m.humanize;
            log(oss.str());
            if (!dry_run_) {
                if (m.humanize) {
                    if (!scheduler_.moveToHumanized(target))
                        log("[input] move stopped before reaching the target");
                } else {
                    checkInput(
                        backend_.moveAbsolute(toPixels(target.x, "x"), toPixels(target.y, "y")),
                        "move");
                }
            }
            break;
        }
        case StmtKind::Scroll: {
            const auto& s = static_cast<const ScrollStmt&>(stmt);
            const double wheel = s.delta_expr ? evalExpr(*s.delta_expr) : s.delta;
            const int delta =
                static_cast<int>(std::lround(std::clamp(wheel, -1000000.0, 1000000.0)));
            log("[scroll] " + std::to_string(delta));
            if (!dry_run_) checkInput(backend_.mouseWheel(delta), "scroll");
            break;
        }
        case StmtKind::Wait: {
            const auto& w = static_cast<const WaitStmt&>(stmt);
            const bool is_holding = !held_keys_.empty() || !held_buttons_.empty();
            const double mean = waitMeanMs(w);
            double delay = 0.0;
            double stall_added = 0.0;
            bool panic_hold = false;
            if (is_holding) {
                delay =
                    rng_.sampleHoldDelayMs(mean, profile_.delay_sigma_ms, w.humanize, &panic_hold);
            } else {
                delay = rng_.sampleTransitDelayMs(mean, profile_.delay_sigma_ms, w.humanize,
                                                  &stall_added);
            }
            // A hold can sample well below the time the script asked for, and a
            // press shorter than a frame is one the target may never see.
            bool floored = false;
            const double press_floor = std::clamp(profile_.min_press_ms, 0.0, 1000.0);
            if (is_holding && w.humanize && delay < press_floor) {
                delay = press_floor;
                floored = true;
            }
            // Humanization can sample above the mean, and the sleep refuses a
            // duration past the ceiling, so a wait at the limit stays a wait.
            delay = (std::min)(delay, kMaxWaitMs);
            std::ostringstream oss;
            oss << "[wait] " << std::fixed << std::setprecision(1) << delay << " ms";
            if (stall_added > 0.0) {
                oss << " (micro-stall +" << std::fixed << std::setprecision(1) << stall_added
                    << " ms)";
            } else if (panic_hold) {
                oss << " (hold-stretch)";
            } else if (floored) {
                oss << " (press floor)";
            }
            log(oss.str());
            if (!dry_run_) pauseAwareSleep(delay);
            break;
        }
    }
}

void Vm::executeBlock(const std::vector<std::unique_ptr<Stmt>>& block) {
    for (size_t i = 0; i < block.size(); ++i) {
        const Stmt& cur = *block[i];

        if (cur.kind == StmtKind::KeyUp) {
            const auto& ku = static_cast<const KeyUpStmt&>(cur);
            const bool key_is_held = (held_keys_.find(ku.key) != held_keys_.end()) &&
                                     (missed_keys_.find(ku.key) == missed_keys_.end());
            if (key_is_held && i + 1 < block.size()) {
                size_t next_action_idx = 0;
                const WaitStmt* mid_wait = nullptr;

                auto isOverlapTarget = [&](const Stmt& cand) -> bool {
                    if (cand.kind == StmtKind::MouseDown) return true;
                    if (cand.kind == StmtKind::KeyDown) {
                        const auto& next_kd = static_cast<const KeyDownStmt&>(cand);
                        return next_kd.key != ku.key;
                    }
                    return false;
                };

                if (isOverlapTarget(*block[i + 1])) {
                    next_action_idx = i + 1;
                } else if (block[i + 1]->kind == StmtKind::Wait && i + 2 < block.size() &&
                           isOverlapTarget(*block[i + 2])) {
                    mid_wait = static_cast<const WaitStmt*>(block[i + 1].get());
                    next_action_idx = i + 2;
                }

                if (next_action_idx > 0 && rng_.shouldOverlapInput()) {
                    if (mid_wait) {
                        const double pre_delay =
                            rng_.sampleDelayMs(waitMeanMs(*mid_wait) * 0.4,
                                               profile_.delay_sigma_ms * 0.4, mid_wait->humanize);
                        std::ostringstream woss;
                        woss << "[wait] " << std::fixed << std::setprecision(1) << pre_delay
                             << " ms (pre-overlap)";
                        log(woss.str());
                        if (!dry_run_) scheduler_.sleepMs(pre_delay);
                    }

                    const double overlap_ms = rng_.sampleOverlapDelayMs();
                    std::ostringstream ooss;
                    ooss << "[overlap] Finger overlap before KeyUp(" << ku.key << ") ("
                         << std::fixed << std::setprecision(1) << overlap_ms << " ms)";
                    log(ooss.str());

                    force_no_miss_ = true;
                    execute(*block[next_action_idx]);
                    force_no_miss_ = false;

                    if (!dry_run_) scheduler_.sleepMs(overlap_ms);
                    execute(cur);

                    i = next_action_idx;
                    if (break_) return;
                    continue;
                }
            }
        }

        execute(cur);
        if (break_) return;
    }
}

void Vm::run() {
    rng_.resetSessionClock();
    start_time_ = std::chrono::steady_clock::now();
    if (profile_.max_runtime_sec > 0.0) {
        const auto limit =
            std::chrono::milliseconds(static_cast<long long>(profile_.max_runtime_sec * 1000.0));
        scheduler_.setDeadline(start_time_ + limit);
    } else {
        scheduler_.setDeadline(std::chrono::steady_clock::time_point::max());
    }
    struct FinishGuard {
        DebugController* dbg;
        ~FinishGuard() {
            if (dbg) dbg->finish();
        }
    } finish_guard{dbg_};
    try {
        executeBlock(program_.statements);
    } catch (...) {
        releaseHeldInputs();
        throw;
    }
    releaseHeldInputs();
}

} // namespace undek::script
