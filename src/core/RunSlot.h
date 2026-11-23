#pragma once

#include <string>

namespace undek::runs {

// Only one macro may drive the input backends at a time, whichever way it was
// started (GUI button, hotkey, or `listen` in the CLI).
//
// Starting a run also clears the cancel flag, because clearing it separately
// let a fresh run wipe out another run's stop request. A pause is left alone
// here and cleared by end() instead, so one pressed as the run starts survives.
bool tryBegin(const std::string& owner);
void end();
bool active();
std::string owner();

// The pause belongs to the run it was pressed during. Requests go through the
// slot so one that arrives after end() has cleared the flag is refused instead
// of pausing the run after it. Returns false when no run is active.
bool requestPause();
void clearPause();

} // namespace undek::runs
