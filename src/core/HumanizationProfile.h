#pragma once

#include <string>

namespace undek {

// Rough starting points for the humanization sliders.
enum class ProfilePreset { Off, Safe, Balanced, Aggressive };

// A hold is a key press, so ten minutes is already far longer than one can be.
// The parser refuses a script that asks for more, and the random scaling a
// profile applies can't push an accepted hold past it either.
constexpr double kMaxHoldMs = 600000.0;

struct HumanizationProfile {
    double delay_sigma_ms = 12.0;
    double delay_min_ms = 5.0;
    double delay_max_ms = 5000.0;

    int steps_per_100px = 25;
    double bezier_curve_strength_min = 0.15;
    double bezier_curve_strength_max = 0.35;

    double overshoot_probability = 0.05;
    double overshoot_pixels_min = 3.0;
    double overshoot_pixels_max = 8.0;

    double path_base_ms = 150.0;
    double path_fitts_ms = 110.0;
    double path_wobble_px = 0.8;
    double pause_probability = 0.03;
    double pause_ms_min = 40.0;
    double pause_ms_max = 120.0;

    double max_runtime_sec = 0.0;
    int max_loop_iterations = 100000;
    int max_total_instructions = 1000000;
    std::string require_window;
    bool log_to_file = true;

    int random_seed = 0;

    bool session_drift_enabled = true;
    double session_drift_min = 0.95;
    double session_drift_max = 1.08;
    double session_drift_period_sec = 600.0;

    bool noise_enabled = false;
    int noise_interval_actions = 20;
    int noise_max_pixels = 2;

    // A dropped press and its release look like a script that skips inputs, so
    // it happens rarely and only the off preset takes it out.
    bool miss_input_enabled = true;
    double miss_probability = 0.008;

    bool fatigue_focus_enabled = true;
    double fatigue_cycle_min_sec = 120.0;
    double fatigue_cycle_max_sec = 180.0;
    int fatigue_cycle_actions = 200;
    double fatigue_shift_min = 0.05;
    double fatigue_shift_max = 0.15;
    double fatigue_sigma_widen_min = 1.15;
    double fatigue_sigma_widen_max = 1.45;

    bool micro_stall_enabled = true;
    double micro_stall_chance = 0.015;
    double micro_stall_min_ms = 180.0;
    double micro_stall_max_ms = 350.0;

    std::string input_backend = "auto";

    // What a failed key press, click or move means. "abort" stops the macro so
    // it can't carry on as if the input happened; "continue" keeps the old
    // log-and-carry-on behaviour for scripts that check the log themselves.
    std::string input_failure_policy = "abort";

    bool min_packet_delay_enabled = true;
    double min_packet_delay_ms = 15.0;

    // The humanized delay between a press and its release can sample far below
    // the time the script asked for, and a press shorter than a frame is one the
    // target may not notice at all. Holds never come out shorter than this.
    double min_press_ms = 30.0;
    double min_packet_delay_jitter_ms = 5.0;

    bool hold_variability_enabled = true;
    double hold_scale_min = 0.85;
    double hold_scale_max = 1.35;
    double hold_panic_chance = 0.08;
    double hold_panic_mult_min = 1.35;
    double hold_panic_mult_max = 1.90;
    bool key_overlap_enabled = true;
    double key_overlap_probability = 0.12;
    double key_overlap_min_ms = 6.0;
    double key_overlap_max_ms = 24.0;

    static HumanizationProfile defaults();

    // A preset fills in the humanization knobs; limits, window and backend
    // stay at their defaults. applyPreset() keeps the ones you already set.
    static HumanizationProfile preset(ProfilePreset which);
    static const char* presetName(ProfilePreset which);
    void applyPreset(ProfilePreset which);
    static bool loadFromFile(const std::string& path, HumanizationProfile& out);
    bool saveToFile(const std::string& path) const;
    bool validate(std::string& error) const;
};

} // namespace undek
