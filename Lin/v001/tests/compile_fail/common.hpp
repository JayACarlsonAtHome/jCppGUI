// File: tests/compile_fail/common.hpp
// Purpose: What the compile-fail cases share: a minimal widget. Each case compiles as written, and
//          must fail with JGUI_BREAK defined, which adds one mistake and changes nothing else.
#pragma once

#include <expected>

#include "jCppGUI/v001/jgui.hpp"

template <class Tag>
struct Plain {
    using tag = Tag;
    [[nodiscard]] constexpr jgui::Rect bounds() const noexcept { return jgui::Rect{}; }
#ifndef JGUI_PLAIN_LACKS_PAINT
    constexpr void paint(jgui::Canvas&) {}
#endif
    template <class Shaper>
    constexpr std::expected<jgui::Rect, jgui::Error> layout(jgui::Rect area, const Shaper&) {
        return area;
    }
    template <class U, class E>
    constexpr bool handle(U&, const E&) {
        return false;
    }
    template <class U, class E>
    constexpr void notify(U&, const E&) {}
};

struct First;
struct Second;
struct Third;
