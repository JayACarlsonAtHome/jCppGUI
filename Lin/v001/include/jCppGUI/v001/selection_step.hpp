// File: include/jCppGUI/v001/selection_step.hpp
// Purpose: Moving a selection along a list of positions: which key means which step, and where a step lands.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "events.hpp"

namespace jgui::inline v001 {

enum class SelectionStep : std::uint8_t { Previous, Next, PageBack, PageForward, First, Last };

// The step a key stands for, if any.
[[nodiscard]] constexpr std::optional<SelectionStep> selection_step_for_key(Keysym key) noexcept {
    if (key == Key::Up) return SelectionStep::Previous;
    if (key == Key::Down) return SelectionStep::Next;
    if (key == Key::PageUp) return SelectionStep::PageBack;
    if (key == Key::PageDown) return SelectionStep::PageForward;
    if (key == Key::Home) return SelectionStep::First;
    if (key == Key::End) return SelectionStep::Last;
    return std::nullopt;
}

// The position after a step, in a list of count positions. An empty list has none. With nothing selected, Last lands on the
// last position and every other step on the first. A page of no positions counts as one.
[[nodiscard]] constexpr std::optional<std::size_t> stepped_selection(std::optional<std::size_t> current, SelectionStep step, std::size_t count, std::size_t page) noexcept {
    if (count == 0) return std::nullopt;
    const std::size_t last = count - 1;
    if (!current) return step == SelectionStep::Last ? last : std::size_t{0};
    const std::size_t from = *current < last ? *current : last;
    const std::size_t stride = page == 0 ? 1 : page;
    switch (step) {
        case SelectionStep::Previous: return from == 0 ? 0 : from - 1;
        case SelectionStep::Next: return from == last ? last : from + 1;
        case SelectionStep::PageBack: return from < stride ? 0 : from - stride;
        case SelectionStep::PageForward: return last - from < stride ? last : from + stride;
        case SelectionStep::First: return std::size_t{0};
        case SelectionStep::Last: return last;
        default: return from;
    }
}

}  // namespace jgui::inline v001
