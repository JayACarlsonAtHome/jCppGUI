// File: tests/test_help.cpp
// Purpose: Test 14 on the demo's real tree, with the real font: clicking Help shows the line and
//          clicking again hides it, checked by rendered pixels. Also two independent applications.
#include "app_helpers.hpp"
#include "check.hpp"
#include "help_ui.hpp"

using jgui_test::Checker;
using jgui_test::centre;
using jgui_test::click;
using jgui_test::count_other_than;

namespace {

constexpr jgui::Size window_size{640, 480};

template <class App>
jgui::Rect area_below_bar(App& app) {
    const jgui::Rect bar = app.ui().template get<jgui::MenuBarTag>().bounds();
    const jgui::Rect client = jgui::WindowFrame<jgui::DefaultFrameMetrics>::client_rect(window_size);
    return jgui::Rect{client.x(), bar.bottom(), client.width(), client.bottom() - bar.bottom()};
}

void test_toggle(Checker& check) {
    auto app = jgui::make_application(jgui::HeadlessBackend{window_size}, demo::make_ui());
    check(app.has_value() && app->process_pending().has_value(), "first frame");
    const jgui::Rect below = area_below_bar(*app);
    const jgui::Point help = centre(app->ui().get<demo::HelpButton>().bounds());

    check(count_other_than(app->backend(), below, jgui::theme::background) == 0, "the client area starts empty");
    click(*app, help);
    check(app->ui().get<demo::HelpLine>().shown(), "the line exists");
    check(count_other_than(app->backend(), below, jgui::theme::background) > 0, "the first click shows it in pixels");
    click(*app, help);
    check(!app->ui().get<demo::HelpLine>().shown(), "the line is gone");
    check(count_other_than(app->backend(), below, jgui::theme::background) == 0, "the second click hides it in pixels");
    click(*app, help);
    check(count_other_than(app->backend(), below, jgui::theme::background) > 0, "the third click shows it again");
}

// The tree as the design shows it, inside a frame: the callback drives a Dynamic node directly.
struct DirectButton;
struct DirectLine;

void test_direct_form(Checker& check) {
    auto ui = jgui::window(jgui::window_frame(
        jgui::menu_bar(jgui::button<DirectButton>("Help", [](auto& u) -> void {
            auto& line = u.template get<DirectLine>();
            if (line.shown()) line.hide(); else line.show("Some help");
        })),
        jgui::dynamic<DirectLine, jgui::Label<DirectLine>>()));
    auto app = jgui::make_application(jgui::HeadlessBackend{window_size}, std::move(ui));
    check(app.has_value() && app->process_pending().has_value(), "first frame");
    const jgui::Point help = centre(app->ui().get<DirectButton>().bounds());
    click(*app, help);
    check(app->ui().get<DirectLine>().shown(), "the first click creates the item");
    click(*app, help);
    check(!app->ui().get<DirectLine>().shown(), "the second click destroys it");
}

void test_two_applications(Checker& check) {
    auto one = jgui::make_application(jgui::HeadlessBackend{window_size}, demo::make_ui());
    auto two = jgui::make_application(jgui::HeadlessBackend{window_size}, demo::make_ui());
    check(one.has_value() && two.has_value(), "both start");
    check(one->process_pending().has_value() && two->process_pending().has_value(), "both draw");
    click(*one, centre(one->ui().get<demo::HelpButton>().bounds()));
    check(one->ui().get<demo::HelpLine>().shown(), "the first shows its line");
    check(!two->ui().get<demo::HelpLine>().shown(), "the second is not affected");
}

}  // namespace

int main() {
    Checker check;
    test_toggle(check);
    test_direct_form(check);
    test_two_applications(check);
    return check.finish();
}
