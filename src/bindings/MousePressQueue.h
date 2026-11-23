#pragma once

#include <cstddef>
#include <vector>

#include "bindings/MouseHotkeyWatcher.h"

namespace undek::bindings {

// The numbers Windows uses, so the hook can hand its message straight over
// without translating anything first. Keeping them here is what lets the
// double-click spellings be tested without a Windows desktop.
namespace mouse_message {
constexpr unsigned int kLeftDown = 0x0201;
constexpr unsigned int kLeftDblClk = 0x0203;
constexpr unsigned int kRightDown = 0x0204;
constexpr unsigned int kRightDblClk = 0x0206;
constexpr unsigned int kMiddleDown = 0x0207;
constexpr unsigned int kMiddleDblClk = 0x0209;
constexpr unsigned int kXButtonDown = 0x020B;
constexpr unsigned int kXButtonDblClk = 0x020D;
} // namespace mouse_message

namespace modifier_bit {
constexpr unsigned int kAlt = 0x0001;
constexpr unsigned int kControl = 0x0002;
constexpr unsigned int kShift = 0x0004;
} // namespace modifier_bit

namespace mouse_key {
constexpr unsigned int kLeft = 0x01;
constexpr unsigned int kRight = 0x02;
constexpr unsigned int kMiddle = 0x04;
constexpr unsigned int kX1 = 0x05;
constexpr unsigned int kX2 = 0x06;
} // namespace mouse_key

// What the watcher decides before it wakes anyone: which button a message
// means, whether the modifiers match, and how many presses it's willing to hold
// on to. Split out from the hook so it can be tested.
class MousePressQueue {
public:
    // Oldest presses are dropped past this; nobody wants a backlog from a long
    // macro run.
    static constexpr std::size_t kBacklog = 16;

    // True when the message is a press of a watched kind, with the virtual key
    // it maps to. The *DBLCLK spellings count, since the second click of a fast
    // double tap arrives as one of those rather than another down.
    static bool buttonFor(unsigned int message, bool second_x_button, unsigned int& vk_out);

    // Only the modifiers a binding can ask for are compared.
    static bool modifiersMatch(unsigned int required, unsigned int held);

    void setButtons(std::vector<WatchedButton> buttons);
    const std::vector<WatchedButton>& buttons() const { return buttons_; }
    void clear();

    // The id bound to that button with those modifiers, or 0 if nothing is.
    int match(unsigned int vk, unsigned int held_modifiers) const;

    void push(int id);
    std::vector<int> take();
    std::size_t size() const { return pressed_.size(); }

private:
    std::vector<WatchedButton> buttons_;
    std::vector<int> pressed_;
};

} // namespace undek::bindings
