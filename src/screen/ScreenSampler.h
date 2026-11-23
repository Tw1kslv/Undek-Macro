#pragma once

#include "core/Vec2.h"

#include <optional>
#include <string>

namespace undek {

class ScreenSampler {
public:
    // nullopt when the pixel can't be read, so a failed read is not confused with black.
    static std::optional<unsigned int> pixelColor(int x, int y);
    static bool colorMatches(unsigned int actual, unsigned int expected, int tolerance = 0);
};

class WindowInfo {
public:
    static std::string foregroundTitle();
    static bool titleContains(const std::string& needle);
};

} // namespace undek
