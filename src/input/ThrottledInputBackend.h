#pragma once

#include "input/IInputBackend.h"

#include <memory>
#include <random>

namespace undek {

class ThrottledInputBackend : public IInputBackend {
public:
    ThrottledInputBackend(std::unique_ptr<IInputBackend> inner, double min_gap_ms,
                          double jitter_ms);

    Vec2 cursorPosition() const override;
    InputResult moveAbsolute(int x, int y) override;
    InputResult moveRelative(int dx, int dy) override;
    InputResult clickLeft() override;
    InputResult mouseDown(const std::string& button) override;
    InputResult mouseUp(const std::string& button) override;
    InputResult mouseWheel(int delta) override;
    InputResult keyDown(const std::string& key) override;
    InputResult keyUp(const std::string& key) override;
    bool supportsKey(const std::string& key) const override;

private:
    void gate();
    // Releases never wait and never check the cancel flag: a Stop must not
    // leave a key or button held down.
    void markSent();
    void sleepForMs(double ms) const;

    std::unique_ptr<IInputBackend> inner_;
    double min_gap_ms_ = 15.0;
    double jitter_ms_ = 5.0;
    std::mt19937 rng_;
};

} // namespace undek
