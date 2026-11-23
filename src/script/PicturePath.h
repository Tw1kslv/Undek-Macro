#pragma once

#include <string>

namespace undek::script {

// The file a picture path in a script points at: as written, then beside the
// script, then under the app folder. Checking and running both come through
// here, so a script that checks clean is looking at the file a run would open.
// Empty when none of the three exist.
std::string resolvePicturePath(const std::string& path, const std::string& script_dir);

} // namespace undek::script
