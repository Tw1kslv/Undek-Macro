#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace undek {

// Plain 0xRRGGBB pixels, row-major starting from the top.
struct ImageFrame {
    int width = 0;
    int height = 0;
    // Desktop coordinate of pixel (0,0). Non-zero when a capture starts left of
    // or above the primary monitor, which is normal on a multi-monitor desk.
    int origin_x = 0;
    int origin_y = 0;
    std::vector<uint32_t> pixels;

    int desktopX(int x) const { return origin_x + x; }
    int desktopY(int y) const { return origin_y + y; }

    bool valid() const {
        return width > 0 && height > 0 &&
               pixels.size() == static_cast<size_t>(width) * static_cast<size_t>(height);
    }
    uint32_t at(int x, int y) const {
        return pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
    }
};

struct ImageMatch {
    int x = 0; // top-left of the match
    int y = 0;
    int width = 0;
    int height = 0;
    double score = 0.0; // 1.0 means every pixel was inside tolerance and identical

    int centerX() const { return x + width / 2; }
    int centerY() const { return y + height / 2; }
};

// Only uncompressed 24/32-bit BMP for now, which is what a screenshot tool or
// Paint saved crop will be anyway. `name` only shows up in error messages, so
// a caller holding bytes from somewhere other than a file can still say where
// they came from.
bool loadBmp(const std::string& path, ImageFrame& out, std::string& error);
bool decodeBmp(const std::vector<uint8_t>& bytes, ImageFrame& out, std::string& error,
               const std::string& name = {});
bool saveBmp(const std::string& path, const ImageFrame& frame, std::string& error);

// Largest per-channel difference between two colours, 0-255.
int channelDistance(uint32_t a, uint32_t b);

// Scans the frame and keeps the best spot where every pixel is within
// `tolerance` of the template. Ties go to the first one found.
// `checkpoint` runs once per row so a big scan can be interrupted; whatever it
// throws comes straight back out. `first_match` returns on the first hit
// instead of scanning the rest for a better score, which is all a yes/no check
// like ImageExists needs.
// The part of the desktop to search, in desktop coordinates. A zero width or
// height means the whole capture. The rectangle is clamped to the frame, and one
// that misses it entirely matches nothing.
struct SearchArea {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool set() const { return width > 0 && height > 0; }
};

std::optional<ImageMatch> findImage(const ImageFrame& haystack, const ImageFrame& needle,
                                    int tolerance = 0,
                                    const std::function<void()>& checkpoint = nullptr,
                                    bool first_match = false, const SearchArea& area = {});

} // namespace undek
