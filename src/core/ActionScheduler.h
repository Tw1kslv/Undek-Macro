#pragma once

#include "core/PathGenerator.h"
#include "core/RandomizationEngine.h"
#include "input/IInputBackend.h"

#include <chrono>
#include <functional>
#include <string>

namespace undek {

class ActionScheduler {
public:
    ActionScheduler(IInputBackend& backend, RandomizationEngine& rng, PathGenerator& paths);

    void sleepMs(double ms);
    // A long wait stops here too, not just between statements.
    void setDeadline(std::chrono::steady_clock::time_point deadline);
    // These return false as soon as an input fails. A click is skipped when the
    // cursor never arrived, so a blocked move can't fire it somewhere else.
    // A path step that leaves the coordinate range stops the move instead of
    // being rounded into an integer it doesn't fit.
    bool moveAlongPath(const std::vector<PathStep>& steps, bool use_relative = true);
    bool moveToHumanized(const Vec2& target);

    enum class ClickOutcome { Ok, MoveFailed, ClickFailed, TargetOutOfRange };
    ClickOutcome clickAt(const Vec2& target, double radius = 0, double box_half_w = 0,
                         double box_half_h = 0);
    void pressKey(const std::string& key, double hold_min_ms, double hold_max_ms, bool humanize);

    // Called with every failed input the scheduler sends; also used for the
    // release attempt when a hold is cut short.
    using InputReporter = std::function<void(const InputResult&, const std::string&)>;
    void setInputReporter(InputReporter report) { report_ = std::move(report); }

    // A press reports the key as held the moment it is sent, confirmed telling
    // the caller which kind of hold it is, and reports it released only once the
    // backend agrees. A release that fails stays on the caller's list, so its
    // cleanup tries again instead of leaving the key down.
    // The release callback is told whether the press had a claim of its own: a
    // press that never confirmed has none, so it clears its "may be down" note
    // instead of giving up someone else's hold.
    using PressCallback = std::function<void(const std::string& key, bool confirmed)>;
    using ReleaseCallback = std::function<void(const std::string& key, bool claimed)>;
    void setPressTracking(PressCallback pressed, ReleaseCallback released) {
        pressed_ = std::move(pressed);
        released_ = std::move(released);
    }

    // A humanized click whose release never confirmed leaves a button that the
    // caller's cleanup has to try again, and it is told before the failure is
    // reported.
    using UncertainPressCallback = std::function<void(const std::string& button)>;
    void setUncertainPress(UncertainPressCallback cb) { uncertain_press_ = std::move(cb); }

    // How many statements currently claim the key. One claim or none means a
    // release from this press is the physical one; more means the hold belongs
    // to someone else too and the release waits for the last of them.
    using ReleaseQuery = std::function<int(const std::string& key)>;
    void setReleaseQuery(ReleaseQuery query) { release_query_ = std::move(query); }

private:
    void report(const InputResult& result, const std::string& what);

    IInputBackend& backend_;
    RandomizationEngine& rng_;
    PathGenerator& paths_;
    InputReporter report_;
    PressCallback pressed_;
    ReleaseCallback released_;
    UncertainPressCallback uncertain_press_;
    ReleaseQuery release_query_;
    std::chrono::steady_clock::time_point deadline_ = std::chrono::steady_clock::time_point::max();
};

// Letting go is the one thing that mustn't be left half-done: a stuck key types
// into whatever the user does next, so a failed release gets one retry.
InputResult releaseWithRetry(IInputBackend& backend, bool mouse, const std::string& name);

// No single wait is allowed past this. A script can produce a number a clock
// conversion can't hold, and 1e9 ms is already over eleven days.
constexpr double kMaxWaitMs = 1.0e9;

} // namespace undek
