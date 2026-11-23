#pragma once

#include "core/ActionScheduler.h"
#include "core/HumanizationProfile.h"
#include "core/PathGenerator.h"
#include "core/RandomizationEngine.h"
#include "input/IInputBackend.h"
#include "screen/ScreenSampler.h"
#include "script/Ast.h"
#include "script/FunctionRegistry.h"
#include "vision/Image.h"

#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <utility>
#include <optional>
#include <set>
#include <unordered_map>

namespace undek::script {

class DebugController;

using LogCallback = std::function<void(const std::string&)>;

class Vm {
public:
    Vm(const Program& program, HumanizationProfile profile, IInputBackend& backend,
       bool dry_run = false, LogCallback log_cb = nullptr, DebugController* dbg = nullptr);

    void run();

    // Tests swap this for a fake frame; the default grabs the real desktop.
    using ScreenGrabber = std::function<bool(ImageFrame&, std::string&)>;
    void setScreenGrabber(ScreenGrabber grab) { grab_ = std::move(grab); }

    // nullopt means the pixel couldn't be read, which is not the same answer as
    // black. Tests inject one so a condition doesn't depend on the desktop.
    using PixelSampler = std::function<std::optional<unsigned int>(int x, int y)>;
    void setPixelSampler(PixelSampler sampler) {
        if (sampler) pixel_sampler_ = std::move(sampler);
    }

    // Defaults to FunctionRegistry::global().
    void setFunctionRegistry(FunctionRegistry* registry) { functions_ = registry; }

    // The folder the script lives in, so a picture kept beside it is found the
    // same way the checker finds it. Empty means the app folder only.
    void setScriptDir(std::string dir) { script_dir_ = std::move(dir); }

private:
    void executeBlock(const std::vector<std::unique_ptr<Stmt>>& block);
    void execute(const Stmt& stmt);
    double evalExpr(const Expr& expr);
    int toPixels(double value, const char* what) const;
    void requirePixels(double value, const char* what) const;
    double waitMeanMs(const WaitStmt& wait);
    Vec2 evalPoint(const PointExpr& point);
    Vec2 resolvePoint(bool use_var, const std::string& name, const Vec2& point,
                      const PointExpr* expr);
    Vec2 resolveTarget(const ClickStmt& stmt);
    bool evalCondition(const Condition& cond, bool quiet = false);
    const ImageFrame* templateImage(const std::string& path, std::string& error);
    std::optional<ImageMatch> findOnScreen(const std::string& path, int tolerance,
                                           const SearchArea& area, std::string& error,
                                           bool first_match = false);
    void checkLimits();
    void checkDeadline() const;
    void pauseAwareSleep(double ms);
    // A failed input stops the macro unless the profile says to carry on.
    // Cleaning up passes true so a stubborn release can't cut the cleanup short.
    void checkInput(const InputResult& result, const std::string& what, bool cleaning_up = false);
    bool stopsOnInputFailure() const;
    int loopCap() const;
    void releaseHeldInputs();
    void log(const std::string& msg);
    // More than one statement can want the same key down. The count is how
    // many still do, and the physical release waits for the last of them.
    bool lastKeyOwner(const std::string& key) const;
    void dropKeyOwner(const std::string& key);
    // Buttons are claimed the same way, so a single release between two holds
    // behaves like the key version. A click refuses to run while any claim on
    // the left button is open.
    bool buttonHeld(const std::string& button) const;
    bool lastButtonOwner(const std::string& button) const;
    void dropButtonOwner(const std::string& button);

    const Program& program_;
    HumanizationProfile profile_;
    RandomizationEngine rng_;
    PathGenerator paths_;
    IInputBackend& backend_;
    ActionScheduler scheduler_;
    bool dry_run_;
    std::ostream& log_ostream_;
    LogCallback log_cb_;
    DebugController* dbg_ = nullptr;
    ScreenGrabber grab_;
    // Two checks in a row used to bit-blit the whole desktop twice, and a poll
    // loop with no wait could do it thousands of times a second. A capture is
    // good for a few milliseconds, and sending anything throws it away because
    // that is the thing that actually changes the screen.
    static constexpr int kCaptureReuseMs = 10;
    std::chrono::steady_clock::time_point frame_stamp_{};
    bool frame_fresh_ = false;
    PixelSampler pixel_sampler_ = [](int x, int y) {
        return ScreenSampler::pixelColor(x, y);
    };
    FunctionRegistry* functions_ = nullptr;
    std::string script_dir_;
    ImageFrame screen_frame_;
    std::unordered_map<std::string, ImageFrame> templates_;
    std::set<std::string> missing_templates_;

    std::unordered_map<std::string, Vec2> vars_;
    std::unordered_map<std::string, double> num_vars_;
    int loop_iteration_ = 0;
    bool break_ = false;
    std::chrono::steady_clock::time_point start_time_;
    long long instruction_count_ = 0;
    std::set<std::string> held_keys_;
    std::map<std::string, int> key_holders_;
    std::set<std::string> held_buttons_;
    std::map<std::string, int> button_holders_;
    // A refused press may still have landed halfway, so these are released on
    // the way out even though the script never counts them as held.
    std::set<std::string> maybe_held_keys_;
    std::set<std::string> maybe_held_buttons_;
    std::set<std::string> missed_keys_;
    std::set<std::string> missed_buttons_;
    bool force_no_miss_ = false;
};

} // namespace undek::script
