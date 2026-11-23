#pragma once

#include "core/HumanizationProfile.h"
#include "core/HumanizationSession.h"
#include "core/Vec2.h"

#include <chrono>
#include <random>

namespace undek {

class RandomizationEngine {
public:
    // Without an explicit session each engine owns one, so a preview engine can't
    // seed the fatigue cycle of a real run.
    explicit RandomizationEngine(const HumanizationProfile& profile, uint32_t seed = 0,
                                 HumanizationSession* session = nullptr);

    void setProfile(const HumanizationProfile& profile);
    void resetSessionClock();

    double sampleDelayMs(double mean_ms, double sigma_ms, bool humanize, double min_ms = -1,
                         double max_ms = -1);

    double sampleTransitDelayMs(double mean_ms, double sigma_ms, bool humanize,
                                double* stall_added_out = nullptr);

    double sampleHoldDelayMs(double mean_ms, double sigma_ms, bool humanize,
                             bool* panic_hold_out = nullptr);

    double sampleHoldMs(double hold_min, double hold_max, bool humanize);

    bool shouldMissInput();
    bool shouldOverlapInput();
    double sampleOverlapDelayMs();

    bool checkFatigueCycle();
    void advanceFatigueCycle();

    double fatigueDelayMultiplier() const { return fatigue_delay_mult_; }
    double fatigueSigmaMultiplier() const { return fatigue_sigma_mult_; }

    Vec2 samplePointInDisk(const Vec2& center, double radius);
    Vec2 samplePointInBox(const Vec2& center, double half_w, double half_h);

    double sessionDriftFactor() const;
    Vec2 sampleNoiseOffset();

    std::mt19937& rng() { return rng_; }

private:
    double clampDelay(double ms, double min_override, double max_override) const;
    void sampleNextCycleDuration();

    HumanizationProfile profile_;
    HumanizationSession owned_session_;
    HumanizationSession& session_;
    std::mt19937 rng_;
    std::normal_distribution<double> normal_;
    std::uniform_real_distribution<double> unit_;
    std::chrono::steady_clock::time_point session_start_;
    std::chrono::steady_clock::time_point cycle_start_;
    double current_cycle_duration_sec_ = 150.0;
    double fatigue_delay_mult_ = 1.0;
    double fatigue_sigma_mult_ = 1.0;
    double hold_session_scale_ = 1.0;
    int action_count_ = 0;
    int cycle_action_count_ = 0;
};

} // namespace undek
