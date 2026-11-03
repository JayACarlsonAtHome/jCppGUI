// File: include/jCppGUI/v001/spans.hpp
// Purpose: Lengths laid end to end, such as columns across a window, and items of one height down it: their total, what is
//          at a position, which boundary is near one, and which of them show in a view. Each answer comes from typed values.
#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>

#include "geometry.hpp"

namespace jgui::inline v001 {

// All the widths, end to end.
[[nodiscard]] constexpr Width total_width(std::span<const Width> widths) noexcept {
    Width total{};
    for (const Width width : widths) total = total + width;
    return total;
}

// Which width holds this position, when the first starts at origin. Empty when none does.
[[nodiscard]] constexpr std::optional<std::size_t> span_index_at(std::span<const Width> widths, Left origin, Left at) noexcept {
    Left start = origin;
    for (std::size_t i = 0; i < widths.size(); ++i) {
        const Left next = left_after(start + widths[i]);
        if (at >= start && at < next) return i;
        start = next;
    }
    return std::nullopt;
}

// Which width's far boundary this position is within reach of. The nearest wins, and the later of two at the same distance.
[[nodiscard]] constexpr std::optional<std::size_t> boundary_near(std::span<const Width> widths, Left origin, Left at, Width reach) noexcept {
    std::optional<std::size_t> nearest{};
    Width nearest_distance{};
    Left start = origin;
    for (std::size_t i = 0; i < widths.size(); ++i) {
        const Right boundary = start + widths[i];
        start = left_after(boundary);
        const Width distance = at < start ? boundary - at : (at + Width{}) - start;
        if (distance < reach && (!nearest || distance <= nearest_distance)) {
            nearest = i;
            nearest_distance = distance;
        }
    }
    return nearest;
}

// A width being dragged by its far boundary: what it was, plus how far the pointer has come, within the limits.
[[nodiscard]] constexpr Width dragged_width(Left pressed_at, Width width_at_press, Left now, Width least, Width most) noexcept {
    return std::clamp((now + width_at_press) - pressed_at, least, most);
}

// A run of items.
struct ItemRange {
    std::size_t first{};
    std::size_t count{};
    friend constexpr bool operator==(const ItemRange&, const ItemRange&) = default;
};

// Which items of one height show in a view scrolled this far. The cost does not depend on how many there are.
[[nodiscard]] constexpr ItemRange visible_item_range(Height offset, Height view, Height item, std::size_t count) noexcept {
    const std::size_t first = whole_units_in(offset, item);
    if (first >= count || view == Height{} || item == Height{}) return ItemRange{count, 0};
    const std::size_t past_last = whole_units_in((offset + view) - Height{1}, item) + 1;
    return ItemRange{first, std::min(past_last, count) - first};
}

// Which item this far down the content is. Empty below the last of them.
[[nodiscard]] constexpr std::optional<std::size_t> item_index_at(Height distance, Height item, std::size_t count) noexcept {
    if (item == Height{}) return std::nullopt;
    const std::size_t index = whole_units_in(distance, item);
    if (index >= count) return std::nullopt;
    return index;
}

}  // namespace jgui::inline v001
