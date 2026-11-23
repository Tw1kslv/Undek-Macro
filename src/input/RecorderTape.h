#pragma once

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace undek {

// Turns what the hook sees into script lines. The hook itself stays out of this
// so the awkward parts can be tried without a desktop: the two Shift keys
// sharing one name, releases that land while paused, jitter that isn't worth a
// MoveTo.
//
// Times are milliseconds on any steady clock, supplied by the caller. Not
// thread safe; the recorder holds one lock around it.
class RecorderTape {
public:
    void reset(long long now_ms);

    void setPaused(bool paused);
    bool paused() const;

    void key(long long vk, const std::string& name, bool down, long long now_ms);
    void button(const std::string& name, bool down, long long now_ms);
    void move(int x, int y, long long now_ms);
    void wheel(int delta, long long now_ms);

    // Writes the releases the script is still waiting for. `keep_held` skips
    // anything the user still has down, which is what resuming wants; stopping
    // closes everything out.
    void closeOut(bool keep_held, long long now_ms);

    const std::vector<std::string>& actions() const { return actions_; }
    std::size_t count() const { return actions_.size(); }

private:
    void pushWait(long long now_ms);
    bool nameHeld(const std::string& name) const;
    bool nameHeldElsewhere(const std::string& name, long long vk) const;
    // The physical key now responsible for the one KeyDown of that name.
    long long holderOf(const std::string& name) const;

    std::vector<std::string> actions_;
    std::map<long long, std::string> held_keys_;
    std::map<long long, std::string> recorded_keys_;
    std::set<std::string> held_buttons_;
    std::set<std::string> recorded_buttons_;
    long long last_event_ = 0;
    int last_move_x_ = 0;
    int last_move_y_ = 0;
    long long last_move_at_ = 0;
    bool have_last_move_ = false;
    bool paused_ = false;
};

} // namespace undek
