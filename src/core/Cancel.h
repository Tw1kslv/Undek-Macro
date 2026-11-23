#pragma once

#include <atomic>
#include <mutex>
#include <stdexcept>

namespace undek::cancel {

inline std::atomic<bool>& flag() {
    static std::atomic<bool> f{false};
    return f;
}

inline std::atomic<bool>& shutdownFlag() {
    static std::atomic<bool> f{false};
    return f;
}

// Stop and "start a run" have to agree on the order, otherwise a Stop landing
// in the few microseconds where a new run clears the flag gets lost.
inline std::mutex& requestMutex() {
    static std::mutex m;
    return m;
}

inline void request() {
    std::lock_guard<std::mutex> lock(requestMutex());
    flag().store(true);
}

inline void clear() {
    std::lock_guard<std::mutex> lock(requestMutex());
    flag().store(false);
}

inline bool requested() {
    return flag().load();
}

inline void requestShutdown() {
    shutdownFlag().store(true);
    flag().store(true);
}

struct Cancelled : std::runtime_error {
    Cancelled() : std::runtime_error("Cancelled") {}
};

inline void throwIfRequested() {
    if (requested() || shutdownFlag().load()) throw Cancelled();
}

} // namespace undek::cancel
