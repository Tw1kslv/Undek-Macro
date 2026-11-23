#include "core/HumanizationProfile.h"

#include "util/AtomicFile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <utility>

namespace undek {

HumanizationProfile HumanizationProfile::defaults() {
    return HumanizationProfile{};
}

const char* HumanizationProfile::presetName(ProfilePreset which) {
    switch (which) {
        case ProfilePreset::Off:
            return "Off";
        case ProfilePreset::Safe:
            return "Safe";
        case ProfilePreset::Aggressive:
            return "Aggressive";
        case ProfilePreset::Balanced:
            break;
    }
    return "Balanced";
}

HumanizationProfile HumanizationProfile::preset(ProfilePreset which) {
    HumanizationProfile p; // the defaults are the balanced preset

    if (which == ProfilePreset::Balanced) return p;

    if (which == ProfilePreset::Off) {
        p.delay_sigma_ms = 0.0;
        p.delay_min_ms = 0.0;
        p.steps_per_100px = 1;
        p.bezier_curve_strength_min = 0.0;
        p.bezier_curve_strength_max = 0.0;
        p.path_wobble_px = 0.0;
        p.path_base_ms = 0.0;
        p.path_fitts_ms = 0.0;
        p.overshoot_probability = 0.0;
        p.pause_probability = 0.0;
        p.session_drift_enabled = false;
        p.noise_enabled = false;
        p.miss_input_enabled = false;
        p.fatigue_focus_enabled = false;
        p.micro_stall_enabled = false;
        p.hold_variability_enabled = false;
        p.key_overlap_enabled = false;
        p.min_packet_delay_enabled = false;
        return p;
    }

    if (which == ProfilePreset::Safe) {
        // Small deviations, longer pauses, very few mistakes.
        p.delay_sigma_ms = 7.0;
        p.delay_min_ms = 10.0;
        p.delay_max_ms = 900.0;
        p.steps_per_100px = 32;
        p.bezier_curve_strength_min = 0.10;
        p.bezier_curve_strength_max = 0.22;
        p.overshoot_probability = 0.02;
        p.overshoot_pixels_min = 2.0;
        p.overshoot_pixels_max = 5.0;
        p.path_base_ms = 190.0;
        p.path_fitts_ms = 120.0;
        p.path_wobble_px = 0.5;
        p.pause_probability = 0.015;
        p.session_drift_min = 0.97;
        p.session_drift_max = 1.04;
        p.miss_input_enabled = true;
        p.miss_probability = 0.003;
        p.micro_stall_chance = 0.006;
        p.fatigue_shift_min = 0.03;
        p.fatigue_shift_max = 0.09;
        p.fatigue_sigma_widen_min = 1.08;
        p.fatigue_sigma_widen_max = 1.20;
        p.hold_scale_min = 0.90;
        p.hold_scale_max = 1.20;
        p.hold_panic_chance = 0.05;
        return p;
    }

    // Aggressive: wide spread, more overshoot and the odd stumble.
    p.delay_sigma_ms = 22.0;
    p.delay_min_ms = 4.0;
    p.delay_max_ms = 6500.0;
    p.steps_per_100px = 18;
    p.bezier_curve_strength_min = 0.22;
    p.bezier_curve_strength_max = 0.55;
    p.overshoot_probability = 0.12;
    p.overshoot_pixels_min = 4.0;
    p.overshoot_pixels_max = 14.0;
    p.path_base_ms = 130.0;
    p.path_fitts_ms = 105.0;
    p.path_wobble_px = 1.6;
    p.pause_probability = 0.07;
    p.pause_ms_min = 50.0;
    p.pause_ms_max = 200.0;
    p.session_drift_min = 0.92;
    p.session_drift_max = 1.16;
    p.session_drift_period_sec = 360.0;
    p.noise_enabled = true;
    p.miss_input_enabled = true;
    p.miss_probability = 0.02;
    p.fatigue_shift_min = 0.08;
    p.fatigue_shift_max = 0.20;
    p.fatigue_sigma_widen_min = 1.25;
    p.fatigue_sigma_widen_max = 1.60;
    p.micro_stall_chance = 0.035;
    p.hold_scale_min = 0.80;
    p.hold_scale_max = 1.45;
    p.hold_panic_chance = 0.12;
    p.key_overlap_probability = 0.20;
    return p;
}

void HumanizationProfile::applyPreset(ProfilePreset which) {
    const HumanizationProfile keep = *this;
    HumanizationProfile next = preset(which);

    // These say where and how the macro is allowed to run, so a preset must
    // not quietly loosen them.
    next.max_runtime_sec = keep.max_runtime_sec;
    next.max_loop_iterations = keep.max_loop_iterations;
    next.max_total_instructions = keep.max_total_instructions;
    next.require_window = keep.require_window;
    next.log_to_file = keep.log_to_file;
    next.random_seed = keep.random_seed;
    next.input_backend = keep.input_backend;

    *this = next;
}

bool HumanizationProfile::loadFromFile(const std::string& path, HumanizationProfile& out) {
    std::ifstream file(path);
    if (!file) return false;

    nlohmann::json j;
    try {
        file >> j;
    } catch (...) {
        return false;
    }
    if (!j.is_object()) return false;

    // Fill a copy, so a profile that turns out to be broken leaves the one
    // currently in use untouched instead of half-overwritten.
    HumanizationProfile parsed = out;

    // A key we know about with the wrong type is a broken profile, not an
    // invitation to carry on with the default.
    bool bad_type = false;
    auto getd = [&](const char* k, double& v) {
        if (!j.contains(k)) return;
        if (j[k].is_number())
            v = j[k].get<double>();
        else
            bad_type = true;
    };
    auto geti = [&](const char* k, int& v) {
        if (!j.contains(k)) return;
        const auto& node = j[k];
        // get<int>() narrows whatever number it is handed, so a fractional or an
        // oversized setting is turned away before the conversion, not after it.
        if (!node.is_number_integer()) {
            bad_type = true;
            return;
        }
        const double wide = node.get<double>();
        if (wide < static_cast<double>(std::numeric_limits<int>::min()) ||
            wide > static_cast<double>(std::numeric_limits<int>::max())) {
            bad_type = true;
            return;
        }
        v = static_cast<int>(wide);
    };
    auto getb = [&](const char* k, bool& v) {
        if (!j.contains(k)) return;
        if (j[k].is_boolean())
            v = j[k].get<bool>();
        else
            bad_type = true;
    };
    auto gets = [&](const char* k, std::string& v) {
        if (!j.contains(k)) return;
        if (j[k].is_string())
            v = j[k].get<std::string>();
        else
            bad_type = true;
    };

    getd("delay_sigma_ms", parsed.delay_sigma_ms);
    getd("delay_min_ms", parsed.delay_min_ms);
    getd("delay_max_ms", parsed.delay_max_ms);
    geti("steps_per_100px", parsed.steps_per_100px);
    getd("bezier_curve_strength_min", parsed.bezier_curve_strength_min);
    getd("bezier_curve_strength_max", parsed.bezier_curve_strength_max);
    getd("overshoot_probability", parsed.overshoot_probability);
    getd("overshoot_pixels_min", parsed.overshoot_pixels_min);
    getd("overshoot_pixels_max", parsed.overshoot_pixels_max);
    getd("path_base_ms", parsed.path_base_ms);
    getd("path_fitts_ms", parsed.path_fitts_ms);
    getd("path_wobble_px", parsed.path_wobble_px);
    getd("pause_probability", parsed.pause_probability);
    getd("pause_ms_min", parsed.pause_ms_min);
    getd("pause_ms_max", parsed.pause_ms_max);
    getd("max_runtime_sec", parsed.max_runtime_sec);
    geti("max_loop_iterations", parsed.max_loop_iterations);
    geti("max_total_instructions", parsed.max_total_instructions);
    getb("log_to_file", parsed.log_to_file);
    gets("require_window", parsed.require_window);
    geti("random_seed", parsed.random_seed);
    getb("session_drift_enabled", parsed.session_drift_enabled);
    getd("session_drift_min", parsed.session_drift_min);
    getd("session_drift_max", parsed.session_drift_max);
    getd("session_drift_period_sec", parsed.session_drift_period_sec);
    getb("noise_enabled", parsed.noise_enabled);
    geti("noise_interval_actions", parsed.noise_interval_actions);
    geti("noise_max_pixels", parsed.noise_max_pixels);
    gets("input_backend", parsed.input_backend);
    gets("input_failure_policy", parsed.input_failure_policy);
    getb("min_packet_delay_enabled", parsed.min_packet_delay_enabled);
    getd("min_packet_delay_ms", parsed.min_packet_delay_ms);
    getd("min_packet_delay_jitter_ms", parsed.min_packet_delay_jitter_ms);
    getb("miss_input_enabled", parsed.miss_input_enabled);
    getd("miss_probability", parsed.miss_probability);
    getb("fatigue_focus_enabled", parsed.fatigue_focus_enabled);
    getd("fatigue_cycle_min_sec", parsed.fatigue_cycle_min_sec);
    getd("fatigue_cycle_max_sec", parsed.fatigue_cycle_max_sec);
    geti("fatigue_cycle_actions", parsed.fatigue_cycle_actions);
    getd("fatigue_shift_min", parsed.fatigue_shift_min);
    getd("fatigue_shift_max", parsed.fatigue_shift_max);
    getd("fatigue_sigma_widen_min", parsed.fatigue_sigma_widen_min);
    getd("fatigue_sigma_widen_max", parsed.fatigue_sigma_widen_max);
    getb("micro_stall_enabled", parsed.micro_stall_enabled);
    getd("micro_stall_chance", parsed.micro_stall_chance);
    getd("min_press_ms", parsed.min_press_ms);
    getd("micro_stall_min_ms", parsed.micro_stall_min_ms);
    getd("micro_stall_max_ms", parsed.micro_stall_max_ms);
    getb("hold_variability_enabled", parsed.hold_variability_enabled);
    getd("hold_scale_min", parsed.hold_scale_min);
    getd("hold_scale_max", parsed.hold_scale_max);
    getd("hold_panic_chance", parsed.hold_panic_chance);
    getd("hold_panic_mult_min", parsed.hold_panic_mult_min);
    getd("hold_panic_mult_max", parsed.hold_panic_mult_max);
    getb("key_overlap_enabled", parsed.key_overlap_enabled);
    getd("key_overlap_probability", parsed.key_overlap_probability);
    getd("key_overlap_min_ms", parsed.key_overlap_min_ms);
    getd("key_overlap_max_ms", parsed.key_overlap_max_ms);

    if (bad_type) return false;
    out = parsed;
    return true;
}

bool HumanizationProfile::saveToFile(const std::string& path) const {
    nlohmann::json j;
    j["delay_sigma_ms"] = delay_sigma_ms;
    j["delay_min_ms"] = delay_min_ms;
    j["delay_max_ms"] = delay_max_ms;
    j["steps_per_100px"] = steps_per_100px;
    j["bezier_curve_strength_min"] = bezier_curve_strength_min;
    j["bezier_curve_strength_max"] = bezier_curve_strength_max;
    j["overshoot_probability"] = overshoot_probability;
    j["overshoot_pixels_min"] = overshoot_pixels_min;
    j["overshoot_pixels_max"] = overshoot_pixels_max;
    j["path_base_ms"] = path_base_ms;
    j["path_fitts_ms"] = path_fitts_ms;
    j["path_wobble_px"] = path_wobble_px;
    j["pause_probability"] = pause_probability;
    j["pause_ms_min"] = pause_ms_min;
    j["pause_ms_max"] = pause_ms_max;
    j["max_runtime_sec"] = max_runtime_sec;
    j["max_loop_iterations"] = max_loop_iterations;
    j["max_total_instructions"] = max_total_instructions;
    j["require_window"] = require_window;
    j["log_to_file"] = log_to_file;
    j["random_seed"] = random_seed;
    j["session_drift_enabled"] = session_drift_enabled;
    j["session_drift_min"] = session_drift_min;
    j["session_drift_max"] = session_drift_max;
    j["session_drift_period_sec"] = session_drift_period_sec;
    j["noise_enabled"] = noise_enabled;
    j["noise_interval_actions"] = noise_interval_actions;
    j["noise_max_pixels"] = noise_max_pixels;
    j["input_backend"] = input_backend;
    j["input_failure_policy"] = input_failure_policy;
    j["min_packet_delay_enabled"] = min_packet_delay_enabled;
    j["min_packet_delay_ms"] = min_packet_delay_ms;
    j["min_packet_delay_jitter_ms"] = min_packet_delay_jitter_ms;
    j["miss_input_enabled"] = miss_input_enabled;
    j["miss_probability"] = miss_probability;
    j["fatigue_focus_enabled"] = fatigue_focus_enabled;
    j["fatigue_cycle_min_sec"] = fatigue_cycle_min_sec;
    j["fatigue_cycle_max_sec"] = fatigue_cycle_max_sec;
    j["fatigue_cycle_actions"] = fatigue_cycle_actions;
    j["fatigue_shift_min"] = fatigue_shift_min;
    j["fatigue_shift_max"] = fatigue_shift_max;
    j["fatigue_sigma_widen_min"] = fatigue_sigma_widen_min;
    j["fatigue_sigma_widen_max"] = fatigue_sigma_widen_max;
    j["micro_stall_enabled"] = micro_stall_enabled;
    j["micro_stall_chance"] = micro_stall_chance;
    j["min_press_ms"] = min_press_ms;
    j["micro_stall_min_ms"] = micro_stall_min_ms;
    j["micro_stall_max_ms"] = micro_stall_max_ms;
    j["hold_variability_enabled"] = hold_variability_enabled;
    j["hold_scale_min"] = hold_scale_min;
    j["hold_scale_max"] = hold_scale_max;
    j["hold_panic_chance"] = hold_panic_chance;
    j["hold_panic_mult_min"] = hold_panic_mult_min;
    j["hold_panic_mult_max"] = hold_panic_mult_max;
    j["key_overlap_enabled"] = key_overlap_enabled;
    j["key_overlap_probability"] = key_overlap_probability;
    j["key_overlap_min_ms"] = key_overlap_min_ms;
    j["key_overlap_max_ms"] = key_overlap_max_ms;

    return writeFileAtomically(path, j.dump(2) + "\n");
}

bool HumanizationProfile::validate(std::string& error) const {
    auto fail = [&error](const std::string& msg) {
        error = msg;
        return false;
    };

    // NaN gets past every comparison below, and an infinite duration turns into
    // a nonsense chrono value, so nothing numeric gets in as one.
    const std::pair<const char*, double> numbers[] = {
        {"delay_min_ms", delay_min_ms},
        {"delay_max_ms", delay_max_ms},
        {"delay_sigma_ms", delay_sigma_ms},
        {"bezier_curve_strength_min", bezier_curve_strength_min},
        {"bezier_curve_strength_max", bezier_curve_strength_max},
        {"path_wobble_px", path_wobble_px},
        {"path_base_ms", path_base_ms},
        {"path_fitts_ms", path_fitts_ms},
        {"overshoot_probability", overshoot_probability},
        {"overshoot_pixels_min", overshoot_pixels_min},
        {"overshoot_pixels_max", overshoot_pixels_max},
        {"pause_probability", pause_probability},
        {"pause_ms_min", pause_ms_min},
        {"pause_ms_max", pause_ms_max},
        {"session_drift_min", session_drift_min},
        {"session_drift_max", session_drift_max},
        {"session_drift_period_sec", session_drift_period_sec},
        {"miss_probability", miss_probability},
        {"fatigue_cycle_min_sec", fatigue_cycle_min_sec},
        {"fatigue_cycle_max_sec", fatigue_cycle_max_sec},
        {"fatigue_shift_min", fatigue_shift_min},
        {"fatigue_shift_max", fatigue_shift_max},
        {"fatigue_sigma_widen_min", fatigue_sigma_widen_min},
        {"fatigue_sigma_widen_max", fatigue_sigma_widen_max},
        {"micro_stall_chance", micro_stall_chance},
        {"micro_stall_min_ms", micro_stall_min_ms},
        {"micro_stall_max_ms", micro_stall_max_ms},
        {"min_press_ms", min_press_ms},
        {"hold_scale_min", hold_scale_min},
        {"hold_scale_max", hold_scale_max},
        {"hold_panic_chance", hold_panic_chance},
        {"hold_panic_mult_min", hold_panic_mult_min},
        {"hold_panic_mult_max", hold_panic_mult_max},
        {"key_overlap_probability", key_overlap_probability},
        {"key_overlap_min_ms", key_overlap_min_ms},
        {"key_overlap_max_ms", key_overlap_max_ms},
        {"min_packet_delay_ms", min_packet_delay_ms},
        {"min_packet_delay_jitter_ms", min_packet_delay_jitter_ms},
        {"max_runtime_sec", max_runtime_sec},
    };
    for (const auto& entry : numbers) {
        if (!std::isfinite(entry.second))
            return fail(std::string(entry.first) + " must be a number");
    }

    if (delay_min_ms < 0.0) return fail("delay_min_ms must be >= 0");
    if (delay_max_ms < delay_min_ms) return fail("delay_max_ms must be >= delay_min_ms");
    if (delay_sigma_ms < 0.0) return fail("delay_sigma_ms must be >= 0");
    if (steps_per_100px <= 0) return fail("steps_per_100px must be > 0");

    // Settings for a disabled effect are not validated, so a profile can keep
    // stale values for something it has switched off.
    if (overshoot_probability < 0.0 || overshoot_probability > 1.0)
        return fail("overshoot_probability must be between 0 and 1");
    if (pause_probability < 0.0 || pause_probability > 1.0)
        return fail("pause_probability must be between 0 and 1");
    if (miss_input_enabled && (miss_probability < 0.0 || miss_probability > 1.0))
        return fail("miss_probability must be between 0 and 1");
    if (micro_stall_enabled && (micro_stall_chance < 0.0 || micro_stall_chance > 1.0))
        return fail("micro_stall_chance must be between 0 and 1");
    if (hold_variability_enabled && (hold_panic_chance < 0.0 || hold_panic_chance > 1.0))
        return fail("hold_panic_chance must be between 0 and 1");
    if (key_overlap_enabled && (key_overlap_probability < 0.0 || key_overlap_probability > 1.0))
        return fail("key_overlap_probability must be between 0 and 1");

    if (bezier_curve_strength_min < 0.0 || bezier_curve_strength_max < bezier_curve_strength_min)
        return fail("bezier curve strength range is invalid");
    if (overshoot_pixels_min < 0.0 || overshoot_pixels_max < overshoot_pixels_min)
        return fail("overshoot pixel range is invalid");
    if (pause_ms_min < 0.0 || pause_ms_max < pause_ms_min) return fail("pause ms range is invalid");
    if (session_drift_enabled) {
        if (session_drift_min <= 0.0 || session_drift_max < session_drift_min)
            return fail("session drift range is invalid");
        if (session_drift_period_sec <= 0.0) return fail("session_drift_period_sec must be > 0");
    }
    if (noise_enabled && (noise_interval_actions <= 0 || noise_max_pixels < 0))
        return fail("noise settings are invalid");
    if (fatigue_focus_enabled) {
        if (fatigue_cycle_min_sec <= 0.0 || fatigue_cycle_max_sec < fatigue_cycle_min_sec)
            return fail("fatigue cycle seconds range is invalid");
        if (fatigue_shift_min < 0.0 || fatigue_shift_max < fatigue_shift_min)
            return fail("fatigue shift range is invalid");
        if (fatigue_sigma_widen_min < 1.0 || fatigue_sigma_widen_max < fatigue_sigma_widen_min)
            return fail("fatigue sigma widen range is invalid");
    }
    if (micro_stall_enabled &&
        (micro_stall_min_ms < 0.0 || micro_stall_max_ms < micro_stall_min_ms))
        return fail("micro stall range is invalid");
    if (min_press_ms < 0.0 || min_press_ms > 500.0)
        return fail("min_press_ms must be between 0 and 500");
    if (hold_variability_enabled) {
        if (hold_scale_min <= 0.0 || hold_scale_max < hold_scale_min)
            return fail("hold scale range is invalid");
        if (hold_panic_mult_min < 1.0 || hold_panic_mult_max < hold_panic_mult_min)
            return fail("hold panic multiplier range is invalid");
        // These multiply the hold a script asked for, so an absurd value here
        // turned a valid hold into one far past the parser's ceiling.
        if (hold_scale_max > 10.0) return fail("hold_scale_max must be <= 10");
        if (hold_panic_mult_max > 10.0) return fail("hold_panic_mult_max must be <= 10");
    }
    if (key_overlap_enabled &&
        (key_overlap_min_ms < 0.0 || key_overlap_max_ms < key_overlap_min_ms))
        return fail("key overlap range is invalid");
    if (min_packet_delay_ms < 0.0 || min_packet_delay_jitter_ms < 0.0)
        return fail("packet delay settings must be >= 0");
    if (path_base_ms < 0.0 || path_fitts_ms < 0.0 || path_wobble_px < 0.0)
        return fail("path settings must be >= 0");
    if (max_loop_iterations <= 0) return fail("max_loop_iterations must be > 0");
    if (max_total_instructions < 0) return fail("max_total_instructions must be >= 0");
    if (max_runtime_sec < 0.0) return fail("max_runtime_sec must be >= 0");

    // Ceilings for the values that end up as an int, a duration or a tick count.
    // They are far above anything the editor offers, so only a hand-edited file
    // or a stray assignment runs into them.
    if (steps_per_100px > 400) return fail("steps_per_100px must be <= 400");
    if (delay_sigma_ms > 60000.0) return fail("delay_sigma_ms must be <= 60000");
    if (delay_max_ms > 3600000.0) return fail("delay_max_ms must be <= 3600000");
    if (path_base_ms > 60000.0 || path_fitts_ms > 60000.0)
        return fail("path timings must be <= 60000 ms");
    if (path_wobble_px > 1000.0) return fail("path_wobble_px must be <= 1000");
    if (overshoot_pixels_max > 10000.0) return fail("overshoot_pixels_max must be <= 10000");
    if (pause_ms_max > 3600000.0) return fail("pause_ms_max must be <= 3600000");
    if (micro_stall_max_ms > 60000.0) return fail("micro_stall_max_ms must be <= 60000");
    if (key_overlap_max_ms > 10000.0) return fail("key_overlap_max_ms must be <= 10000");
    if (min_packet_delay_ms > 60000.0 || min_packet_delay_jitter_ms > 60000.0)
        return fail("packet delays must be <= 60000 ms");
    if (max_runtime_sec > 86400.0) return fail("max_runtime_sec must be <= 86400 (a day)");
    if (random_seed < 0) return fail("random_seed must be >= 0");

    // A misspelled backend used to look like it worked and quietly sent input
    // through SendInput.
    std::string backend = input_backend;
    std::transform(backend.begin(), backend.end(), backend.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (backend != "auto" && backend != "sendinput" && backend != "interception")
        return fail("input_backend must be auto, sendinput or interception");

    std::string policy = input_failure_policy;
    std::transform(policy.begin(), policy.end(), policy.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (policy != "abort" && policy != "continue")
        return fail("input_failure_policy must be abort or continue");

    error.clear();
    return true;
}

} // namespace undek
