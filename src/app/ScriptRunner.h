#pragma once

#include <functional>
#include <string>

namespace undek {

namespace script {
class DebugController;
}

using LogCallback = std::function<void(const std::string&)>;

class ScriptRunner {
public:
    static int run(const std::string& script_path, const std::string& profile_path, bool dry_run,
                   LogCallback log_cb = nullptr);
    // `script_dir` is the folder the script lives in. Pictures are looked up
    // beside it, the same way `undek check` looks them up.
    static int runSource(const std::string& source, const std::string& profile_path, bool dry_run,
                         std::string* error_out = nullptr, LogCallback log_cb = nullptr,
                         script::DebugController* dbg = nullptr,
                         const std::string& script_dir = "");
};

} // namespace undek
