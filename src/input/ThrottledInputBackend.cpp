#include "input/ThrottledInputBackend.h"

#include "core/Cancel.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <thread>
#endif

#include <algorithm>
#include <chrono>
#include <mutex>

namespace undek {
namespace {

struct SharedGate {
    std::mutex mutex;
    std::chrono::steady_clock::time_point last_send =
        std::chrono::steady_clock::now() - std::chrono::hours(1);
};

SharedGate& sharedGate() {
    static SharedGate g;
    return g;
}

} // namespace

ThrottledInputBackend::ThrottledInputBackend(std::unique_ptr<IInputBackend> inner,
                                             double min_gap_ms, double jitter_ms) :
    inner_(std::move(inner)), min_gap_ms_(std::max(0.0, min_gap_ms)),
    jitter_ms_(std::max(0.0, jitter_ms)), rng_(std::random_device{}()) {}

void ThrottledInputBackend::sleepForMs(double ms) const {
    if (ms <= 0) return;
#ifdef _WIN32
    LARGE_INTEGER freq{}, start{}, now{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    const long long target = static_cast<long long>((ms / 1000.0) * freq.QuadPart);
    const long long coarse_margin = static_cast<long long>(0.004 * freq.QuadPart);
    for (;;) {
        QueryPerformanceCounter(&now);
        const long long remaining = target - (now.QuadPart - start.QuadPart);
        if (remaining <= 0) break;
        cancel::throwIfRequested();
        if (remaining > coarse_margin) Sleep(1);
    }
#else
    std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(ms));
#endif
}

void ThrottledInputBackend::gate() {
    std::uniform_real_distribution<double> gap_dist(min_gap_ms_, min_gap_ms_ + jitter_ms_);
    const double gap = gap_dist(rng_);

    auto& g = sharedGate();
    std::lock_guard<std::mutex> lock(g.mutex);
    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g.last_send)
            .count();
    if (elapsed_ms < gap) sleepForMs(gap - elapsed_ms);
    g.last_send = std::chrono::steady_clock::now();
}

void ThrottledInputBackend::markSent() {
    auto& g = sharedGate();
    std::lock_guard<std::mutex> lock(g.mutex);
    g.last_send = std::chrono::steady_clock::now();
}

Vec2 ThrottledInputBackend::cursorPosition() const {
    return inner_->cursorPosition();
}

InputResult ThrottledInputBackend::moveAbsolute(int x, int y) {
    return inner_->moveAbsolute(x, y);
}

InputResult ThrottledInputBackend::moveRelative(int dx, int dy) {
    return inner_->moveRelative(dx, dy);
}

InputResult ThrottledInputBackend::clickLeft() {
    gate();
    const InputResult down = inner_->mouseDown("left");
    markSent();
    InputResult up = inner_->mouseUp("left");
    if (!up.ok) {
        // The press landed but the release didn't. This path used to skip the
        // retry the direct backend does, so a click could leave the button held.
        const InputResult retry = inner_->mouseUp("left");
        if (retry.ok)
            up = retry;
        else if (down.ok)
            // The press landed and neither release did, so the button may
            // still be down when this returns.
            up = InputResult::uncertain(up.error + "; retry failed: " + retry.error);
        else
            up = InputResult::fail(up.error + "; retry failed: " + retry.error);
    }
    return down.ok ? up : down;
}

InputResult ThrottledInputBackend::mouseDown(const std::string& button) {
    gate();
    return inner_->mouseDown(button);
}

InputResult ThrottledInputBackend::mouseUp(const std::string& button) {
    markSent();
    return inner_->mouseUp(button);
}

InputResult ThrottledInputBackend::mouseWheel(int delta) {
    gate();
    return inner_->mouseWheel(delta);
}

InputResult ThrottledInputBackend::keyDown(const std::string& key) {
    gate();
    return inner_->keyDown(key);
}

InputResult ThrottledInputBackend::keyUp(const std::string& key) {
    markSent();
    return inner_->keyUp(key);
}

bool ThrottledInputBackend::supportsKey(const std::string& key) const {
    return inner_->supportsKey(key);
}

} // namespace undek
