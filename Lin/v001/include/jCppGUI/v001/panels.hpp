// File: include/jCppGUI/v001/panels.hpp
// Purpose: Where panels go inside a parent. Each function takes the parent's Rect and returns a Rect: the whole of it, a half or
//          a quarter. With an odd size, the left half and the top half take the extra pixel, so two halves always meet with
//          no gap or overlap and together make exactly the parent. A quarter is a half of a half. split_remaining_space shares
//          one axis among an ordered list of panels. Helpers only: there is no panel widget, splitter or layer here.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "geometry.hpp"

namespace jgui::inline v001 {

// A length cut in two: the smaller side, and the side with the extra pixel when the length is odd.
template <class Tag>
[[nodiscard]] constexpr Extent<Tag> smaller_half_of(Extent<Tag> whole) noexcept {
    return rescaled_extent(whole, Extent<Tag>{2}, Extent<Tag>{1});
}
template <class Tag>
[[nodiscard]] constexpr Extent<Tag> larger_half_of(Extent<Tag> whole) noexcept {
    return whole - smaller_half_of(whole);
}

[[nodiscard]] constexpr Rect full_of(const Rect& parent) noexcept { return parent; }

[[nodiscard]] constexpr Rect left_half_of(const Rect& parent) noexcept {
    return Rect{parent.origin(), Size{larger_half_of(parent.width()), parent.height()}};
}
[[nodiscard]] constexpr Rect right_half_of(const Rect& parent) noexcept {
    return Rect{Point{left_after(left_half_of(parent).right()), parent.top()}, Size{smaller_half_of(parent.width()), parent.height()}};
}
[[nodiscard]] constexpr Rect top_half_of(const Rect& parent) noexcept {
    return Rect{parent.origin(), Size{parent.width(), larger_half_of(parent.height())}};
}
[[nodiscard]] constexpr Rect bottom_half_of(const Rect& parent) noexcept {
    return Rect{Point{parent.left(), top_after(top_half_of(parent).bottom())}, Size{parent.width(), smaller_half_of(parent.height())}};
}

[[nodiscard]] constexpr Rect top_left_quarter_of(const Rect& parent) noexcept { return top_half_of(left_half_of(parent)); }
[[nodiscard]] constexpr Rect top_right_quarter_of(const Rect& parent) noexcept { return top_half_of(right_half_of(parent)); }
[[nodiscard]] constexpr Rect bottom_left_quarter_of(const Rect& parent) noexcept { return bottom_half_of(left_half_of(parent)); }
[[nodiscard]] constexpr Rect bottom_right_quarter_of(const Rect& parent) noexcept { return bottom_half_of(right_half_of(parent)); }

// How much of one axis a panel wants. Filling is what a default-made one does, and a fixed size must be asked for by name.
template <class Tag>
class PanelSize {
public:
    constexpr PanelSize() noexcept = default;
    [[nodiscard]] static constexpr PanelSize fixed(Extent<Tag> size) noexcept {
        PanelSize panel;
        panel.fixed_ = size;
        return panel;
    }
    [[nodiscard]] constexpr const std::optional<Extent<Tag>>& fixed_size() const noexcept { return fixed_; }

private:
    std::optional<Extent<Tag>> fixed_{};
};

// Each panel's size along an axis of this length, in the order given. Fixed panels take their size first, in order, and filling
// panels share what is left equally, with the earlier ones taking any extra pixels. So nothing overlaps, and with one filling
// panel or more the sizes add up to exactly the length. When fixed sizes ask for more than there is, each is clipped to what the
// ones before left, which can be nothing, and no size goes negative. With no filling panel, whatever the fixed ones do not
// take stays unused, after the last of them.
template <class Tag>
[[nodiscard]] constexpr std::vector<Extent<Tag>> split_remaining_space(Extent<Tag> length, std::span<const PanelSize<Tag>> panels) {
    std::vector<Extent<Tag>> sizes(panels.size());
    Extent<Tag> left_over = length;
    std::size_t filling = 0;
    for (std::size_t i = 0; i < panels.size(); ++i) {
        if (const auto& wanted = panels[i].fixed_size(); wanted) {
            sizes[i] = std::min(*wanted, left_over);
            left_over = left_over - sizes[i];
        } else {
            ++filling;
        }
    }
    if (filling == 0) return sizes;
    const Extent<Tag> share = rescaled_extent(left_over, Extent<Tag>{saturate_to<std::int32_t>(filling)}, Extent<Tag>{1});
    std::size_t extra_pixels = whole_units_in(left_over - repeated_extent(share, filling), Extent<Tag>{1});
    for (std::size_t i = 0; i < panels.size(); ++i) {
        if (panels[i].fixed_size()) continue;
        sizes[i] = share;
        if (extra_pixels > 0) {
            sizes[i] = sizes[i] + Extent<Tag>{1};
            --extra_pixels;
        }
    }
    return sizes;
}

}  // namespace jgui::inline v001
