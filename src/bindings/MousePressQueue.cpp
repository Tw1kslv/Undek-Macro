#include "bindings/MousePressQueue.h"

#include <utility>

namespace undek::bindings {

bool MousePressQueue::buttonFor(unsigned int message, bool second_x_button, unsigned int& vk_out) {
    using namespace mouse_message;
    switch (message) {
        case kLeftDown:
        case kLeftDblClk:
            vk_out = mouse_key::kLeft;
            return true;
        case kRightDown:
        case kRightDblClk:
            vk_out = mouse_key::kRight;
            return true;
        case kMiddleDown:
        case kMiddleDblClk:
            vk_out = mouse_key::kMiddle;
            return true;
        case kXButtonDown:
        case kXButtonDblClk:
            vk_out = second_x_button ? mouse_key::kX2 : mouse_key::kX1;
            return true;
        default:
            return false;
    }
}

bool MousePressQueue::modifiersMatch(unsigned int required, unsigned int held) {
    const unsigned int mask = modifier_bit::kAlt | modifier_bit::kControl | modifier_bit::kShift;
    return (required & mask) == (held & mask);
}

void MousePressQueue::setButtons(std::vector<WatchedButton> buttons) {
    buttons_ = std::move(buttons);
}

void MousePressQueue::clear() {
    buttons_.clear();
    pressed_.clear();
}

int MousePressQueue::match(unsigned int vk, unsigned int held_modifiers) const {
    for (const auto& button : buttons_) {
        if (button.virtual_key == vk && modifiersMatch(button.modifiers, held_modifiers))
            return button.id;
    }
    return 0;
}

void MousePressQueue::push(int id) {
    if (id == 0) return;
    if (pressed_.size() >= kBacklog) pressed_.erase(pressed_.begin());
    pressed_.push_back(id);
}

std::vector<int> MousePressQueue::take() {
    std::vector<int> out;
    out.swap(pressed_);
    return out;
}

} // namespace undek::bindings
