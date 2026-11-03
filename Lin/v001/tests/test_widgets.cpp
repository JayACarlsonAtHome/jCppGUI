// File: tests/test_widgets.cpp
// Purpose: Tests 6 to 10, 12 and 13: click rules, self-destruction during a callback, late events,
//          capture and focus on dead items, button colours and the menu bar, with the real font.
#include <string>
#include <utility>

#include "app_helpers.hpp"
#include "check.hpp"

using jgui_test::Checker;
using jgui_test::centre;
using jgui_test::click;
using jgui_test::move_to;
using jgui_test::press;
using jgui_test::release;

namespace {

struct HelpButton;
struct Item;

constexpr jgui::Size window_size{jgui::Width{640}, jgui::Height{480}};

void test_click_rules(Checker& check) {
    int fired = 0;
    auto app = jgui::make_application(
        jgui::HeadlessBackend{window_size},
        jgui::window(jgui::window_frame(jgui::menu_bar(jgui::button<HelpButton>("Help", [&fired](auto&) -> void { ++fired; })))));
    check(app.has_value(), "application starts");
    check(app->process_pending().has_value(), "first frame");
    const jgui::Point on = centre(app->ui().get<HelpButton>().bounds());
    const jgui::Point off{jgui::Left{300}, jgui::Top{300}};

    // Test 6
    click(*app, on);
    check(fired == 1, "press and release on the button fires once");

    // Test 7
    press(*app, on);
    release(*app, off);
    check(fired == 1, "press on, release off fires nothing");
    press(*app, off);
    release(*app, on);
    check(fired == 1, "press off, release on fires nothing");

    // A drag off and back still counts, because the button held the pointer.
    press(*app, on);
    move_to(*app, off);
    release(*app, on);
    check(fired == 2, "press on, drag away and back, release on fires");

    // Keyboard: the button took focus from the click.
    jgui_test::backend(*app).inject(jgui::KeyEvent{jgui::Key::Return, jgui::ModifierMask{}, true});
    check(app->process_pending().has_value() && fired == 3, "Return on the focused button fires");
    jgui_test::backend(*app).inject(jgui::KeyEvent{jgui::Key::Return, jgui::ModifierMask{}, false});
    check(app->process_pending().has_value() && fired == 3, "a key release does not fire");
    press(*app, off);
    release(*app, off);
    jgui_test::backend(*app).inject(jgui::KeyEvent{jgui::Key::Return, jgui::ModifierMask{}, true});
    check(app->process_pending().has_value() && fired == 3, "a press elsewhere took the focus away");
}

// Test 8: the callback destroys its own widget and goes on using its captured state.
void test_self_destruction(Checker& check) {
    std::size_t seen = 0;
    auto kill = [&seen, text = std::string(100, 'x')](auto& u) -> void {
        u.template get<Item>().hide();
        seen = text.size();
    };
    using KillButton = jgui::Button<Item, decltype(kill)>;
    auto app = jgui::make_application(jgui::HeadlessBackend{window_size}, jgui::window(jgui::window_frame(jgui::Dynamic<Item, KillButton>{})));
    check(app.has_value(), "application starts");
    auto& node = app->ui().get<Item>();
    const jgui::Handle<KillButton> handle = node.create(jgui::CheckedText{"Kill"}, kill).value();
    check(app->process_pending().has_value(), "first frame");
    const jgui::Point on = centre(node.find(handle)->bounds());
    click(*app, on);
    check(seen == 100, "the callback finished using its own state");
    check(node.find(handle) == nullptr && !node.shown(), "the widget is gone");

    // Test 9: a late event at the same place does nothing, even with a new item in the old slot.
    seen = 0;
    click(*app, on);
    check(seen == 0, "a late event for a destroyed item does nothing");
    const jgui::Handle<KillButton> next = node.create(jgui::CheckedText{"Kill"}, kill).value();
    check(node.find(handle) == nullptr && node.find(next) != nullptr, "the old handle stays dead after reuse");
}

// Test 10
void test_capture_and_focus(Checker& check) {
    int fired = 0;
    auto fire = [&fired](auto&) -> void { ++fired; };
    using FireButton = jgui::Button<Item, decltype(fire)>;
    auto app = jgui::make_application(jgui::HeadlessBackend{window_size}, jgui::window(jgui::window_frame(jgui::Dynamic<Item, FireButton>{})));
    check(app.has_value(), "application starts");
    auto& node = app->ui().get<Item>();
    const jgui::Handle<FireButton> first = node.create(jgui::CheckedText{"One"}, fire).value();
    check(app->process_pending().has_value(), "first frame");
    const jgui::Point on = centre(node.find(first)->bounds());

    press(*app, on);
    check(node.captured() != nullptr && node.focused() != nullptr, "a press takes capture and focus");
    check(jgui_test::capture_active(app->ui()), "the tree reports the capture");
    node.destroy(first);
    check(node.captured() == nullptr, "capture is inert at once");
    check(node.focused() == nullptr, "focus is inert at once");
    check(!jgui_test::capture_active(app->ui()), "the tree no longer reports a capture");
    release(*app, on);
    jgui_test::backend(*app).inject(jgui::KeyEvent{jgui::Key::Return, jgui::ModifierMask{}, true});
    check(app->process_pending().has_value(), "events after the death are handled");
    check(fired == 0, "neither release nor key reached the dead item");

    const jgui::Handle<FireButton> second = node.create(jgui::CheckedText{"Two"}, fire).value();
    check(app->process_pending().has_value(), "the new item is laid out");
    check(node.captured() == nullptr && node.focused() == nullptr, "a new item does not inherit either");
    check(node.find(second) != nullptr, "the new item exists");
}

// Test 12
void test_button_colours(Checker& check) {
    auto app = jgui::make_application(
        jgui::HeadlessBackend{window_size},
        jgui::window(jgui::window_frame(jgui::menu_bar(jgui::button<HelpButton>("Help", [](auto&) -> void {})))));
    check(app.has_value() && app->process_pending().has_value(), "first frame");
    const jgui::Rect area = app->ui().get<HelpButton>().bounds();
    const jgui::Point sample = jgui_test::inside(area, 2, 2);
    const jgui::Point on = centre(area);

    const std::uint32_t normal = jgui_test::backend(*app).pixel(sample);
    move_to(*app, on);
    const std::uint32_t hover = jgui_test::backend(*app).pixel(sample);
    press(*app, on);
    const std::uint32_t pressed = jgui_test::backend(*app).pixel(sample);
    check(normal == jgui::theme::button_normal.argb, "normal colour");
    check(hover == jgui::theme::button_hover.argb, "hover colour");
    check(pressed == jgui::theme::button_pressed.argb, "pressed colour");
    check(normal != hover && hover != pressed && normal != pressed, "all three differ");
    move_to(*app, jgui::Point{jgui::Left{300}, jgui::Top{300}});
    check(jgui_test::backend(*app).pixel(sample) == jgui::theme::button_normal.argb, "dragged off, it no longer looks pressed");
}

// Test 13
void test_menu_bar(Checker& check) {
    auto app = jgui::make_application(
        jgui::HeadlessBackend{window_size},
        jgui::window(jgui::window_frame(jgui::menu_bar(jgui::button<HelpButton>("Help", [](auto&) -> void {})))));
    check(app.has_value(), "application starts");
    for (const jgui::Size size : {window_size, jgui::Size{jgui::Width{900}, jgui::Height{300}}, jgui::Size{jgui::Width{200}, jgui::Height{150}}, window_size}) {
        jgui_test::backend(*app).inject(jgui::ResizeEvent{size});
        check(app->process_pending().has_value(), "resize handled");
        const jgui::Rect client = jgui::WindowFrame<jgui::DefaultFrameMetrics>::client_rect(size);
        const jgui::Rect bar = app->ui().get<jgui::MenuBarTag>().bounds();
        const jgui::Rect target = app->ui().get<HelpButton>().bounds();
        check(bar.left() == client.left() && bar.width() == client.width(), "the bar has the full client width");
        check(bar.top() == client.top(), "the bar is directly below the frame's title strip");
        check(!target.empty() && target.intersect(bar) == target, "the button is inside the bar");
    }
}

// The redraw rule: nothing changed, no frame.
void test_redraw_only_when_damaged(Checker& check) {
    auto app = jgui::make_application(
        jgui::HeadlessBackend{window_size},
        jgui::window(jgui::window_frame(jgui::menu_bar(jgui::button<HelpButton>("Help", [](auto&) -> void {})))));
    check(app.has_value() && app->process_pending().has_value(), "first frame");
    const auto frames = app->frames();
    check(app->process_pending().has_value() && app->frames() == frames, "no events, no frame");
    move_to(*app, jgui::Point{jgui::Left{300}, jgui::Top{300}});
    check(app->frames() == frames, "motion over nothing, no frame");
    move_to(*app, centre(app->ui().get<HelpButton>().bounds()));
    check(app->frames() == frames + 1, "hover changed, one frame");
    move_to(*app, jgui::Point{jgui::Left{300}, jgui::Top{300}});
    const auto settled = app->frames();
    click(*app, jgui::Point{jgui::Left{300}, jgui::Top{300}});
    check(app->frames() == settled, "a click on nothing, no frame");
    jgui_test::backend(*app).inject(jgui::PointerLeave{});
    check(app->process_pending().has_value() && app->frames() == settled, "the pointer leaving nothing, no frame");
}

}  // namespace

int main() {
    Checker check;
    test_click_rules(check);
    test_self_destruction(check);
    test_capture_and_focus(check);
    test_button_colours(check);
    test_menu_bar(check);
    test_redraw_only_when_damaged(check);
    return check.finish();
}
