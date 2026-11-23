#pragma once

#include <string>
#include <vector>

namespace undek::editor {

// One editable line of a recording: the wait that comes before it, the script
// text itself, whether it's kept, and which left drag it belongs to.
struct RecordedStep {
    int wait_ms = 0;
    std::string text;
    bool enabled = true;
    int drag_group = -1;
};

// Turns the recorder's flat "Wait(...) / action" lines into steps.
std::vector<RecordedStep> parseRecordedSteps(const std::vector<std::string>& lines);

// Writes the kept steps back out as .undek source.
std::string formatRecordedSteps(const std::vector<RecordedStep>& steps);

// Empty when every kept press has a matching release, otherwise a sentence
// naming what would still be held. A recording that ends mid-hold plays back as
// a key stuck down, so this is checked before anything is inserted.
std::string checkRecordingBalance(const std::vector<RecordedStep>& steps);

} // namespace undek::editor
