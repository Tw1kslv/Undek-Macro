#pragma once

#include "input/IInputBackend.h"
#include "input/Win32SendInputBackend.h"

namespace undek {

class InterceptionBackend : public IInputBackend {
public:
    InterceptionBackend();
    ~InterceptionBackend() override;

    bool available() const { return context_ != nullptr && live_; }
    const char* statusName() const;

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
    struct Api {
        void* (*create_context)() = nullptr;
        void (*destroy_context)(void*) = nullptr;
        int (*send)(void*, int, const void*, unsigned int) = nullptr;
        unsigned int (*get_hardware_id)(void*, int, void*, unsigned int) = nullptr;
    };

    bool sendKeyboard(unsigned short code, bool up, bool e0);
    bool sendMouse(unsigned short state, unsigned short flags, int x, int y);
    bool sendMouseWheel(short delta);
    unsigned short scancodeFor(const std::string& key, bool& e0) const;

    Api api_;
    void* module_ = nullptr;
    void* context_ = nullptr;
    int keyboard_device_ = 0;
    int mouse_device_ = 0;
    bool live_ = false;
    Win32SendInputBackend fallback_;
};

} // namespace undek
