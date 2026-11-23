#include "core/PathGenerator.h"

#include <algorithm>
#include <cmath>

namespace undek {
namespace {
constexpr double kPi = 3.14159265358979323846;

// Clamped in floating point, because rounding first is what turns a silly
// profile value into an integer that can't hold it.
int clampToInt(double value, int low, int high) {
    if (!std::isfinite(value)) return high;
    if (value <= static_cast<double>(low)) return low;
    if (value >= static_cast<double>(high)) return high;
    return static_cast<int>(value);
}
} // namespace

PathGenerator::PathGenerator(const HumanizationProfile& profile, RandomizationEngine& rng) :
    profile_(profile), rng_(rng) {}

void PathGenerator::setProfile(const HumanizationProfile& profile) {
    profile_ = profile;
}

double PathGenerator::smoothstep(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double PathGenerator::minJerk(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * t * (10.0 - 15.0 * t + 6.0 * t * t);
}

Vec2 PathGenerator::evaluateBezier(const Vec2& p0, const Vec2& p1, const Vec2& p2, const Vec2& p3,
                                   double t) {
    const double u = 1.0 - t;
    const double b0 = u * u * u;
    const double b1 = 3.0 * u * u * t;
    const double b2 = 3.0 * u * t * t;
    const double b3 = t * t * t;
    return {b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x,
            b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y};
}

double PathGenerator::overshootChance(double dist, double probability) {
    const double scaled = std::clamp(dist / 400.0, 0.25, 2.0) * probability;
    return std::clamp(scaled, 0.0, 1.0);
}

std::vector<PathStep> PathGenerator::generatePath(const Vec2& from, const Vec2& to, Vec2* c1,
                                                  Vec2* c2) {
    std::vector<PathStep> steps;
    // A bad coordinate shouldn't turn into a curve full of infinities; going
    // straight there is the least surprising thing to do.
    if (!std::isfinite(from.x) || !std::isfinite(from.y) || !std::isfinite(to.x) ||
        !std::isfinite(to.y)) {
        steps.push_back({to, 0.0});
        return steps;
    }
    const Vec2 chord = to - from;
    const double dist = chord.length();
    if (!std::isfinite(dist) || dist > 1.0e7) {
        steps.push_back({to, 0.0});
        return steps;
    }
    if (dist < 1.0) {
        steps.push_back({to, 0.0});
        return steps;
    }

    auto& gen = rng_.rng();
    std::uniform_real_distribution<double> unit(0.0, 1.0);

    const bool fitts = profile_.path_fitts_ms > 0.0;
    const int density_steps =
        clampToInt(std::ceil(dist * profile_.steps_per_100px / 100.0), 2, 2000);
    const double duration_ms =
        fitts ? profile_.path_base_ms + profile_.path_fitts_ms * std::log2(1.0 + dist / 20.0) : 0.0;
    const int step_count = fitts ? clampToInt(duration_ms / 4.0, 2, density_steps) : density_steps;
    const double step_delay =
        fitts ? duration_ms / step_count : std::clamp(2.0 + dist / 200.0, 1.0, 15.0);

    std::uniform_real_distribution<double> strength(profile_.bezier_curve_strength_min,
                                                    profile_.bezier_curve_strength_max);

    const Vec2 dir = chord.normalized();
    const Vec2 perp = dir.perpendicular();
    const double offset = dist * strength(gen);

    const Vec2 p0 = from;
    const Vec2 p3 = to;
    const Vec2 p1 = from + dir * (dist * 0.33) + perp * offset;
    const Vec2 p2 = from + dir * (dist * 0.66) - perp * (offset * 0.7);
    if (c1) *c1 = p1;
    if (c2) *c2 = p2;

    const bool overshoot = unit(gen) < overshootChance(dist, profile_.overshoot_probability);
    Vec2 overshoot_point = to;
    if (overshoot) {
        std::uniform_real_distribution<double> opx(profile_.overshoot_pixels_min,
                                                   profile_.overshoot_pixels_max);
        const double px = std::min(opx(gen), dist * 0.15);
        std::uniform_real_distribution<double> side(-1.5, 1.5);
        overshoot_point = to + dir * px + perp * side(gen);
    }

    const double wobble_amp = std::max(0.0, profile_.path_wobble_px);
    const double ph1 = unit(gen) * 2.0 * kPi;
    const double ph2 = unit(gen) * 2.0 * kPi;
    const double f1 = 1.5 + unit(gen);
    const double f2 = 3.0 + 2.0 * unit(gen);

    int pause_index = -1;
    if (step_count > 6 && unit(gen) < profile_.pause_probability) {
        std::uniform_int_distribution<int> pi(static_cast<int>(step_count * 0.2),
                                              static_cast<int>(step_count * 0.8));
        pause_index = pi(gen);
    }

    for (int i = 1; i <= step_count; ++i) {
        const double t_lin = static_cast<double>(i) / step_count;
        Vec2 pos = evaluateBezier(p0, p1, p2, p3, minJerk(t_lin));

        const double w = wobble_amp * std::sin(kPi * t_lin) *
                         (0.6 * std::sin(2.0 * kPi * f1 * t_lin + ph1) +
                          0.4 * std::sin(2.0 * kPi * f2 * t_lin + ph2));
        pos = pos + perp * w;

        if (t_lin > 0.9) {
            const double jitter_scale = (t_lin - 0.9) / 0.1;
            std::uniform_real_distribution<double> jitter(-2.5, 2.5);
            pos = pos + perp * (jitter(gen) * (1.0 - jitter_scale));
        }

        if (i == step_count && !overshoot) pos = to;

        double delay = rng_.sampleDelayMs(step_delay, step_delay * 0.15, true, 1.0, 500.0);
        if (i - 1 == pause_index) {
            std::uniform_real_distribution<double> pd(
                profile_.pause_ms_min, std::max(profile_.pause_ms_min, profile_.pause_ms_max));
            delay += pd(gen);
        }
        steps.push_back({pos, delay});
    }

    if (overshoot) {
        steps.back().position = overshoot_point;
        std::uniform_real_distribution<double> react(30.0, 90.0);
        steps.back().delay_ms += react(gen);
        const int corr = 3;
        for (int i = 1; i <= corr; ++i) {
            const double s = minJerk(static_cast<double>(i) / corr);
            const Vec2 pos = (i == corr) ? to : overshoot_point + (to - overshoot_point) * s;
            steps.push_back({pos, rng_.sampleDelayMs(10.0, 3.0, true, 3.0, 40.0)});
        }
    }

    return steps;
}

} // namespace undek
