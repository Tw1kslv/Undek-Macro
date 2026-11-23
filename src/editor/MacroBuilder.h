#pragma once

#include <string>
#include <vector>

namespace undek::editor {

// The visual builder's model. It only formats .undek source; the editor's own
// lexer and VM still do the actual work, so a built script behaves exactly like
// a hand-written one.
enum class BuilderAction {
    Wait,
    MoveTo,
    Click,
    PressKey,
    KeyDown,
    KeyUp,
    MouseDown,
    MouseUp,
    Scroll,
    Loop,
    IfWindow,
    IfPixel,
};

struct BuilderStep {
    BuilderAction action = BuilderAction::Click;
    int x = 0;
    int y = 0;
    int amount = -120; // Scroll only
    double wait_ms = 100.0;
    std::string text; // key name or mouse button
    bool humanize = true;

    bool selected = false; // builder UI only, ignored when writing the script
    int loop_count = 3;
    std::string window_title;
    int pixel_x = 0;
    int pixel_y = 0;
    unsigned int color = 0xFFFFFFu;
    int tolerance = 0;
    std::vector<BuilderStep> body; // Loop / if blocks
};

const char* builderActionName(BuilderAction action);
bool builderActionNeedsPoint(BuilderAction action);
bool builderActionNeedsText(BuilderAction action);
bool builderActionIsButton(BuilderAction action);
bool builderActionIsBlock(BuilderAction action);

// One line of script for one step, or a whole indented block for a loop/if.
// Returns an empty string for a step that can't be written yet (a key step with
// no key name, an empty block), so the UI can skip it.
std::string formatBuilderStep(const BuilderStep& step);
std::string formatBuilderScript(const std::vector<BuilderStep>& steps);

// First line only, for a one-line row in the builder list.
std::string builderStepSummary(const BuilderStep& step);

// Follows a list of block indexes, e.g. {2, 0} for "second step of the third
// block". Returns nullptr when the path doesn't land on a block.
std::vector<BuilderStep>* builderTarget(std::vector<BuilderStep>& steps,
                                        const std::vector<int>& path);

// Moves the ticked steps into one block of the given kind, in place. Returns
// false when nothing was ticked.
bool wrapSelected(std::vector<BuilderStep>& steps, BuilderAction block_kind,
                  const std::string& window_title = {}, int loop_count = 3, int pixel_x = 0,
                  int pixel_y = 0, unsigned int color = 0xFFFFFFu, int tolerance = 0);

// Same thing, but for ticked steps at any depth: each level wraps its own
// selection. Returns how many blocks were made.
int wrapSelectedAnywhere(std::vector<BuilderStep>& steps, BuilderAction block_kind,
                         const std::string& window_title = {}, int loop_count = 3, int pixel_x = 0,
                         int pixel_y = 0, unsigned int color = 0xFFFFFFu, int tolerance = 0);

} // namespace undek::editor
