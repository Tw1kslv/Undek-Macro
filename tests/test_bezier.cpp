#include "core/HumanizationProfile.h"
#include "core/PathGenerator.h"
#include "core/RandomizationEngine.h"

#include <cmath>
#include <iostream>

namespace {

bool near(double a, double b, double eps = 2.0) {
    return std::fabs(a - b) <= eps;
}

} // namespace

int main() {
    undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
    undek::RandomizationEngine rng(profile);
    undek::PathGenerator paths(profile, rng);

    const undek::Vec2 from{0, 0};
    const undek::Vec2 to{300, 200};
    const auto steps = paths.generatePath(from, to);

    if (steps.size() < 2) {
        std::cerr << "FAIL: path too short\n";
        return 1;
    }

    const auto& last = steps.back().position;
    if (!near(last.x, to.x, 15.0) || !near(last.y, to.y, 15.0)) {
        std::cerr << "FAIL: path endpoint (" << last.x << "," << last.y << ") far from target\n";
        return 1;
    }

    const double s0 = undek::PathGenerator::smoothstep(0.0);
    const double s1 = undek::PathGenerator::smoothstep(1.0);
    if (!near(s0, 0.0, 0.001) || !near(s1, 1.0, 0.001)) {
        std::cerr << "FAIL: smoothstep endpoints\n";
        return 1;
    }

    const undek::Vec2 mid =
        undek::PathGenerator::evaluateBezier({0, 0}, {50, 100}, {150, 50}, {300, 200}, 0.5);
    if (mid.x <= 0 || mid.y <= 0) {
        std::cerr << "FAIL: bezier midpoint degenerate\n";
        return 1;
    }

    // The preview needs the curve handles; a straight move should still report two
    undek::Vec2 c1{}, c2{};
    const auto guided = paths.generatePath(from, to, &c1, &c2);
    if (guided.empty() || !std::isfinite(c1.x) || !std::isfinite(c1.y) || !std::isfinite(c2.x) ||
        !std::isfinite(c2.y) || (c1.x == c2.x && c1.y == c2.y)) {
        std::cerr << "FAIL: control points not reported\n";
        return 1;
    }

    using undek::PathGenerator;
    if (!near(PathGenerator::minJerk(0.0), 0.0, 1e-9) ||
        !near(PathGenerator::minJerk(1.0), 1.0, 1e-9) ||
        !near(PathGenerator::minJerk(0.5), 0.5, 1e-9)) {
        std::cerr << "FAIL: minJerk endpoints\n";
        return 1;
    }
    double prev_mj = 0.0;
    for (int i = 1; i <= 100; ++i) {
        const double v = PathGenerator::minJerk(i / 100.0);
        if (v < prev_mj) {
            std::cerr << "FAIL: minJerk not monotone\n";
            return 1;
        }
        prev_mj = v;
    }

    const double distances[] = {3.0, 25.0, 120.0, 600.0, 1800.0};
    for (double d : distances) {
        for (int trial = 0; trial < 200; ++trial) {
            const undek::Vec2 a{100, 100};
            const undek::Vec2 b{100 + d * 0.8, 100 + d * 0.6};
            const auto path = paths.generatePath(a, b);
            if (path.empty()) {
                std::cerr << "FAIL: empty path\n";
                return 1;
            }
            const auto& e = path.back().position;
            if (std::fabs(e.x - b.x) > 1e-6 || std::fabs(e.y - b.y) > 1e-6) {
                std::cerr << "FAIL: path must end exactly on target (d=" << d << ")\n";
                return 1;
            }
            double total = 0.0;
            for (const auto& s : path) {
                if (!std::isfinite(s.position.x) || !std::isfinite(s.position.y) ||
                    s.delay_ms < 0.0) {
                    std::cerr << "FAIL: bad step\n";
                    return 1;
                }
                const undek::Vec2 chord = b - a;
                const undek::Vec2 rel = s.position - a;
                const double perp = std::fabs(rel.x * chord.y - rel.y * chord.x) / chord.length();
                if (perp > d * 0.6 + 10.0) {
                    std::cerr << "FAIL: path strays too far\n";
                    return 1;
                }
                total += s.delay_ms;
            }
            if (d >= 25.0 && (total < 50.0 || total > 3000.0)) {
                std::cerr << "FAIL: duration " << total << " ms out of range for d=" << d << "\n";
                return 1;
            }
        }
    }

    // Overshoot chance is a probability, so no distance or profile value may
    // push it past 1.
    {
        using undek::PathGenerator;
        const double chances[] = {0.0, 0.05, 0.4, 1.0};
        for (double p : chances) {
            const double far = PathGenerator::overshootChance(20000.0, p);
            const double want = std::min(1.0, p * 2.0);
            if (far < 0.0 || far > 1.0 || std::fabs(far - want) > 1e-9) {
                std::cerr << "FAIL: overshoot chance " << far << " for probability " << p << "\n";
                return 1;
            }
        }
        if (std::fabs(PathGenerator::overshootChance(0.0, 0.4) - 0.1) > 1e-9) {
            std::cerr << "FAIL: short moves should use the 0.25 multiplier\n";
            return 1;
        }
    }

    // A coordinate the app can't use must not become a curve full of infinities
    {
        const auto fallback = paths.generatePath({0, 0}, {std::nan(""), 10});
        if (fallback.size() != 1 || !std::isfinite(fallback[0].position.y)) {
            std::cerr << "FAIL: a bad target should become one straight step\n";
            return 1;
        }
    }

    std::cout << "Bezier path test: " << steps.size() << " steps, end (" << last.x << ", " << last.y
              << ")\n";
    std::cout << "PASS\n";
    return 0;
}
