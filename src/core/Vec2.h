#pragma once

#include <cmath>

namespace undek {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    Vec2() = default;
    Vec2(double x_, double y_) : x(x_), y(y_) {}

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }

    double length() const { return std::sqrt(x * x + y * y); }
    Vec2 normalized() const {
        const double len = length();
        if (len < 1e-9) return {0, 0};
        return {x / len, y / len};
    }
    Vec2 perpendicular() const { return {-y, x}; }
};

// Nothing sensible sits a million pixels from the origin, and anything further
// out is a rounding away from an integer that can't hold it.
constexpr double kMaxPixelCoordinate = 1000000.0;

inline bool pixelInRange(double value) {
    return std::isfinite(value) && std::fabs(value) <= kMaxPixelCoordinate;
}

inline bool pixelInRange(const Vec2& point) {
    return pixelInRange(point.x) && pixelInRange(point.y);
}

struct ColorRGB {
    int r = 0;
    int g = 0;
    int b = 0;
};

} // namespace undek
