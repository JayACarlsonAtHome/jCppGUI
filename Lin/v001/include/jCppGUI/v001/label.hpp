// File: include/jCppGUI/v001/label.hpp
// Purpose: Label<Tag>: one line of text. It takes a full-width row and leaves what is below it.
#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "widget.hpp"

namespace jgui::inline v001 {

template <class Tag>
class Label {
public:
    using tag = Tag;

    constexpr explicit Label(std::string_view text) : text_{text} {}

    [[nodiscard]] constexpr Rect bounds() const noexcept { return bounds_; }
    [[nodiscard]] constexpr std::string_view text() const noexcept { return text_; }

    // The new text is checked by the next layout, which reports any Error.
    constexpr void set_text(std::string_view text) {
        text_ = text;
        run_.reset();
        damaged_ = true;
    }

    template <TextShaper Shaper>
    constexpr std::expected<Rect, Error> layout(Rect area, const Shaper& engine) {
        if (!run_) {
            auto shaped = engine.shape(text_);
            if (!shaped) return std::unexpected(std::move(shaped.error()));
            run_ = std::move(*shaped);
        }
        const std::int32_t row = sat_add(run_->size().height(), sat_mul(theme::label_pad_y, 2));
        bounds_ = area.intersect(Rect{area.origin(), Size{area.width(), row}});
        return Rect{area.x(), bounds_.bottom(), area.width(), sat_sub(area.height(), bounds_.height())};
    }

    constexpr void paint(Canvas& canvas) {
        if (!run_) return;
        const Canvas local = canvas.clipped(bounds_);
        const Point at{sat_add(bounds_.x(), theme::label_pad_x), sat_add(bounds_.y(), theme::label_pad_y)};
        local.draw_text(at, *run_, theme::text);
    }

    template <class U>
    constexpr bool handle(U&, const QueryDamage&) {
        return damaged_;
    }

    template <class U>
    constexpr void notify(U&, const ClearDamage&) {
        damaged_ = false;
    }

    template <class U, class E>
    constexpr bool handle(U&, const E&) {
        return false;
    }

    template <class U, class E>
    constexpr void notify(U&, const E&) {}

private:
    std::string text_;
    std::optional<TextRun> run_;
    Rect bounds_{};
    bool damaged_{};
};

template <class Tag>
[[nodiscard]] constexpr Label<Tag> label(std::string_view text) {
    return Label<Tag>{text};
}

}  // namespace jgui::inline v001
