#include "core/ActionScheduler.h"

#include "core/Cancel.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <chrono>
#include <thread>
#endif

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace undek {

InputResult releaseWithRetry(IInputBackend& backend, bool mouse, const std::string& name) {
    const InputResult first = mouse ? backend.mouseUp(name) : backend.keyUp(name);
    if (first.ok) return first;
    const InputResult again = mouse ? backend.mouseUp(name) : backend.keyUp(name);
    if (again.ok) return again;
    return InputResult::fail(first.error + "; retry failed: " + again.error);
}

ActionScheduler::ActionScheduler(IInputBackend& backend, RandomizationEngine& rng,
                                 PathGenerator& paths) :
    backend_(backend), rng_(rng), paths_(paths) {}

void ActionScheduler::setDeadline(std::chrono::steady_clock::time_point deadline) {
    deadline_ = deadline;
}

void ActionScheduler::report(const InputResult& result, const std::string& what) {
    if (result.ok || !report_) return;
    report_(result, what);
}

void ActionScheduler::sleepMs(double raw_ms) {
    // Written this way so a NaN falls out here too, and a wait past what a
    // clock conversion can hold is cut to the limit instead of wrapping.
    if (!(raw_ms > 0.0)) return;
    const double ms = (std::min)(raw_ms, kMaxWaitMs);
#ifdef _WIN32
    LARGE_INTEGER freq{}, start{}, now{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    const long long target = static_cast<long long>((ms / 1000.0) * freq.QuadPart);
    // Only the last couple of ms are spun on. A 20 ms margin meant every wait
    // burned a slice of a core doing nothing.
    const long long coarse_margin = static_cast<long long>(0.003 * freq.QuadPart);
    for (;;) {
        QueryPerformanceCounter(&now);
        const long long remaining = target - (now.QuadPart - start.QuadPart);
        if (remaining <= 0) break;
        cancel::throwIfRequested();
        if (std::chrono::steady_clock::now() >= deadline_) {
            throw std::runtime_error("Max runtime reached");
        }
        if (remaining > coarse_margin) Sleep(1);
    }
#else
    const auto end =
        std::chrono::steady_clock::now() + std::chrono::duration<double, std::milli>(ms);
    for (;;) {
        cancel::throwIfRequested();
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline_) throw std::runtime_error("Max runtime reached");
        if (now >= end) break;
        const auto chunk =
            (std::min)(std::chrono::duration_cast<std::chrono::milliseconds>(end - now),
                       std::chrono::milliseconds(5));
        std::this_thread::sleep_for(chunk);
    }
#endif
}

bool ActionScheduler::moveAlongPath(const std::vector<PathStep>& steps, bool use_relative) {
    for (const auto& step : steps) {
        // Second line of defence: a path that wandered off the coordinate range
        // would be rounded into an integer that can't hold it.
        if (!pixelInRange(step.position)) return false;

        if (use_relative) {
            const Vec2 cur = backend_.cursorPosition();
            const int dx = static_cast<int>(std::lround(step.position.x - cur.x));
            const int dy = static_cast<int>(std::lround(step.position.y - cur.y));
            if (dx != 0 || dy != 0) {
                const InputResult moved = backend_.moveRelative(dx, dy);
                if (!moved.ok) {
                    report(moved, "move along path");
                    return false;
                }
            }
        } else {
            const InputResult moved =
                backend_.moveAbsolute(static_cast<int>(std::lround(step.position.x)),
                                      static_cast<int>(std::lround(step.position.y)));
            if (!moved.ok) {
                report(moved, "move along path");
                return false;
            }
        }
        sleepMs(step.delay_ms);
    }
    return true;
}

bool ActionScheduler::moveToHumanized(const Vec2& target) {
    if (!pixelInRange(target)) return false;
    const Vec2 from = backend_.cursorPosition();
    return moveAlongPath(paths_.generatePath(from, target), true);
}

ActionScheduler::ClickOutcome ActionScheduler::clickAt(const Vec2& target, double radius,
                                                       double box_half_w, double box_half_h) {
    Vec2 point = target;
    if (radius > 0)
        point = rng_.samplePointInDisk(target, radius);
    else if (box_half_w > 0 || box_half_h > 0)
        point = rng_.samplePointInBox(target, box_half_w, box_half_h);

    // The spot the click was pushed to has to be somewhere it can actually go.
    if (!pixelInRange(point)) return ClickOutcome::TargetOutOfRange;
    if (!moveToHumanized(point)) return ClickOutcome::MoveFailed;
    sleepMs(rng_.sampleDelayMs(8.0, 3.0, true));
    const InputResult clicked = backend_.clickLeft();
    // Told before the failure is reported: stopping the macro throws, and by
    // then the caller needs the button on its cleanup list already.
    if (clicked.maybe_down && uncertain_press_) uncertain_press_("left");
    report(clicked, "click");
    return clicked.ok ? ClickOutcome::Ok : ClickOutcome::ClickFailed;
}

void ActionScheduler::pressKey(const std::string& key, double hold_min_ms, double hold_max_ms,
                               bool humanize) {
    const double hold = rng_.sampleHoldMs(hold_min_ms, hold_max_ms, humanize);
    // Read before the press: a failed press may only send a compensating release
    // when the key wasn't already someone else's.
    const int owners_before = release_query_ ? release_query_(key) : 0;
    const InputResult down = backend_.keyDown(key);
    // Noted before anything can throw, so a hold cut short, refused or left
    // unreleased is still something the caller's cleanup knows to let go of.
    if (pressed_) pressed_(key, down.ok);
    if (!down.ok) {
        // A press that landed while another claim was already there belongs to
        // that hold now: releasing here would end it. Otherwise let go before
        // reporting, because the press may have landed halfway even though the
        // backend refused it.
        if (owners_before == 0) {
            const InputResult up = releaseWithRetry(backend_, false, key);
            if (up.ok && released_) released_(key, false);
        }
        report(down, "keyDown(" + key + ")");
        return;
    }
    // The release belongs to whoever claimed the key first as much as to this
    // press: with another claim left, the key stays down and only this press's
    // claim goes away.
    auto letGo = [&]() {
        if (release_query_ && release_query_(key) > 1) {
            if (released_) released_(key, true);
            return InputResult::success();
        }
        const InputResult up = releaseWithRetry(backend_, false, key);
        if (up.ok && released_) released_(key, true);
        return up;
    };

    try {
        sleepMs(hold);
    } catch (...) {
        const InputResult up = letGo();
        try {
            report(up, "keyUp(" + key + ") after an interrupted hold");
        } catch (...) {
            // The stop that interrupted the hold is the one worth reporting.
        }
        throw;
    }
    const InputResult up = letGo();
    report(up, "keyUp(" + key + ")");
}

} // namespace undek
