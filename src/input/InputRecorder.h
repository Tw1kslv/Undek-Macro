#pragma once

#include <string>
#include <vector>

namespace undek {

class InputRecorder {
public:
    // False when the hooks couldn't be installed, with the reason in `error_out`;
    // recording nothing while the UI says it's recording is worse than saying so.
    static bool start(std::string& error_out);
    static void stop();
    static void pause();
    static void resume();
    static bool isRecording();
    static bool isPaused();
    static void setHostWindow(void* hwnd);
    static std::vector<std::string> takeActions();
    static size_t actionCount();
};

} // namespace undek
