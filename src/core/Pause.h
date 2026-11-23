#pragma once

#include "core/Cancel.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace undek::pause {

inline std::atomic<bool>& flag() {
    static std::atomic<bool> f{false};
    return f;
}

inline bool pending() {
    return flag().load();
}

inline void request() {
    flag().store(true);
}

inline void clear() {
    flag().store(false);
}

inline void toggle() {
    flag().store(!flag().load());
}

// Blocks while a pause is in force. Stop still wins, so a paused run can always
// be ended rather than needing to be resumed first.
inline void waitIfPaused() {
    if (!pending()) return;
    while (pending()) {
        cancel::throwIfRequested();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

} // namespace undek::pause
