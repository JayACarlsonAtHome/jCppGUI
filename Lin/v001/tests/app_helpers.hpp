// File: tests/app_helpers.hpp
// Purpose: Pointer gestures for headless tests, and pixel queries on what was rendered.
#pragma once

#include <cstdint>

#include "jCppGUI/v001/headless_backend.hpp"
#include "jCppGUI/v001/jgui.hpp"
#include "test_access.hpp"

namespace jgui_test {

inline constexpr jgui::Serial test_serial{77};

template <class App>
void move_to(App& app, jgui::Point at) {
    jgui_test::backend(app).inject(jgui::PointerMove{at});
    static_cast<void>(app.process_pending());
}

template <class App>
void press(App& app, jgui::Point at) {
    jgui_test::backend(app).inject(jgui::PointerMove{at});
    jgui_test::backend(app).inject(jgui::PointerButton{at, jgui::MouseButton::Left, jgui::ButtonState::Pressed, test_serial});
    static_cast<void>(app.process_pending());
}

template <class App>
void release(App& app, jgui::Point at) {
    jgui_test::backend(app).inject(jgui::PointerMove{at});
    jgui_test::backend(app).inject(jgui::PointerButton{at, jgui::MouseButton::Left, jgui::ButtonState::Released, test_serial});
    static_cast<void>(app.process_pending());
}

template <class App>
void click(App& app, jgui::Point at) {
    press(app, at);
    release(app, at);
}

[[nodiscard]] constexpr jgui::Point centre(const jgui::Rect& area) {
    return inside(area, number(area.width()) / 2, number(area.height()) / 2);
}

// Pixels in the area that differ from the colour.
[[nodiscard]] inline int count_other_than(const jgui::HeadlessBackend& backend, const jgui::Rect& area, jgui::Color colour) {
    int count = 0;
    for (std::int32_t y = number(area.top()); y < number(area.bottom()); ++y)
        for (std::int32_t x = number(area.left()); x < number(area.right()); ++x)
            if (backend.pixel(jgui::Point{jgui::Left{x}, jgui::Top{y}}) != colour.argb) ++count;
    return count;
}

}  // namespace jgui_test
