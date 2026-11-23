#include "core/HumanizationProfile.h"
#include "core/RandomizationEngine.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    }
}

bool near(double a, double b, double eps) {
    return std::fabs(a - b) <= eps;
}

} // namespace

int main() {
    undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
    profile.delay_sigma_ms = 10.0;
    profile.session_drift_enabled = false;

    undek::RandomizationEngine rng(profile);
    const double mean = 100.0;
    const double sigma = 10.0;
    const int n = 10000;

    double sum = 0;
    double sum_sq = 0;
    for (int i = 0; i < n; ++i) {
        const double s = rng.sampleDelayMs(mean, sigma, true);
        sum += s;
        sum_sq += s * s;
    }

    const double sample_mean = sum / n;
    const double sample_var = (sum_sq / n) - (sample_mean * sample_mean);

    std::cout << "Gaussian delay test (" << n << " samples)\n";
    std::cout << "  target mean: " << mean << " sampled mean: " << sample_mean << "\n";
    std::cout << "  target sigma^2: " << (sigma * sigma) << " sampled var: " << sample_var << "\n";

    check(near(sample_mean, mean, 2.0), "mean is within 2 ms of the target");
    check(near(sample_var, sigma * sigma, 25.0), "variance is within tolerance");

    const auto p = rng.samplePointInDisk({100, 100}, 10);
    check(std::hypot(p.x - 100, p.y - 100) <= 10.5, "disk sample stays inside the radius");

    // Same seed and profile means the same sequence, which is what a
    // reproducible run needs.
    {
        undek::HumanizationProfile fixed = undek::HumanizationProfile::defaults();
        fixed.session_drift_enabled = false;
        fixed.fatigue_focus_enabled = false;
        undek::RandomizationEngine a(fixed, 1234);
        undek::RandomizationEngine b(fixed, 1234);
        bool same = true;
        for (int i = 0; i < 50; ++i) {
            if (a.sampleDelayMs(50.0, 0.0, true) != b.sampleDelayMs(50.0, 0.0, true)) {
                same = false;
                break;
            }
        }
        check(same, "the same seed produces the same delays");

        undek::RandomizationEngine c(fixed, 4321);
        bool differs = false;
        for (int i = 0; i < 20; ++i) {
            if (c.sampleDelayMs(50.0, 0.0, true) != a.sampleDelayMs(50.0, 0.0, true)) {
                differs = true;
                break;
            }
        }
        check(differs, "a different seed produces a different sequence");
    }

    // No sample may escape the profile's clamps, however wide the sigma
    {
        undek::HumanizationProfile clamped = undek::HumanizationProfile::defaults();
        clamped.delay_min_ms = 20.0;
        clamped.delay_max_ms = 60.0;
        clamped.session_drift_enabled = false;
        clamped.fatigue_focus_enabled = false;
        clamped.micro_stall_enabled = false;
        undek::RandomizationEngine wild(clamped, 7);
        bool in_range = true;
        double lo = 1e9;
        double hi = -1e9;
        for (int i = 0; i < 5000; ++i) {
            const double s = wild.sampleDelayMs(40.0, 500.0, true);
            lo = std::min(lo, s);
            hi = std::max(hi, s);
            if (s < clamped.delay_min_ms - 1e-9 || s > clamped.delay_max_ms + 1e-9)
                in_range = false;
        }
        check(in_range, "every delay stays inside delay_min/delay_max");
        check(near(lo, clamped.delay_min_ms, 1e-6) && near(hi, clamped.delay_max_ms, 1e-6),
              "a wild sigma actually reaches both clamps");
    }

    // Zero spread with humanization on still returns the requested time
    {
        undek::HumanizationProfile flat = undek::HumanizationProfile::defaults();
        flat.delay_sigma_ms = 0.0;
        flat.delay_min_ms = 0.0;
        flat.session_drift_enabled = false;
        flat.fatigue_focus_enabled = false;
        flat.micro_stall_enabled = false;
        undek::RandomizationEngine steady(flat, 3);
        check(steady.sampleDelayMs(40.0, 0.0, true) == 40.0,
              "a zero sigma returns the requested delay exactly");
    }

    // Probabilities land near their configured rates
    {
        undek::HumanizationProfile rates = undek::HumanizationProfile::defaults();
        rates.miss_input_enabled = true;
        rates.miss_probability = 0.2;
        rates.key_overlap_enabled = true;
        rates.key_overlap_probability = 0.25;
        undek::RandomizationEngine engine(rates, 99);

        int misses = 0;
        int overlaps = 0;
        const int trials = 20000;
        for (int i = 0; i < trials; ++i) {
            if (engine.shouldMissInput()) ++misses;
            if (engine.shouldOverlapInput()) ++overlaps;
        }
        const double miss_rate = static_cast<double>(misses) / trials;
        const double overlap_rate = static_cast<double>(overlaps) / trials;
        check(near(miss_rate, 0.2, 0.02),
              "miss rate matches miss_probability (" + std::to_string(miss_rate) + ")");
        check(near(overlap_rate, 0.25, 0.02), "overlap rate matches key_overlap_probability (" +
                                                  std::to_string(overlap_rate) + ")");
    }

    // Holds: exact range without humanizing, and a known envelope with it.
    // Variability deliberately scales the requested hold (that's the point of
    // hold_scale and panic stretch), so the humanized bounds are wider.
    {
        undek::HumanizationProfile holds = undek::HumanizationProfile::defaults();
        undek::RandomizationEngine engine(holds, 11);

        bool exact = true;
        for (int i = 0; i < 2000; ++i) {
            const double h = engine.sampleHoldMs(50.0, 80.0, false);
            if (h < 50.0 - 1e-9 || h > 80.0 + 1e-9) exact = false;
        }
        check(exact, "humanize=false keeps holds between hold_min and hold_max");

        const double low = 50.0 * holds.hold_scale_min;
        const double high = 80.0 * holds.hold_scale_max * holds.hold_panic_mult_max;
        bool in_envelope = true;
        double shortest = 1e9;
        double longest = -1e9;
        for (int i = 0; i < 5000; ++i) {
            const double h = engine.sampleHoldMs(50.0, 80.0, true);
            shortest = std::min(shortest, h);
            longest = std::max(longest, h);
            if (h < low - 1e-9 || h > high + 1e-9 || h < holds.delay_min_ms) in_envelope = false;
        }
        check(in_envelope, "humanized holds stay inside hold scale + panic stretch");
        check(longest > 80.0, "humanized holds do reach above hold_max sometimes (" +
                                  std::to_string(longest) + ")");
        check(shortest < 50.0,
              "humanized holds can be shorter than hold_min (" + std::to_string(shortest) + ")");
    }

    // Box jitter stays inside the box
    {
        undek::RandomizationEngine engine(profile, 5);
        bool inside = true;
        for (int i = 0; i < 2000; ++i) {
            const auto q = engine.samplePointInBox({50, 50}, 8, 4);
            if (std::fabs(q.x - 50) > 8.0 + 1e-9 || std::fabs(q.y - 50) > 4.0 + 1e-9) {
                inside = false;
            }
        }
        check(inside, "box jitter stays within box_w/box_h");
    }

    // Drift and noise respect their configured bounds
    {
        undek::HumanizationProfile bounded = undek::HumanizationProfile::defaults();
        bounded.session_drift_enabled = true;
        bounded.session_drift_min = 0.9;
        bounded.session_drift_max = 1.1;
        bounded.noise_enabled = true;
        bounded.noise_max_pixels = 3;
        undek::RandomizationEngine engine(bounded, 21);

        bool drift_ok = true;
        for (int i = 0; i < 500; ++i) {
            const double f = engine.sessionDriftFactor();
            if (f < 0.9 - 1e-9 || f > 1.1 + 1e-9) drift_ok = false;
        }
        check(drift_ok, "session drift stays inside its bounds");

        bool noise_ok = true;
        for (int i = 0; i < 500; ++i) {
            const auto n = engine.sampleNoiseOffset();
            if (std::fabs(n.x) > 3.0 + 1e-9 || std::fabs(n.y) > 3.0 + 1e-9) noise_ok = false;
        }
        check(noise_ok, "noise offsets stay within noise_max_pixels");
    }

    // Boundaries: the clamps, the configured rates and the exact cycle action
    {
        undek::HumanizationProfile bounded = undek::HumanizationProfile::defaults();
        bounded.delay_min_ms = 20.0;
        bounded.delay_max_ms = 60.0;
        bounded.session_drift_enabled = false;
        bounded.fatigue_focus_enabled = false;
        bounded.micro_stall_enabled = false;
        undek::RandomizationEngine engine(bounded, 13);
        check(engine.sampleDelayMs(1000.0, 0.0, true) == 60.0,
              "a delay past delay_max is clamped to it");
        check(engine.sampleDelayMs(0.0, 0.0, true) == 20.0,
              "a delay under delay_min is clamped to it");

        const auto still = engine.samplePointInDisk({10, 10}, 0.0);
        check(still.x == 10.0 && still.y == 10.0, "a zero radius samples the centre exactly");

        undek::HumanizationProfile quiet = undek::HumanizationProfile::defaults();
        quiet.noise_enabled = true;
        quiet.noise_max_pixels = 0;
        undek::RandomizationEngine quiet_engine(quiet, 14);
        const auto none = quiet_engine.sampleNoiseOffset();
        check(none.x == 0.0 && none.y == 0.0, "zero noise means no offset");

        undek::HumanizationProfile stalls = undek::HumanizationProfile::defaults();
        stalls.micro_stall_enabled = true;
        stalls.micro_stall_chance = 0.3;
        stalls.micro_stall_min_ms = 200.0;
        stalls.micro_stall_max_ms = 200.0;
        stalls.session_drift_enabled = false;
        stalls.fatigue_focus_enabled = false;
        undek::RandomizationEngine stall_engine(stalls, 15);
        const int trials = 4000;
        int stalled = 0;
        for (int i = 0; i < trials; ++i) {
            double added = 0.0;
            stall_engine.sampleTransitDelayMs(10.0, 0.0, true, &added);
            if (added > 0.0) ++stalled;
        }
        const double stall_rate = static_cast<double>(stalled) / trials;
        check(near(stall_rate, 0.3, 0.03),
              "micro stalls land near micro_stall_chance (" + std::to_string(stall_rate) + ")");

        undek::HumanizationProfile panic = undek::HumanizationProfile::defaults();
        panic.hold_variability_enabled = true;
        panic.hold_panic_chance = 0.25;
        panic.session_drift_enabled = false;
        panic.fatigue_focus_enabled = false;
        undek::RandomizationEngine panic_engine(panic, 16);
        panic_engine.resetSessionClock();
        int panics = 0;
        for (int i = 0; i < trials; ++i) {
            bool panicked = false;
            panic_engine.sampleHoldDelayMs(20.0, 1.0, true, &panicked);
            if (panicked) ++panics;
        }
        const double panic_rate = static_cast<double>(panics) / trials;
        check(near(panic_rate, 0.25, 0.03),
              "panic holds land near hold_panic_chance (" + std::to_string(panic_rate) + ")");

        // The action count is a boundary, not a suggestion
        undek::HumanizationProfile cycle = undek::HumanizationProfile::defaults();
        cycle.fatigue_focus_enabled = true;
        cycle.fatigue_cycle_actions = 5;
        cycle.fatigue_cycle_min_sec = 1.0e9;
        cycle.fatigue_cycle_max_sec = 1.0e9;
        undek::RandomizationEngine cycle_engine(cycle, 17);
        cycle_engine.resetSessionClock();
        int fired_at = 0;
        for (int i = 1; i <= 5; ++i) {
            if (cycle_engine.checkFatigueCycle()) fired_at = i;
        }
        check(fired_at == 5, "the fatigue cycle turns over on the 5th action, not earlier (" +
                                 std::to_string(fired_at) + ")");
    }

    // Test Fatigue & Focus Multiplier (5-15% base delay shift and widened sigma)
    profile.fatigue_focus_enabled = true;
    profile.fatigue_shift_min = 0.05;
    profile.fatigue_shift_max = 0.15;
    profile.fatigue_sigma_widen_min = 1.15;
    profile.fatigue_sigma_widen_max = 1.45;
    rng.setProfile(profile);
    rng.advanceFatigueCycle();

    const double delay_mult = rng.fatigueDelayMultiplier();
    const double sigma_mult = rng.fatigueSigmaMultiplier();
    const double shift_mag = std::fabs(delay_mult - 1.0);
    check(shift_mag >= 0.049 && shift_mag <= 0.151,
          "fatigue delay shift stays in [5%, 15%] (" + std::to_string(delay_mult) + ")");
    check(sigma_mult >= 1.149 && sigma_mult <= 1.451,
          "fatigue sigma widen stays in [1.15, 1.45] (" + std::to_string(sigma_mult) + ")");

    // Test Micro Stalls (180-350ms outlier delay when triggered)
    profile.micro_stall_enabled = true;
    profile.micro_stall_chance = 1.0;
    profile.micro_stall_min_ms = 180.0;
    profile.micro_stall_max_ms = 350.0;
    rng.setProfile(profile);
    double stall_added = 0.0;
    const double stalled_delay = rng.sampleTransitDelayMs(25.0, 5.0, true, &stall_added);
    check(stall_added >= 180.0 && stall_added <= 350.0 && stalled_delay >= 180.0,
          "micro-stall delay stays in [180, 350] ms");

    // Test Key Hold Variability & Panic Hold Stretch
    profile.hold_variability_enabled = true;
    profile.hold_panic_chance = 1.0;
    profile.hold_panic_mult_min = 1.40;
    profile.hold_panic_mult_max = 1.80;
    rng.setProfile(profile);
    bool panic_hold = false;
    const double hold_delay = rng.sampleHoldDelayMs(20.0, 2.0, true, &panic_hold);
    check(panic_hold && hold_delay > 20.0, "hold panic stretch triggers when its chance is 1.0");

    // A dropped press looks like a broken script, so it stays rare and only the
    // off preset refuses to do it at all.
    {
        const auto balanced = undek::HumanizationProfile::preset(undek::ProfilePreset::Balanced);
        check(balanced.miss_input_enabled && balanced.miss_probability > 0.0,
              "the balanced preset drops the odd input");
        check(!undek::HumanizationProfile::preset(undek::ProfilePreset::Off).miss_input_enabled,
              "the off preset doesn't drop inputs");
        check(
            undek::HumanizationProfile::preset(undek::ProfilePreset::Aggressive).miss_probability >
                balanced.miss_probability,
            "the aggressive preset misses more often than the balanced one");
        const auto safe = undek::HumanizationProfile::preset(undek::ProfilePreset::Safe);
        check(safe.miss_input_enabled && safe.miss_probability > 0.0,
              "the safe preset keeps its rare misses");
        check(
            undek::HumanizationProfile::preset(undek::ProfilePreset::Aggressive).miss_input_enabled,
            "the aggressive preset keeps its misses");
    }

    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "PASS\n";
    return 0;
}
