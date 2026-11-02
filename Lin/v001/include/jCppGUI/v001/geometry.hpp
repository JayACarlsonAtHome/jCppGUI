// File: include/jCppGUI/v001/geometry.hpp
// Purpose: Point, Size and Rect on int32_t. Arithmetic saturates, and Size is never negative.
#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <limits>
#include <utility>

namespace jgui::inline v001 {

// Converts between integer types, clamping to the target's range instead of wrapping.
template <std::integral To, std::integral From>
[[nodiscard]] constexpr To saturate_to(From value) noexcept {
    if (std::cmp_less(value, std::numeric_limits<To>::min())) return std::numeric_limits<To>::min();
    if (std::cmp_greater(value, std::numeric_limits<To>::max())) return std::numeric_limits<To>::max();
    return static_cast<To>(value);
}

[[nodiscard]] constexpr std::int32_t sat_add(std::int32_t a, std::int32_t b) noexcept {
    return saturate_to<std::int32_t>(std::int64_t{a} + std::int64_t{b});
}
[[nodiscard]] constexpr std::int32_t sat_sub(std::int32_t a, std::int32_t b) noexcept {
    return saturate_to<std::int32_t>(std::int64_t{a} - std::int64_t{b});
}
[[nodiscard]] constexpr std::int32_t sat_mul(std::int32_t a, std::int32_t b) noexcept {
    return saturate_to<std::int32_t>(std::int64_t{a} * std::int64_t{b});
}

struct Point {
    std::int32_t x{};
    std::int32_t y{};

    friend constexpr bool operator==(const Point&, const Point&) = default;
    [[nodiscard]] friend constexpr Point operator+(Point a, Point b) noexcept {
        return Point{sat_add(a.x, b.x), sat_add(a.y, b.y)};
    }
    [[nodiscard]] friend constexpr Point operator-(Point a, Point b) noexcept {
        return Point{sat_sub(a.x, b.x), sat_sub(a.y, b.y)};
    }
};

class Size {
public:
    constexpr Size() noexcept = default;
    constexpr Size(std::int32_t w, std::int32_t h) noexcept : w_{std::max(w, 0)}, h_{std::max(h, 0)} {}

    [[nodiscard]] constexpr std::int32_t width() const noexcept { return w_; }
    [[nodiscard]] constexpr std::int32_t height() const noexcept { return h_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return w_ == 0 || h_ == 0; }

    friend constexpr bool operator==(const Size&, const Size&) = default;

private:
    std::int32_t w_{};
    std::int32_t h_{};
};

class Rect {
public:
    constexpr Rect() noexcept = default;
    constexpr Rect(Point origin, Size size) noexcept : origin_{origin}, size_{size} {}
    constexpr Rect(std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h) noexcept
        : origin_{x, y}, size_{w, h} {}

    [[nodiscard]] constexpr Point origin() const noexcept { return origin_; }
    [[nodiscard]] constexpr Size size() const noexcept { return size_; }
    [[nodiscard]] constexpr std::int32_t x() const noexcept { return origin_.x; }
    [[nodiscard]] constexpr std::int32_t y() const noexcept { return origin_.y; }
    [[nodiscard]] constexpr std::int32_t width() const noexcept { return size_.width(); }
    [[nodiscard]] constexpr std::int32_t height() const noexcept { return size_.height(); }
    [[nodiscard]] constexpr std::int32_t right() const noexcept { return sat_add(origin_.x, width()); }
    [[nodiscard]] constexpr std::int32_t bottom() const noexcept { return sat_add(origin_.y, height()); }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_.empty(); }

    // True when the point is inside. Computed in 64 bits, so it holds at the int32_t limits.
    [[nodiscard]] constexpr bool contains(Point p) const noexcept {
        const std::int64_t dx = std::int64_t{p.x} - std::int64_t{origin_.x};
        const std::int64_t dy = std::int64_t{p.y} - std::int64_t{origin_.y};
        return dx >= 0 && dy >= 0 && dx < std::int64_t{width()} && dy < std::int64_t{height()};
    }

    // The overlap, or an empty rectangle at this one's origin. Each overlap side is no longer than
    // the same side of either input, so the 64-bit difference always fits back into int32_t.
    [[nodiscard]] constexpr Rect intersect(const Rect& other) const noexcept {
        const std::int64_t left = std::max<std::int64_t>(origin_.x, other.origin_.x);
        const std::int64_t top = std::max<std::int64_t>(origin_.y, other.origin_.y);
        const std::int64_t far_x = std::min(std::int64_t{origin_.x} + width(), std::int64_t{other.origin_.x} + other.width());
        const std::int64_t far_y = std::min(std::int64_t{origin_.y} + height(), std::int64_t{other.origin_.y} + other.height());
        if (far_x <= left || far_y <= top) return Rect{origin_, Size{}};
        return Rect{saturate_to<std::int32_t>(left), saturate_to<std::int32_t>(top),
                    saturate_to<std::int32_t>(far_x - left), saturate_to<std::int32_t>(far_y - top)};
    }

    // Shrinks every side by the margin. The size never goes below zero.
    [[nodiscard]] constexpr Rect inset(std::int32_t margin) const noexcept {
        return Rect{sat_add(origin_.x, margin), sat_add(origin_.y, margin),
                    sat_sub(width(), sat_mul(margin, 2)), sat_sub(height(), sat_mul(margin, 2))};
    }

    friend constexpr bool operator==(const Rect&, const Rect&) = default;

private:
    Point origin_{};
    Size size_{};
};

}  // namespace jgui::inline v001
