// File: tests/test_frame.cpp
// Purpose: Test 11 and the frame's hit regions: the close box produces close, an absurd resize fails
//          cleanly, and strip and edges ask the backend for a move or resize with the press's serial.
#include <cstdint>
#include <expected>
#include <limits>
#include <utility>

#include "app_helpers.hpp"
#include "check.hpp"

using jgui_test::Checker;
using jgui_test::centre;
using jgui_test::click;
using jgui_test::press;
using jgui_test::release;

// Outside the anonymous namespace because only the Widget concept reads bounds, and a compiler rightly
// reports a member nobody calls on a type with internal linkage.
// Paints past its bounds on purpose: the frame's clip and paint order must keep it off the frame.
struct Overflowing {
    using tag = Overflowing;
    [[nodiscard]] constexpr jgui::Rect bounds() const noexcept { return jgui::Rect{}; }
    constexpr void paint(const jgui::TreeKey&, jgui::Canvas& canvas) { canvas.fill_rect(jgui::Rect{jgui::Point{jgui::Left{-100}, jgui::Top{-100}}, jgui::Size{jgui::Width{10000}, jgui::Height{10000}}}, jgui::Color{0xFF00FF00U}); }
    template <class Shaper>
    constexpr std::expected<jgui::Rect, jgui::Error> layout(const jgui::TreeKey&, jgui::Rect area, const Shaper&) { return area; }
    template <class U, class E>
    constexpr bool handle(const jgui::TreeKey&, U&, const E&) { return false; }
    template <class U, class E>
    constexpr void notify(const jgui::TreeKey&, U&, const E&) {}
};

namespace {

struct Unused;
using Frame = jgui::WindowFrame<jgui::DefaultFrameMetrics>;
constexpr jgui::Size window_size{jgui::Width{640}, jgui::Height{480}};

auto make_app() {
    return jgui::make_application(jgui::HeadlessBackend{window_size},
                                  jgui::window(jgui::window_frame(jgui::label<Unused>("x"))));
}

void test_close_box(Checker& check) {
    auto app = make_app();
    check(app.has_value() && app->process_pending().has_value(), "first frame");
    const jgui::Point box = centre(Frame::close_box(window_size));
    press(*app, box);
    release(*app, jgui::Point{jgui::Left{300}, jgui::Top{300}});
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
    for (const jgui::Size absurd : {jgui::Size{jgui::Width{hi}, jgui::Height{hi}}, jgui::Size{jgui::Width{8193}, jgui::Height{10}}, jgui::Size{jgui::Width{10}, jgui::Height{8193}}, jgui::Size{jgui::Width{65536}, jgui::Height{65536}}}) {
        jgui_test::backend(*app).inject(jgui::ResizeEvent{absurd});
        const auto result = app->process_pending();
        check(!result.has_value() && result.error().code == jgui::ErrorCode::SizeLimit, "the size is refused with an Error");
        check(app->size() == window_size, "the window keeps its size");
        check(jgui_test::backend(*app).size() == window_size, "no buffer was made for it");
    }
    check(app->frames() == frames, "no frame was drawn");
    jgui_test::backend(*app).inject(jgui::ResizeEvent{jgui::Size{jgui::Width{300}, jgui::Height{200}}});
    check(app->process_pending().has_value() && app->size() == jgui::Size{jgui::Width{300}, jgui::Height{200}}, "a good size still works afterwards");
}

void test_move_and_resize_requests(Checker& check) {
    auto app = make_app();
    check(app.has_value() && app->process_pending().has_value(), "first frame");
    const auto press_with = [&](jgui::Point at, std::uint32_t serial) {
        jgui_test::backend(*app).inject(jgui::PointerButton{at, jgui::MouseButton::Left, jgui::ButtonState::Pressed, jgui::Serial{serial}});
        check(app->process_pending().has_value(), "press handled");
    };
    press_with(jgui::Point{jgui::Left{300}, jgui::Top{12}}, 101);
    check(jgui_test::backend(*app).move_serial() == jgui::Serial{101}, "the title strip starts a move with the press's serial");
    press_with(jgui::Point{jgui::Left{1}, jgui::Top{240}}, 102);
    check(jgui_test::backend(*app).resize_serial() == jgui::Serial{102} && jgui_test::backend(*app).resize_edge() == jgui::ResizeEdge::LeftSide, "left edge");
    press_with(jgui::Point{jgui::Left{639}, jgui::Top{240}}, 103);
    check(jgui_test::backend(*app).resize_edge() == jgui::ResizeEdge::RightSide, "right edge");
    press_with(jgui::Point{jgui::Left{320}, jgui::Top{0}}, 104);
    check(jgui_test::backend(*app).resize_edge() == jgui::ResizeEdge::TopSide, "top edge");
    press_with(jgui::Point{jgui::Left{320}, jgui::Top{479}}, 105);
    check(jgui_test::backend(*app).resize_edge() == jgui::ResizeEdge::BottomSide, "bottom edge");
    press_with(jgui::Point{jgui::Left{0}, jgui::Top{0}}, 106);
    check(jgui_test::backend(*app).resize_edge() == jgui::ResizeEdge::TopLeft, "top-left corner");
    press_with(jgui::Point{jgui::Left{639}, jgui::Top{479}}, 107);
    check(jgui_test::backend(*app).resize_edge() == jgui::ResizeEdge::BottomRight, "bottom-right corner");
    check(jgui_test::backend(*app).move_serial() == jgui::Serial{101}, "edges did not start a move");
}

// The same rule as the static_asserts, at run time and with ASan watching: an old frame reaches nothing.
void test_stale_frames(Checker& check) {
    jgui::HeadlessBackend backend{jgui::Size{jgui::Width{4}, jgui::Height{4}}};
    auto older = backend.acquire_frame(jgui::Size{jgui::Width{4}, jgui::Height{4}});
    check(older.has_value() && backend.frame_pixels(*older).has_value(), "the current frame has pixels");
    check(!backend.acquire_frame(jgui::Size{jgui::Width{1 << 20}, jgui::Height{1 << 20}}).has_value(), "an oversized acquire fails");
    check(!backend.frame_pixels(*older).has_value(), "and has still ended the older frame");
    const auto presented = backend.present(std::move(*older), jgui::Rect{});
    check(!presented.has_value() && presented.error().code == jgui::ErrorCode::StaleFrame, "which present refuses");
    check(backend.presented() == 0, "so nothing was shown");
    auto newest = backend.acquire_frame(jgui::Size{jgui::Width{2}, jgui::Height{2}});
    check(newest.has_value() && backend.present(std::move(*newest), jgui::Rect{}).has_value(), "a new frame still works");
    check(backend.presented() == 1, "and was shown");
}

void test_content_stays_inside(Checker& check) {
    auto app = jgui::make_application(jgui::HeadlessBackend{window_size}, jgui::window(jgui::window_frame(Overflowing{})));
    check(app.has_value() && app->process_pending().has_value(), "first frame");
    const jgui::Rect client = Frame::client_rect(window_size);
    constexpr jgui::Color content{0xFF00FF00U};
    check(jgui_test::count_other_than(jgui_test::backend(*app), client, content) == 0, "the content fills the client area");
    int outside = 0;
    for (std::int32_t y = 0; y < jgui_test::number(window_size.height()); ++y)
        for (std::int32_t x = 0; x < jgui_test::number(window_size.width()); ++x)
            if (!client.contains(jgui::Point{jgui::Left{x}, jgui::Top{y}}) && jgui_test::backend(*app).pixel(jgui::Point{jgui::Left{x}, jgui::Top{y}}) == content.argb) ++outside;
    check(outside == 0, "and nothing outside it");
    check(jgui_test::backend(*app).pixel(jgui::Point{jgui::Left{300}, jgui::Top{12}}) == jgui::theme::frame_strip.argb, "the title strip is on top");
}

}  // namespace

int main() {
    Checker check;
    test_close_box(check);
    test_absurd_resize(check);
    test_move_and_resize_requests(check);
    test_stale_frames(check);
    test_content_stays_inside(check);
    return check.finish();
}
