// File: tests/test_frame.cpp
// Purpose: Test 11 and the frame's hit regions: the close box produces close, an absurd resize fails
//          cleanly, and strip and edges ask the backend for a move or resize with the press's serial.
#include <limits>

#include "app_helpers.hpp"
#include "check.hpp"

using jgui_test::Checker;
using jgui_test::centre;
using jgui_test::click;
using jgui_test::press;
using jgui_test::release;

namespace {

struct Unused;
constexpr jgui::Size window_size{640, 480};

auto make_app() {
    return jgui::make_application(jgui::HeadlessBackend{window_size},
                                  jgui::window(jgui::label<Unused>("x")));
}

void test_close_box(Checker& check) {
    auto app = make_app();
    check(app.has_value() && app->process_pending().has_value(), "first frame");
    const jgui::Point box = centre(jgui::WindowFrame::close_box(window_size));
    press(*app, box);
    release(*app, jgui::Point{300, 300});
    check(!app->closed(), "press on the box, release off it: no close");
    click(*app, box);
    check(app->closed(), "a click on the close box produces close");
    check(app->run().has_value(), "run returns at once after close");
}

void test_absurd_resize(Checker& check) {
    auto app = make_app();
    check(app.has_value() && app->process_pending().has_value(), "first frame");
    const auto frames = app->frames();
    const std::int32_t hi = std::numeric_limits<std::int32_t>::max();
    for (const jgui::Size absurd : {jgui::Size{hi, hi}, jgui::Size{16385, 10}, jgui::Size{10, 16385}, jgui::Size{65536, 65536}}) {
        app->backend().inject(jgui::ResizeEvent{absurd});
        const auto result = app->process_pending();
        check(!result.has_value() && result.error().code == jgui::ErrorCode::SizeLimit, "the size is refused with an Error");
        check(app->size() == window_size, "the window keeps its size");
        check(app->backend().size() == window_size, "no buffer was made for it");
    }
    check(app->frames() == frames, "no frame was drawn");
    app->backend().inject(jgui::ResizeEvent{jgui::Size{300, 200}});
    check(app->process_pending().has_value() && app->size() == jgui::Size{300, 200}, "a good size still works afterwards");
}

void test_move_and_resize_requests(Checker& check) {
    auto app = make_app();
    check(app.has_value() && app->process_pending().has_value(), "first frame");
    const auto press_with = [&](jgui::Point at, std::uint32_t serial) {
        app->backend().inject(jgui::PointerButton{at, jgui::MouseButton::Left, jgui::ButtonState::Pressed, serial});
        check(app->process_pending().has_value(), "press handled");
    };
    press_with(jgui::Point{300, 12}, 101);
    check(app->backend().move_serial() == 101U, "the title strip starts a move with the press's serial");
    press_with(jgui::Point{1, 240}, 102);
    check(app->backend().resize_serial() == 102U && app->backend().resize_edge() == jgui::ResizeEdge::Left, "left edge");
    press_with(jgui::Point{639, 240}, 103);
    check(app->backend().resize_edge() == jgui::ResizeEdge::Right, "right edge");
    press_with(jgui::Point{320, 0}, 104);
    check(app->backend().resize_edge() == jgui::ResizeEdge::Top, "top edge");
    press_with(jgui::Point{320, 479}, 105);
    check(app->backend().resize_edge() == jgui::ResizeEdge::Bottom, "bottom edge");
    press_with(jgui::Point{0, 0}, 106);
    check(app->backend().resize_edge() == jgui::ResizeEdge::TopLeft, "top-left corner");
    press_with(jgui::Point{639, 479}, 107);
    check(app->backend().resize_edge() == jgui::ResizeEdge::BottomRight, "bottom-right corner");
    check(app->backend().move_serial() == 101U, "edges did not start a move");
}

}  // namespace

int main() {
    Checker check;
    test_close_box(check);
    test_absurd_resize(check);
    test_move_and_resize_requests(check);
    return check.finish();
}
