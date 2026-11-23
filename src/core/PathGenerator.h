#pragma once

#include "core/HumanizationProfile.h"
#include "core/RandomizationEngine.h"
#include "core/Vec2.h"

#include <vector>

namespace undek {

struct PathStep {
    Vec2 position;
    double delay_ms = 0.0;
};

class PathGenerator {
public:
    explicit PathGenerator(const HumanizationProfile& profile, RandomizationEngine& rng);

    // c1/c2 are optional and receive the two curve handles, so a preview can draw them.
    std::vector<PathStep> generatePath(const Vec2& from, const Vec2& to, Vec2* c1 = nullptr,
                                       Vec2* c2 = nullptr);
    void setProfile(const HumanizationProfile& profile);

    static Vec2 evaluateBezier(const Vec2& p0, const Vec2& p1, const Vec2& p2, const Vec2& p3,
                               double t);
    static double smoothstep(double t);
    static double minJerk(double t);
    // Distance widens the chance of an overshoot, but it must never pass 1.
    static double overshootChance(double dist, double probability);

private:
    HumanizationProfile profile_;
    RandomizationEngine& rng_;
};

} // namespace undek
