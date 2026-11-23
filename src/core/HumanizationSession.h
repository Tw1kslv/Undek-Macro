#pragma once

#include <chrono>
#include <mutex>
#include <utility>

namespace undek {

// Shared fatigue/focus cycle state used by RandomizationEngine instances.
// Engines sharing one session drift through fatigue cycles together; pass a
// distinct session to an engine's constructor for an independent cycle.
class HumanizationSession {
public:
    struct CycleState {
        std::chrono::steady_clock::time_point cycle_start{};
        double cycle_duration_sec = 150.0;
        double fatigue_delay_mult = 1.0;
        double fatigue_sigma_mult = 1.0;
        int cycle_actions = 0;
    };

    static HumanizationSession& shared();

    // Fills and returns the state on first use; afterwards returns the
    // existing state untouched. `fill` runs under the lock only when the
    // session is still uninitialized.
    template<typename F> CycleState ensureInitialized(F&& fill) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!initialized_) {
            initialized_ = true;
            fill(state_);
        }
        return state_;
    }

    template<typename F> void update(F&& f) {
        std::lock_guard<std::mutex> lock(mtx_);
        f(state_);
    }

    CycleState snapshot() const;
    CycleState bumpActions();
    void reset();

private:
    mutable std::mutex mtx_;
    bool initialized_ = false;
    CycleState state_;
};

} // namespace undek
