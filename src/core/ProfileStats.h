#pragma once

#include "core/HumanizationProfile.h"

#include <cstdint>
#include <string>

namespace undek {

// What a profile actually does to a typical wait, so the sliders aren't the
// only way to find out.
struct DelayStats {
    int samples = 0;
    double target_ms = 0.0;
    double min_ms = 0.0;
    double p10_ms = 0.0;
    double p50_ms = 0.0;
    double p90_ms = 0.0;
    double max_ms = 0.0;
    double mean_ms = 0.0;
};

DelayStats sampleDelays(const HumanizationProfile& profile, double target_ms = 40.0,
                        int samples = 400, uint32_t seed = 1);

std::string describeDelays(const DelayStats& stats);

} // namespace undek
