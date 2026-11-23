#include "input/RecorderTape.h"

#include <cmath>

namespace undek {

void RecorderTape::reset(long long now_ms) {
    actions_.clear();
    held_keys_.clear();
    recorded_keys_.clear();
    held_buttons_.clear();
    recorded_buttons_.clear();
    last_event_ = now_ms;
    last_move_x_ = 0;
    last_move_y_ = 0;
    last_move_at_ = now_ms;
    have_last_move_ = false;
    paused_ = false;
}

void RecorderTape::setPaused(bool paused) {
    paused_ = paused;
}

bool RecorderTape::paused() const {
    return paused_;
}

void RecorderTape::pushWait(long long now_ms) {
    const long long ms = now_ms - last_event_;
    last_event_ = now_ms;
    if (ms >= 8 && !actions_.empty())
        actions_.push_back("Wait(" + std::to_string(ms) + "ms, humanize=true)");
}

bool RecorderTape::nameHeld(const std::string& name) const {
    for (const auto& kv : held_keys_)
        if (kv.second == name) return true;
    return false;
}

bool RecorderTape::nameHeldElsewhere(const std::string& name, long long vk) const {
    for (const auto& kv : held_keys_)
        if (kv.first != vk && kv.second == name) return true;
    return false;
}

long long RecorderTape::holderOf(const std::string& name) const {
    for (const auto& kv : held_keys_)
        if (kv.second == name) return kv.first;
    return 0;
}

void RecorderTape::key(long long vk, const std::string& name, bool down, long long now_ms) {
    if (name.empty()) return;

    if (down) {
        held_keys_[vk] = name;
        // Both Shift keys (and the left/right Ctrl, Alt) share one name, so the
        // second one must not write a second KeyDown.
        if (!paused_ && !nameHeldElsewhere(name, vk)) {
            recorded_keys_[vk] = name;
            pushWait(now_ms);
            actions_.push_back("KeyDown(\"" + name + "\")");
        }
        return;
    }

    held_keys_.erase(vk);
    if (paused_) return;

    const auto it = recorded_keys_.find(vk);
    if (it == recorded_keys_.end()) return;
    const std::string released = it->second;
    recorded_keys_.erase(it);
    // The other one is still down, so this isn't a release: the key that is
    // still held takes over the recorded state and writes the KeyUp later.
    const long long heir = holderOf(released);
    if (heir != 0) {
        recorded_keys_[heir] = released;
        return;
    }
    pushWait(now_ms);
    actions_.push_back("KeyUp(\"" + released + "\")");
}

void RecorderTape::button(const std::string& name, bool down, long long now_ms) {
    if (down) {
        held_buttons_.insert(name);
        if (!paused_ && recorded_buttons_.insert(name).second) {
            pushWait(now_ms);
            actions_.push_back("MouseDown(\"" + name + "\")");
        }
        return;
    }

    held_buttons_.erase(name);
    if (paused_) return;
    if (recorded_buttons_.erase(name) > 0) {
        pushWait(now_ms);
        actions_.push_back("MouseUp(\"" + name + "\")");
    }
}

void RecorderTape::move(int x, int y, long long now_ms) {
    if (paused_) return;

    // One move per ~20px or ~120ms of travel keeps the take readable.
    // Measured in floating point because the gap between the two ends of the
    // coordinate range squared doesn't fit in any integer.
    const double dx = static_cast<double>(x) - static_cast<double>(last_move_x_);
    const double dy = static_cast<double>(y) - static_cast<double>(last_move_y_);
    const double ms = static_cast<double>(now_ms - last_move_at_);
    if (have_last_move_ && std::hypot(dx, dy) < 20.0 && ms < 120.0) return;

    pushWait(now_ms);
    actions_.push_back("MoveTo(Point(" + std::to_string(x) + ", " + std::to_string(y) + "))");
    last_move_x_ = x;
    last_move_y_ = y;
    last_move_at_ = now_ms;
    have_last_move_ = true;
}

void RecorderTape::wheel(int delta, long long now_ms) {
    if (paused_ || delta == 0) return;
    pushWait(now_ms);
    actions_.push_back("Scroll(" + std::to_string(delta) + ")");
}

void RecorderTape::closeOut(bool keep_held, long long now_ms) {
    for (auto it = recorded_keys_.begin(); it != recorded_keys_.end();) {
        // A release that happened while paused: if another key with the same
        // name is still down, hand the recording over instead of closing it.
        if (held_keys_.count(it->first) == 0) {
            const long long heir = holderOf(it->second);
            if (heir != 0) {
                const std::string name = it->second;
                it = recorded_keys_.erase(it);
                recorded_keys_[heir] = name;
                continue;
            }
        } else if (keep_held) {
            ++it;
            continue;
        }
        pushWait(now_ms);
        actions_.push_back("KeyUp(\"" + it->second + "\")");
        it = recorded_keys_.erase(it);
    }

    for (auto it = recorded_buttons_.begin(); it != recorded_buttons_.end();) {
        if (keep_held && held_buttons_.count(*it) > 0) {
            ++it;
            continue;
        }
        pushWait(now_ms);
        actions_.push_back("MouseUp(\"" + *it + "\")");
        it = recorded_buttons_.erase(it);
    }
}

} // namespace undek
