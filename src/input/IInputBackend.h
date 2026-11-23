#pragma once

#include "core/Vec2.h"

#include <string>

namespace undek {

// What came back from one injected event. SendInput tells us when it dropped
// something, and dropping it silently made broken runs look like normal ones.
struct InputResult {
    bool ok = true;
    std::string error;
    // A press that landed while its release didn't: the button or key may
    // still be down, so the caller has to keep it on its cleanup list.
    bool maybe_down = false;

    static InputResult success() { return {}; }
    static InputResult fail(std::string why) {
        InputResult r;
        r.ok = false;
        r.error = std::move(why);
        return r;
    }
    // Failed, and the input it was meant to undo might have stuck.
    static InputResult uncertain(std::string why) {
        InputResult r = fail(std::move(why));
        r.maybe_down = true;
        return r;
    }

    explicit operator bool() const { return ok; }
};

class IInputBackend {
public:
    virtual ~IInputBackend() = default;

    virtual Vec2 cursorPosition() const = 0;
    virtual InputResult moveAbsolute(int x, int y) = 0;
    virtual InputResult moveRelative(int dx, int dy) = 0;
    virtual InputResult clickLeft() = 0;
    virtual InputResult mouseDown(const std::string& button) = 0;
    virtual InputResult mouseUp(const std::string& button) = 0;
    virtual InputResult mouseWheel(int delta) = 0;
    virtual InputResult keyDown(const std::string& key) = 0;
    virtual InputResult keyUp(const std::string& key) = 0;

    // False when this backend cannot map the key name to any input. Used by the
    // VM to report unsupported keys instead of silently dropping them.
    virtual bool supportsKey(const std::string& /*key*/) const { return true; }
};

} // namespace undek
