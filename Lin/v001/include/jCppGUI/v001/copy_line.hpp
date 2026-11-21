// File: include/jCppGUI/v001/copy_line.hpp
// Purpose: CopyLine<Tag>: a Label whose text, all of it, including what is cut off at the window's edge, is offered to the clipboard when clicked. It goes through the sanitizer, and may wait there for a person's answer. When it
//          has gone, this shows so until the text changes or the pointer leaves it.
#pragma once

#include <expected>
#include <optional>
#include <string_view>
#include <utility>

#include "label.hpp"
#include "widget.hpp"

namespace jgui::inline v001 {

template <class Tag>
class CopyLine {
public:
    using tag = Tag;

    constexpr explicit CopyLine(CheckedText text) : label_{text} {}
    CopyLine(const CopyLine&) = delete;
    CopyLine& operator=(const CopyLine&) = delete;
    constexpr CopyLine(CopyLine&&) = default;
    CopyLine& operator=(CopyLine&&) = delete;
    constexpr ~CopyLine() = default;

    [[nodiscard]] constexpr Rect bounds() const noexcept { return label_.bounds(); }
    [[nodiscard]] constexpr std::string_view text() const noexcept { return label_.text(); }
    [[nodiscard]] constexpr bool just_copied() const noexcept { return copied_; }
    [[nodiscard]] constexpr const std::optional<ErrorCode>& text_error() const noexcept { return label_.text_error(); }

    [[nodiscard]] constexpr std::expected<void, Error> set_text(std::string_view text) {
        auto changed = label_.set_text(text);
        if (changed) copied_ = false;
        return changed;
    }

    template <TextShaper Shaper>
    constexpr std::expected<Rect, Error> layout(const TreeKey& key, Rect area, const Shaper& engine) {
        return label_.layout(key, area, engine);
    }

    constexpr void paint(const TreeKey& key, Canvas& canvas) {
        if (copied_) canvas.clipped(bounds()).fill_rect(bounds(), theme::button_hover);
        label_.paint(key, canvas);
    }

    template <class U>
    constexpr bool handle(const TreeKey&, U& ui, const PointerPress& event) {
        if (event.button != MouseButton::Left || !bounds().contains(event.position) || text().empty()) return false;
        copied_ = ui.propose_copy_text(event.serial, text());
        damaged_ = true;
        return true;
    }

    template <class U>
    constexpr bool handle(const TreeKey&, U&, const PointerMove& event) {
        if (copied_ && !bounds().contains(event.position)) stop_showing();
        return false;
    }

    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const QueryDamage& event) {
        return label_.handle(key, ui, event) || damaged_;
    }

    template <class U, class E>
    constexpr bool handle(const TreeKey&, U&, const E&) {
        return false;
    }

    template <class U>
    constexpr void notify(const TreeKey&, U&, const PointerLeave&) {
        if (copied_) stop_showing();
    }

    template <class U>
    constexpr void notify(const TreeKey& key, U& ui, const ClearDamage& event) {
        damaged_ = false;
        label_.notify(key, ui, event);
    }

    template <class U, class E>
    constexpr void notify(const TreeKey&, U&, const E&) {}

private:
    constexpr void stop_showing() noexcept {
        copied_ = false;
        damaged_ = true;
    }

    Label<Tag> label_;
    bool copied_{};
    bool damaged_{};
};

}  // namespace jgui::inline v001
