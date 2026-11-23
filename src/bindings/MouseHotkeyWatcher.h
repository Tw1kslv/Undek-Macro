#pragma once

#include <string>
#include <vector>

namespace undek::bindings {

struct WatchedButton {
    int id = 0;
    unsigned int virtual_key = 0;
    unsigned int modifiers = 0;
};

// RegisterHotKey can't register mouse buttons, so a side button bound to a macro
// needs its own hook. It sits on a separate thread and queues each press, which
// keeps a short tap from falling between two frames the way a GetAsyncKeyState
// poll loses it.
class MouseHotkeyWatcher {
public:
    static bool start(const std::vector<WatchedButton>& buttons, std::string& error_out);
    static void stop();
    static bool running();

    // Queued hotkey ids, oldest first. Calling this empties the queue.
    static std::vector<int> takePressedIds();

    // Signalled when something arrives, so a wait loop can sleep instead of poll.
    static void* wakeHandle();
};

} // namespace undek::bindings
