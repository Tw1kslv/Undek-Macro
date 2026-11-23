#include "core/ProfileStats.h"

#include "core/RandomizationEngine.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace undek {

namespace {

double percentile(const std::vector<double>& sorted, double q) {
    if (sorted.empty()) return 0.0;
    const double pos = q * static_cast<double>(sorted.size() - 1);
    const size_t lo = static_cast<size_t>(pos);
    const size_t hi = (std::min)(sorted.size() - 1, lo + 1);
    const double frac = pos - static_cast<double>(lo);
    return sorted[lo] + (sorted[hi] - sorted[lo]) * frac;
}

} // namespace

DelayStats sampleDelays(const HumanizationProfile& profile, double target_ms, int samples,
                        uint32_t seed) {
    DelayStats stats;
    const int count = std::clamp(samples, 1, 20000);
    stats.samples = count;
    stats.target_ms = (std::max)(0.0, target_ms);

    // Its own session, so sampling doesn't nudge the fatigue cycle of a run
    // that might be going on at the same time.
    HumanizationSession session;
    RandomizationEngine engine(profile, seed, &session);
    engine.resetSessionClock();

    std::vector<double> values;
    values.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        values.push_back(engine.sampleTransitDelayMs(stats.target_ms, 0.0, true));
    }

    std::sort(values.begin(), values.end());
    double sum = 0.0;
    for (double v : values)
        sum += v;

    stats.min_ms = values.front();
    stats.max_ms = values.back();
    stats.mean_ms = sum / static_cast<double>(count);
    stats.p10_ms = percentile(values, 0.10);
    stats.p50_ms = percentile(values, 0.50);
    stats.p90_ms = percentile(values, 0.90);
    return stats;
}

std::string describeDelays(const DelayStats& s) {
    char buf[320];
    std::snprintf(buf, sizeof(buf),
                  "%.0f ms wait -> %.1f / %.1f / %.1f / %.1f / %.1f ms"
                  " (min / p10 / median / p90 / max), mean %.1f, %d samples",
                  s.target_ms, s.min_ms, s.p10_ms, s.p50_ms, s.p90_ms, s.max_ms, s.mean_ms,
                  s.samples);
    return std::string(buf);
}

} // namespace undek
