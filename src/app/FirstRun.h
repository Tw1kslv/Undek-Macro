#pragma once

#include <string>
#include <vector>

namespace undek {

// What the startup check found and did. The GUI shows the welcome panel when
// fresh is set, and lists `created` so the user knows what appeared on disk.
struct FirstRunReport {
    bool fresh = false;
    std::string previous_version;
    std::vector<std::string> created;
    std::string error;
};

// Makes a new install usable before the first frame: creates the folders the
// app writes into, writes default profile/bindings if they're missing, drops a
// starter macro into an empty macros folder, and records the version that ran.
// Existing files are never overwritten, so it's safe to call on every start.
FirstRunReport prepareFirstRun(const std::string& root, const std::string& version);

} // namespace undek
