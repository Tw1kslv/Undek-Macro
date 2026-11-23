#include "core/RunSlot.h"

#include "core/Cancel.h"
#include "core/Pause.h"

#include <atomic>
#include <mutex>

namespace undek::runs {
namespace {

std::atomic<bool> g_active{false};
std::mutex g_owner_mutex;
std::string g_owner;

} // namespace

bool tryBegin(const std::string& owner) {
    {
        // Claiming the slot and clearing the old stop request happen under the
        // same lock a Stop takes, so a Stop can't slip in between and vanish.
        std::lock_guard<std::mutex> stop_lock(cancel::requestMutex());
        bool expected = false;
        if (!g_active.compare_exchange_strong(expected, true)) return false;
        // A stop request from the previous run shouldn't kill this one.
        cancel::flag().store(false);
    }

    {
        std::lock_guard<std::mutex> lock(g_owner_mutex);
        g_owner = owner;
    }
    return true;
}

void end() {
    // A pause belongs to the run it was pressed during, so it goes when that run
    // does. Clearing it here rather than in tryBegin() is what keeps a pause
    // pressed just as Run is hit: the worker can reach tryBegin() a moment after
    // the hotkey, and wiping it there would swallow the request.
    //
    // The flag is cleared under the lock a pause request takes, so a request
    // that comes in while this run is finishing either lands before the clear
    // (and goes with it) or is refused.
    std::lock_guard<std::mutex> stop_lock(cancel::requestMutex());
    pause::flag().store(false);
    {
        std::lock_guard<std::mutex> lock(g_owner_mutex);
        g_owner.clear();
    }
    g_active.store(false);
}

bool requestPause() {
    std::lock_guard<std::mutex> lock(cancel::requestMutex());
    if (!g_active.load()) return false;
    pause::flag().store(true);
    return true;
}

void clearPause() {
    std::lock_guard<std::mutex> lock(cancel::requestMutex());
    pause::flag().store(false);
}

bool active() {
    return g_active.load();
}

std::string owner() {
    std::lock_guard<std::mutex> lock(g_owner_mutex);
    return g_owner;
}

} // namespace undek::runs
