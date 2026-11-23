#include "core/HumanizationSession.h"

namespace undek {

HumanizationSession& HumanizationSession::shared() {
    static HumanizationSession session;
    return session;
}

HumanizationSession::CycleState HumanizationSession::snapshot() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return state_;
}

HumanizationSession::CycleState HumanizationSession::bumpActions() {
    std::lock_guard<std::mutex> lock(mtx_);
    ++state_.cycle_actions;
    return state_;
}

void HumanizationSession::reset() {
    std::lock_guard<std::mutex> lock(mtx_);
    initialized_ = false;
    state_ = CycleState{};
}

} // namespace undek
