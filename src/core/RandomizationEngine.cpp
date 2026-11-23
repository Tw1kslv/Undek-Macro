#include "core/RandomizationEngine.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace undek {

RandomizationEngine::RandomizationEngine(const HumanizationProfile& profile, uint32_t seed,
                                         HumanizationSession* session) :
    profile_(profile), session_(session ? *session : owned_session_),
    rng_(seed != 0 ? seed : std::random_device{}()), normal_(0.0, 1.0), unit_(0.0, 1.0) {
    resetSessionClock();
}

void RandomizationEngine::setProfile(const HumanizationProfile& profile) {
    profile_ = profile;
}

void RandomizationEngine::sampleNextCycleDuration() {
    double lo = std::max(1.0, profile_.fatigue_cycle_min_sec);
    double hi = std::max(lo, profile_.fatigue_cycle_max_sec);
    std::uniform_real_distribution<double> dist(lo, hi);
    current_cycle_duration_sec_ = dist(rng_);
}

void RandomizationEngine::resetSessionClock() {
    const auto now = std::chrono::steady_clock::now();
    session_start_ = now;
    action_count_ = 0;

    if (profile_.hold_variability_enabled) {
        double lo = std::min(profile_.hold_scale_min, profile_.hold_scale_max);
        double hi = std::max(profile_.hold_scale_min, profile_.hold_scale_max);
        std::uniform_real_distribution<double> hold_dist(lo, hi);
        hold_session_scale_ = hold_dist(rng_);
    } else {
        hold_session_scale_ = 1.0;
    }

    const auto gs = session_.ensureInitialized([&](HumanizationSession::CycleState& s) {
        s.cycle_start = now;
        sampleNextCycleDuration();
        s.cycle_duration_sec = current_cycle_duration_sec_;
        s.fatigue_delay_mult = 1.0;
        s.fatigue_sigma_mult = 1.0;
        s.cycle_actions = 0;
    });
    cycle_start_ = gs.cycle_start;
    current_cycle_duration_sec_ = gs.cycle_duration_sec;
    fatigue_delay_mult_ = gs.fatigue_delay_mult;
    fatigue_sigma_mult_ = gs.fatigue_sigma_mult;
    cycle_action_count_ = gs.cycle_actions;
}

void RandomizationEngine::advanceFatigueCycle() {
    const auto now = std::chrono::steady_clock::now();
    cycle_start_ = now;
    cycle_action_count_ = 0;
    sampleNextCycleDuration();

    if (!profile_.fatigue_focus_enabled) {
        fatigue_delay_mult_ = 1.0;
        fatigue_sigma_mult_ = 1.0;
        return;
    }

    const double shift_lo =
        std::clamp(std::min(profile_.fatigue_shift_min, profile_.fatigue_shift_max), 0.0, 0.90);
    const double shift_hi = std::clamp(
        std::max(profile_.fatigue_shift_min, profile_.fatigue_shift_max), shift_lo, 0.90);
    std::uniform_real_distribution<double> shift_dist(shift_lo, shift_hi);
    const double shift = shift_dist(rng_);
    const double sign = (unit_(rng_) < 0.5) ? -1.0 : 1.0;
    fatigue_delay_mult_ = 1.0 + sign * shift;

    const double widen_lo =
        std::max(1.0, std::min(profile_.fatigue_sigma_widen_min, profile_.fatigue_sigma_widen_max));
    const double widen_hi = std::max(
        widen_lo, std::max(profile_.fatigue_sigma_widen_min, profile_.fatigue_sigma_widen_max));
    std::uniform_real_distribution<double> widen_dist(widen_lo, widen_hi);
    fatigue_sigma_mult_ = widen_dist(rng_);

    if (profile_.hold_variability_enabled) {
        double h_lo = std::min(profile_.hold_scale_min, profile_.hold_scale_max);
        double h_hi = std::max(profile_.hold_scale_min, profile_.hold_scale_max);
        std::uniform_real_distribution<double> hold_dist(h_lo, h_hi);
        hold_session_scale_ = hold_dist(rng_);
    }

    session_.update([&](HumanizationSession::CycleState& s) {
        s.cycle_start = cycle_start_;
        s.cycle_duration_sec = current_cycle_duration_sec_;
        s.fatigue_delay_mult = fatigue_delay_mult_;
        s.fatigue_sigma_mult = fatigue_sigma_mult_;
        s.cycle_actions = 0;
    });
}

bool RandomizationEngine::checkFatigueCycle() {
    if (!profile_.fatigue_focus_enabled) {
        fatigue_delay_mult_ = 1.0;
        fatigue_sigma_mult_ = 1.0;
        return false;
    }

    ++cycle_action_count_;
    const auto gs = session_.bumpActions();
    cycle_action_count_ = gs.cycle_actions;
    cycle_start_ = gs.cycle_start;
    current_cycle_duration_sec_ = gs.cycle_duration_sec;
    // Stay in step with whatever cycle the shared session is on.
    fatigue_delay_mult_ = gs.fatigue_delay_mult;
    fatigue_sigma_mult_ = gs.fatigue_sigma_mult;

    const auto now = std::chrono::steady_clock::now();
    const double elapsed_sec = std::chrono::duration<double>(now - cycle_start_).count();
    const bool time_due = elapsed_sec >= current_cycle_duration_sec_;
    const bool actions_due = (profile_.fatigue_cycle_actions > 0) &&
                             (cycle_action_count_ >= profile_.fatigue_cycle_actions);
    if (time_due || actions_due) {
        advanceFatigueCycle();
        return true;
    }
    return false;
}

double RandomizationEngine::clampDelay(double ms, double min_override, double max_override) const {
    const double lo = (min_override >= 0) ? min_override : profile_.delay_min_ms;
    const double hi = (max_override >= 0) ? max_override : profile_.delay_max_ms;
    return std::clamp(ms, lo, hi);
}

double RandomizationEngine::sampleDelayMs(double mean_ms, double sigma_ms, bool humanize,
                                          double min_ms, double max_ms) {
    if (!humanize) {
        double ms = std::max(0.0, mean_ms);
        if (min_ms >= 0) ms = std::max(ms, min_ms);
        if (max_ms >= 0) ms = std::min(ms, max_ms);
        return ms;
    }

    ++action_count_;
    if (profile_.fatigue_focus_enabled) {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed_sec = std::chrono::duration<double>(now - cycle_start_).count();
        if (elapsed_sec >= current_cycle_duration_sec_) {
            advanceFatigueCycle();
        }
    }

    const double delay_mult = profile_.fatigue_focus_enabled ? fatigue_delay_mult_ : 1.0;
    const double sigma_mult = profile_.fatigue_focus_enabled ? fatigue_sigma_mult_ : 1.0;

    double ms = mean_ms * delay_mult;
    const double base_sigma = (sigma_ms > 0) ? sigma_ms : profile_.delay_sigma_ms;
    const double sigma = base_sigma * sigma_mult;
    ms = ms + normal_(rng_) * sigma;
    const double soft_lo = (mean_ms * delay_mult) - 3.0 * sigma;
    const double soft_hi = (mean_ms * delay_mult) + 3.0 * sigma;
    ms = std::clamp(ms, soft_lo, soft_hi);
    ms *= sessionDriftFactor();
    return clampDelay(ms, min_ms, max_ms);
}

double RandomizationEngine::sampleTransitDelayMs(double mean_ms, double sigma_ms, bool humanize,
                                                 double* stall_added_out) {
    if (stall_added_out) *stall_added_out = 0.0;
    if (humanize) {
        checkFatigueCycle();
    }
    double ms = sampleDelayMs(mean_ms, sigma_ms, humanize);
    if (humanize && profile_.micro_stall_enabled && profile_.micro_stall_chance > 0.0) {
        if (unit_(rng_) < profile_.micro_stall_chance) {
            const double lo =
                std::max(0.0, std::min(profile_.micro_stall_min_ms, profile_.micro_stall_max_ms));
            const double hi =
                std::max(lo, std::max(profile_.micro_stall_min_ms, profile_.micro_stall_max_ms));
            std::uniform_real_distribution<double> stall_dist(lo, hi);
            const double stall = stall_dist(rng_);
            ms = clampDelay(ms + stall, -1, -1);
            if (stall_added_out) *stall_added_out = stall;
        }
    }
    return ms;
}

double RandomizationEngine::sampleHoldDelayMs(double mean_ms, double sigma_ms, bool humanize,
                                              bool* panic_hold_out) {
    if (panic_hold_out) *panic_hold_out = false;
    if (!humanize || !profile_.hold_variability_enabled) {
        return sampleDelayMs(mean_ms, sigma_ms, humanize);
    }

    ++action_count_;
    const double lo = std::min(profile_.hold_scale_min, profile_.hold_scale_max);
    const double hi = std::max(profile_.hold_scale_min, profile_.hold_scale_max);
    std::uniform_real_distribution<double> local_scale_dist(lo, hi);
    const double hold_mult = 0.5 * (hold_session_scale_ + local_scale_dist(rng_));

    const double base_sigma = (sigma_ms > 0) ? sigma_ms : profile_.delay_sigma_ms;
    const double hold_sigma = std::max(1.5, std::min(base_sigma * 0.75, mean_ms * 0.35));
    double ms = (mean_ms * hold_mult) + normal_(rng_) * hold_sigma;

    if (profile_.hold_panic_chance > 0.0 && unit_(rng_) < profile_.hold_panic_chance) {
        const double p_lo =
            std::max(1.0, std::min(profile_.hold_panic_mult_min, profile_.hold_panic_mult_max));
        const double p_hi =
            std::max(p_lo, std::max(profile_.hold_panic_mult_min, profile_.hold_panic_mult_max));
        std::uniform_real_distribution<double> panic_dist(p_lo, p_hi);
        ms *= panic_dist(rng_);
        if (panic_hold_out) *panic_hold_out = true;
    }

    return clampDelay(ms, -1, -1);
}

double RandomizationEngine::sampleHoldMs(double hold_min, double hold_max, bool humanize) {
    if (hold_min > hold_max) std::swap(hold_min, hold_max);
    if (!humanize) {
        std::uniform_real_distribution<double> dist(hold_min, hold_max);
        return dist(rng_);
    }
    const double mid = 0.5 * (hold_min + hold_max);
    const double span = hold_max - hold_min;
    const double sigma =
        (span / 6.0) * (profile_.fatigue_focus_enabled ? fatigue_sigma_mult_ : 1.0);
    double hold = mid + normal_(rng_) * sigma;
    hold = std::clamp(hold, hold_min, hold_max);

    if (profile_.hold_variability_enabled) {
        hold *= hold_session_scale_;
        if (profile_.hold_panic_chance > 0.0 && unit_(rng_) < profile_.hold_panic_chance) {
            const double p_lo =
                std::max(1.0, std::min(profile_.hold_panic_mult_min, profile_.hold_panic_mult_max));
            const double p_hi = std::max(
                p_lo, std::max(profile_.hold_panic_mult_min, profile_.hold_panic_mult_max));
            std::uniform_real_distribution<double> panic_dist(p_lo, p_hi);
            hold *= panic_dist(rng_);
        }
    } else {
        hold *= sessionDriftFactor();
    }
    // The parser's ceiling has to survive the scaling: a hold the script was
    // allowed to ask for stays within it whatever the profile does to it.
    const double floor_ms = std::clamp(profile_.delay_min_ms, 0.0, kMaxHoldMs);
    return std::clamp(hold, floor_ms, kMaxHoldMs);
}

bool RandomizationEngine::shouldMissInput() {
    if (!profile_.miss_input_enabled || profile_.miss_probability <= 0.0) return false;
    return unit_(rng_) < profile_.miss_probability;
}

bool RandomizationEngine::shouldOverlapInput() {
    if (!profile_.key_overlap_enabled || profile_.key_overlap_probability <= 0.0) return false;
    return unit_(rng_) < profile_.key_overlap_probability;
}

double RandomizationEngine::sampleOverlapDelayMs() {
    const double lo =
        std::max(1.0, std::min(profile_.key_overlap_min_ms, profile_.key_overlap_max_ms));
    const double hi =
        std::max(lo, std::max(profile_.key_overlap_min_ms, profile_.key_overlap_max_ms));
    std::uniform_real_distribution<double> dist(lo, hi);
    return dist(rng_);
}

Vec2 RandomizationEngine::samplePointInDisk(const Vec2& center, double radius) {
    if (radius <= 0) return center;
    const double u = unit_(rng_);
    const double v = unit_(rng_);
    const double r = radius * std::sqrt(u);
    const double theta = 2.0 * 3.14159265358979323846 * v;
    return {center.x + r * std::cos(theta), center.y + r * std::sin(theta)};
}

Vec2 RandomizationEngine::samplePointInBox(const Vec2& center, double half_w, double half_h) {
    std::uniform_real_distribution<double> dx(-half_w, half_w);
    std::uniform_real_distribution<double> dy(-half_h, half_h);
    return {center.x + dx(rng_), center.y + dy(rng_)};
}

double RandomizationEngine::sessionDriftFactor() const {
    if (!profile_.session_drift_enabled) return 1.0;
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - session_start_).count();
    const double period = std::max(1.0, profile_.session_drift_period_sec);
    const double phase = (elapsed / period) * 2.0 * 3.14159265358979323846;
    const double mid = 0.5 * (profile_.session_drift_min + profile_.session_drift_max);
    const double amp = 0.5 * (profile_.session_drift_max - profile_.session_drift_min);
    return mid + amp * std::sin(phase);
}

Vec2 RandomizationEngine::sampleNoiseOffset() {
    if (!profile_.noise_enabled) return {};
    if (profile_.noise_interval_actions <= 0) return {};
    if (action_count_ % profile_.noise_interval_actions != 0) return {};

    std::uniform_int_distribution<int> d(-profile_.noise_max_pixels, profile_.noise_max_pixels);
    return {static_cast<double>(d(rng_)), static_cast<double>(d(rng_))};
}

} // namespace undek
