#pragma once

#include "input/IInputBackend.h"

namespace undek {

// For runs that may not send anything at all: a dry run, a preview, a plan.
// Every call succeeds and does nothing, so nothing has to load a driver or ask
// Windows for permission to inject input.
class NullInputBackend : public IInputBackend {
public:
    Vec2 cursorPosition() const override { return {}; }
    InputResult moveAbsolute(int, int) override { return InputResult::success(); }
    InputResult moveRelative(int, int) override { return InputResult::success(); }
    InputResult clickLeft() override { return InputResult::success(); }
    InputResult mouseDown(const std::string&) override { return InputResult::success(); }
    InputResult mouseUp(const std::string&) override { return InputResult::success(); }
    InputResult mouseWheel(int) override { return InputResult::success(); }
    InputResult keyDown(const std::string&) override { return InputResult::success(); }
    InputResult keyUp(const std::string&) override { return InputResult::success(); }
};

} // namespace undek
