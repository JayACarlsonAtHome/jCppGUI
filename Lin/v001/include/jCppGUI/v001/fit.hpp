// File: include/jCppGUI/v001/fit.hpp
// Purpose: How a window too small for what it holds is made to fit. Each layout pass is given a FitText: the shaper it
//          wraps, the padding to use, and a tally that a row of buttons adds to when it is short of room. The
//          application tries, in order: the window as it is; a bigger window, where the compositor says there is room;
//          tight padding; smaller text, down to TextEngine::smallest_pixel_size; and then it says the screen is too
//          small. A widget reads its padding and reports through the free functions here, so a plain shaper, as a
//          test passes, still lays out with the theme's padding and reports to no one.
#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <string_view>

#include "error.hpp"
#include "geometry.hpp"
#include "text.hpp"
#include "widget.hpp"

namespace jgui::inline v001 {

// The space a row of buttons gives each button, either side of its caption, and leaves at each end of the row.
struct FitPadding {
    Width button_x{};
    Width bar_x{};
    friend constexpr bool operator==(const FitPadding&, const FitPadding&) = default;
};
inline constexpr FitPadding normal_padding{theme::button_pad_x, theme::bar_pad_x};
inline constexpr FitPadding tight_padding{Width{3}, Width{1}};

// How far short of room the rows of one layout pass were, added up.
class FitTally {
public:
    constexpr void add(Width by) noexcept {
        if (by > Width{}) short_by_ = short_by_ + by;
    }
    [[nodiscard]] constexpr Width short_by() const noexcept { return short_by_; }

private:
    Width short_by_{};
};

// A shaper for one layout pass. It shapes as the shaper it wraps does, and adds the padding and the tally.
template <TextShaper S>
class FitText {
public:
    constexpr FitText(const S& text, FitPadding padding, FitTally& tally) noexcept : text_{text}, padding_{padding}, tally_{tally} {}

    [[nodiscard]] constexpr std::expected<TextRun, Error> shape(std::string_view text) const { return text_.get().shape(text); }
    [[nodiscard]] constexpr Height line_height() const { return text_.get().line_height(); }
    [[nodiscard]] constexpr auto monospace_face() const
        requires HasMonospaceFace<S>
    {
        return text_.get().monospace_face();
    }
    [[nodiscard]] constexpr std::int32_t pixel_size() const noexcept { return detail::font_key(text_.get()); }

    [[nodiscard]] constexpr FitPadding padding() const noexcept { return padding_; }
    constexpr void short_of(Width by) const noexcept { tally_.get().add(by); }

private:
    std::reference_wrapper<const S> text_;
    FitPadding padding_;
    std::reference_wrapper<FitTally> tally_;
};

// The padding this pass lays out with: the theme's, unless the shaper is a FitText.
template <TextShaper Shaper>
[[nodiscard]] constexpr FitPadding padding_of(const Shaper& text) noexcept {
    if constexpr (requires { { text.padding() } -> std::same_as<FitPadding>; }) {
        return text.padding();
    } else {
        return normal_padding;
    }
}

// A row that needed more room than it was given says how much. Only a FitText hears it.
template <TextShaper Shaper>
constexpr void report_short(const Shaper& text, Width by) noexcept {
    if constexpr (requires { text.short_of(by); }) text.short_of(by);
}

namespace detail {

// What a widget in a row wanted across: a button's own wish, and anything else the width it took.
template <class W>
[[nodiscard]] constexpr Width wanted_across(const W& widget) noexcept {
    if constexpr (requires { { widget.wanted_width() } -> std::same_as<Width>; }) {
        return widget.wanted_width();
    } else {
        return widget.bounds().width();
    }
}

}  // namespace detail

// After a row is laid out in room this wide: what its widgets wanted beyond that, reported.
template <TextShaper Shaper, class... W>
constexpr void report_row(const Shaper& text, Width room, const W&... widgets) noexcept {
    const Width wanted = (Width{} + ... + detail::wanted_across(widgets));
    if (wanted > room) report_short(text, wanted - room);
}

}  // namespace jgui::inline v001
