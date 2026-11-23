#pragma once

#include <string>

namespace undek {

// Writes through a temp file, so a crash halfway through a save leaves the
// old file alone instead of a half-written one.
bool writeFileAtomically(const std::string& path, const std::string& data);

} // namespace undek
