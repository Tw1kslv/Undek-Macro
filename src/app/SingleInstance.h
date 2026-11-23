#pragma once

#include <string>

namespace undek {

// One editor at a time. Two copies of the app drive the same cursor, and there is
// no way to tell afterwards which one moved it.
//
// Only the GUI takes this lock: the command line is allowed to run alongside the
// editor so a script can be checked or a dry run done while it is open.
// Returns true when this process owns the editor. error_out explains a refusal.
bool claimEditorInstance(std::string& error_out);

} // namespace undek
