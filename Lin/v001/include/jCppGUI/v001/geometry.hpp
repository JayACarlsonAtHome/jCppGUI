// File: include/jCppGUI/v001/geometry.hpp
// Purpose: Six position and length types, and Point, Size and Rect built only from them. There is no implicit
//          conversion to or from an integer or between the six, arithmetic saturates, and a length is never
//          negative. Only the code that must index memory, and the boundaries in src/, read the number back.
#pragma once

#include <algorithm>
#include <compare>
#include <concepts>
#include <cstddef>
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

class Rect;
class PixelBuffer;
class Canvas;
class TextRun;

namespace detail {
struct LeftTag;
struct RightTag;
struct TopTag;
struct BottomTag;
struct WidthTag;
struct HeightTag;

// The arithmetic below, and nothing else: it gives back only another geometry value.
struct GeometryArithmetic;
// Reads the number. It is defined only in src/geometry_access.hpp, so nothing above the backend line can use it.
struct GeometryAccess;
// Reads the number to write it in a jText file. It is defined only in jtext_writer.hpp.
struct FileNumbers;
// Reads the number to show it as the text of a cell. It is defined only in cell_text.hpp.
struct CellNumbers;
// Defined only in tests/test_access.hpp.
struct TestAccess;
}  // namespace detail

// A position along one axis. The tag makes each edge its own type.
template <class Tag>
class Edge {
public:
    constexpr Edge() noexcept = default;
    // Only an exact int32_t, so nothing narrows or converts on the way in.
    template <std::same_as<std::int32_t> I>
    constexpr explicit Edge(I value) noexcept : value_{value} {}

    friend constexpr bool operator==(const Edge&, const Edge&) = default;
    friend constexpr auto operator<=>(const Edge&, const Edge&) = default;

private:
    friend class Rect;
    friend class PixelBuffer;
    friend class Canvas;
    friend class TextRun;
    friend struct detail::GeometryArithmetic;
    friend struct detail::GeometryAccess;
    friend struct detail::FileNumbers;
    friend struct detail::CellNumbers;
    friend struct detail::TestAccess;

    std::int32_t value_{};
};

// A length along one axis, never negative: a negative value is clamped to zero.
template <class Tag>
class Extent {
public:
    constexpr Extent() noexcept = default;
    template <std::same_as<std::int32_t> I>
    constexpr explicit Extent(I value) noexcept : value_{std::max(value, 0)} {}

    friend constexpr bool operator==(const Extent&, const Extent&) = default;
    friend constexpr auto operator<=>(const Extent&, const Extent&) = default;

    [[nodiscard]] friend constexpr Extent operator+(Extent a, Extent b) noexcept {
        return Extent{sat_add(a.value_, b.value_)};
    }
    [[nodiscard]] friend constexpr Extent operator-(Extent a, Extent b) noexcept {
        return Extent{sat_sub(a.value_, b.value_)};
    }

    // The length repeated this many times, saturating.
    [[nodiscard]] friend constexpr Extent repeated_extent(Extent unit, std::size_t count) noexcept {
        if (unit.value_ == 0) return Extent{};
        const std::size_t most = static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() / unit.value_);
        return count > most ? Extent{std::numeric_limits<std::int32_t>::max()} : Extent{static_cast<std::int32_t>(count) * unit.value_};
    }

    // How many whole units fit in the length. None when the unit is empty.
    [[nodiscard]] friend constexpr std::size_t whole_units_in(Extent total, Extent unit) noexcept {
        return unit.value_ == 0 ? 0 : static_cast<std::size_t>(total.value_ / unit.value_);
    }

    // The length, measured on a scale of "from", on a scale of "to", in 64 bits. Zero when "from" is empty.
    [[nodiscard]] friend constexpr Extent rescaled_extent(Extent value, Extent from, Extent to) noexcept {
        if (from.value_ == 0) return Extent{};
        return Extent{saturate_to<std::int32_t>(std::int64_t{value.value_} * std::int64_t{to.value_} / std::int64_t{from.value_})};
    }

private:
    friend class Rect;
    friend class PixelBuffer;
    friend class Canvas;
    friend class TextRun;
    friend struct detail::GeometryArithmetic;
    friend struct detail::GeometryAccess;
    friend struct detail::FileNumbers;
    friend struct detail::CellNumbers;
    friend struct detail::TestAccess;

    std::int32_t value_{};
};

using Left = Edge<detail::LeftTag>;
using Right = Edge<detail::RightTag>;
using Top = Edge<detail::TopTag>;
using Bottom = Edge<detail::BottomTag>;
using Width = Extent<detail::WidthTag>;
using Height = Extent<detail::HeightTag>;

namespace detail {

template <class T>
inline constexpr bool is_geometry_value = false;
template <class Tag>
inline constexpr bool is_geometry_value<Edge<Tag>> = true;
template <class Tag>
inline constexpr bool is_geometry_value<Extent<Tag>> = true;

struct GeometryArithmetic {
    template <class Result, class A, class B>
        requires is_geometry_value<Result>
    [[nodiscard]] static constexpr Result sum(A a, B b) noexcept {
        return Result{sat_add(a.value_, b.value_)};
    }
    template <class Result, class A, class B>
        requires is_geometry_value<Result>
    [[nodiscard]] static constexpr Result difference(A a, B b) noexcept {
        return Result{sat_sub(a.value_, b.value_)};
    }
    template <class Result, class A>
        requires is_geometry_value<Result>
    [[nodiscard]] static constexpr Result same_place(A a) noexcept {
        return Result{a.value_};
    }
};

}  // namespace detail

// An edge plus a length is the far edge, and the far edge minus the near one is the length.
[[nodiscard]] constexpr Right operator+(Left near, Width span) noexcept { return detail::GeometryArithmetic::sum<Right>(near, span); }
[[nodiscard]] constexpr Bottom operator+(Top near, Height span) noexcept { return detail::GeometryArithmetic::sum<Bottom>(near, span); }
[[nodiscard]] constexpr Width operator-(Right far, Left near) noexcept { return detail::GeometryArithmetic::difference<Width>(far, near); }
[[nodiscard]] constexpr Height operator-(Bottom far, Top near) noexcept { return detail::GeometryArithmetic::difference<Height>(far, near); }
[[nodiscard]] constexpr Left operator-(Right far, Width span) noexcept { return detail::GeometryArithmetic::difference<Left>(far, span); }
[[nodiscard]] constexpr Top operator-(Bottom far, Height span) noexcept { return detail::GeometryArithmetic::difference<Top>(far, span); }

// A far edge is one past the last column or row, so what comes next starts exactly there. Moving an edge by a
// length is written with these too: left_after(left + padding).
[[nodiscard]] constexpr Left left_after(Right far) noexcept { return detail::GeometryArithmetic::same_place<Left>(far); }
[[nodiscard]] constexpr Top top_after(Bottom far) noexcept { return detail::GeometryArithmetic::same_place<Top>(far); }

class Point {
public:
    constexpr Point() noexcept = default;
    constexpr Point(Left left, Top top) noexcept : left_{left}, top_{top} {}

    [[nodiscard]] constexpr Left left() const noexcept { return left_; }
    [[nodiscard]] constexpr Top top() const noexcept { return top_; }

    friend constexpr bool operator==(const Point&, const Point&) = default;

private:
    Left left_{};
    Top top_{};
};

class Size {
public:
    constexpr Size() noexcept = default;
    constexpr Size(Width width, Height height) noexcept : width_{width}, height_{height} {}

    [[nodiscard]] constexpr Width width() const noexcept { return width_; }
    [[nodiscard]] constexpr Height height() const noexcept { return height_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return width_ == Width{} || height_ == Height{}; }

    friend constexpr bool operator==(const Size&, const Size&) = default;

private:
    Width width_{};
    Height height_{};
};

class Rect {
public:
    constexpr Rect() noexcept = default;
    constexpr Rect(Point origin, Size size) noexcept : origin_{origin}, size_{size} {}

    [[nodiscard]] constexpr Point origin() const noexcept { return origin_; }
    [[nodiscard]] constexpr Size size() const noexcept { return size_; }
    [[nodiscard]] constexpr Left left() const noexcept { return origin_.left(); }
    [[nodiscard]] constexpr Top top() const noexcept { return origin_.top(); }
    [[nodiscard]] constexpr Width width() const noexcept { return size_.width(); }
    [[nodiscard]] constexpr Height height() const noexcept { return size_.height(); }
    [[nodiscard]] constexpr Right right() const noexcept { return left() + width(); }
    [[nodiscard]] constexpr Bottom bottom() const noexcept { return top() + height(); }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_.empty(); }

    // True when the point is inside. Computed in 64 bits, so it holds at the int32_t limits.
    [[nodiscard]] constexpr bool contains(Point p) const noexcept {
        const std::int64_t dx = std::int64_t{p.left().value_} - std::int64_t{left().value_};
        const std::int64_t dy = std::int64_t{p.top().value_} - std::int64_t{top().value_};
        return dx >= 0 && dy >= 0 && dx < std::int64_t{width().value_} && dy < std::int64_t{height().value_};
    }

    // The overlap, or an empty rectangle at this one's origin. Each overlap side is no longer than
    // the same side of either input, so the 64-bit difference always fits back into int32_t.
    [[nodiscard]] constexpr Rect intersect(const Rect& other) const noexcept {
        const std::int64_t near_x = std::max(std::int64_t{left().value_}, std::int64_t{other.left().value_});
        const std::int64_t near_y = std::max(std::int64_t{top().value_}, std::int64_t{other.top().value_});
        const std::int64_t far_x = std::min(std::int64_t{left().value_} + width().value_, std::int64_t{other.left().value_} + other.width().value_);
        const std::int64_t far_y = std::min(std::int64_t{top().value_} + height().value_, std::int64_t{other.top().value_} + other.height().value_);
        if (far_x <= near_x || far_y <= near_y) return Rect{origin_, Size{}};
        return Rect{Point{Left{saturate_to<std::int32_t>(near_x)}, Top{saturate_to<std::int32_t>(near_y)}},
                    Size{Width{saturate_to<std::int32_t>(far_x - near_x)}, Height{saturate_to<std::int32_t>(far_y - near_y)}}};
    }

    // Shrinks each side by the margin on that axis. The size never goes below zero.
    [[nodiscard]] constexpr Rect inset(Width across, Height down) const noexcept {
        return Rect{Point{left_after(left() + across), top_after(top() + down)}, Size{width() - across - across, height() - down - down}};
    }

    friend constexpr bool operator==(const Rect&, const Rect&) = default;

private:
    Point origin_{};
    Size size_{};
};

}  // namespace jgui::inline v001
