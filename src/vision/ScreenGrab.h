#pragma once

#include "vision/Image.h"

#include <string>

namespace undek {

// Captures the whole desktop into an ImageFrame. On Windows that is every
// monitor, with the frame's (0,0) at the top-left of the virtual desktop, so
// coordinates match what a screenshot would show.
class ScreenGrab {
public:
    static bool capture(ImageFrame& out, std::string& error);
};

} // namespace undek
