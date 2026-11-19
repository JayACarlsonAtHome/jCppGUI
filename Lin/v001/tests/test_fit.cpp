// File: tests/test_fit.cpp
// Purpose: A window too narrow for its row of buttons is made to fit, as include/jCppGUI/v001/fit.hpp says, with the real
//          font: it grows where the compositor gives it room, or else its padding goes tight, or else its text gets
//          smaller, or else it says the screen is too small. Each is tried only when what came before did not fit.
#include <cstdint>
#include <optional>

#include "check.hpp"
#include "jCppGUI/v001/headless_backend.hpp"
#include "jCppGUI/v001/jgui.hpp"
#include "test_access.hpp"

namespace {

using jgui_test::Checker;
using jgui_test::number;

struct First;
struct Second;
struct Third;
struct Fourth;

struct DoNothing {
    template <class U>
    constexpr void operator()(U&) const {}
};

constexpr std::int32_t window_height = 200;

auto make_app(std::int32_t width) {
    return jgui::make_application(jgui::HeadlessBackend{jgui::Size{jgui::Width{width}, jgui::Height{window_height}}},
                                  jgui::window(jgui::menu_bar(jgui::button<First>("First of four", DoNothing{}), jgui::button<Second>("Second of four", DoNothing{}),
                                                              jgui::button<Third>("Third of four", DoNothing{}), jgui::button<Fourth>("Fourth of four", DoNothing{}))));
}
using App = decltype(make_app(0))::value_type;

// What the row's buttons wanted across in the last frame, with the bar's padding at each end.
std::int32_t row_wanted(App& app, jgui::FitPadding padding) {
    auto& ui = app.ui();
    return number(ui.get<First>().wanted_width()) + number(ui.get<Second>().wanted_width()) + number(ui.get<Third>().wanted_width()) +
           number(ui.get<Fourth>().wanted_width()) + 2 * number(padding.bar_x);
}

// The four captions' own widths in the last frame: what is left when each button's padding is taken off.
std::int32_t captions_across(App& app, jgui::FitPadding padding) { return row_wanted(app, padding) - 2 * number(padding.bar_x) - 8 * number(padding.button_x); }

struct Measured {
    std::int32_t normal{};       // the whole row with normal padding, at the full size
    std::int32_t tight{};        // the same with tight padding
    std::int32_t captions{};     // the captions alone, at the full size
};

Measured measure(Checker& check) {
    auto app = make_app(2000);
    check(app.has_value() && app->process_pending().has_value(), "a wide window draws");
    if (!app) return {};
    check(!app->too_narrow_by().has_value(), "and everything fits in it");
    const std::int32_t normal = row_wanted(*app, jgui::normal_padding);
    const std::int32_t captions = captions_across(*app, jgui::normal_padding);
    const std::int32_t tight = captions + 8 * number(jgui::tight_padding.button_x) + 2 * number(jgui::tight_padding.bar_x);
    check(captions > 0 && tight < normal, "the row is measured");
    return Measured{normal, tight, captions};
}

void test_fits(Checker& check, const Measured& row) {
    auto app = make_app(row.normal);
    check(app.has_value() && app->process_pending().has_value(), "exactly wide enough draws");
    if (!app) return;
    check(row_wanted(*app, jgui::normal_padding) == row.normal && !app->too_narrow_by(), "with normal padding, as it is");
    check(!jgui::detail::TestAccess::backend(*app).grown_to(), "and the window is not grown");
}

void test_grows_where_there_is_room(Checker& check, const Measured& row) {
    auto app = make_app(row.normal - 10);
    if (!app) return;
    jgui::detail::TestAccess::backend(*app).set_room(jgui::Size{jgui::Width{row.normal + 100}, jgui::Height{window_height + 100}});
    check(app->process_pending().has_value(), "a window short of room, with room to grow, draws");
    const auto grown = jgui::detail::TestAccess::backend(*app).grown_to();
    check(grown == jgui::Size{jgui::Width{row.normal}, jgui::Height{window_height}}, "it grows by just what the row is short");
    check(app->size() == jgui::Size{jgui::Width{row.normal}, jgui::Height{window_height}}, "the window is that size");
    check(row_wanted(*app, jgui::normal_padding) == row.normal && !app->too_narrow_by(), "and keeps normal padding");
}

void test_tight_padding_when_there_is_no_room(Checker& check, const Measured& row) {
    for (const std::optional<jgui::Size> room : {std::optional<jgui::Size>{}, std::optional{jgui::Size{jgui::Width{row.normal - 5}, jgui::Height{window_height}}}}) {
        auto app = make_app(row.normal - 10);
        if (!app) return;
        jgui::detail::TestAccess::backend(*app).set_room(room);
        check(app->process_pending().has_value(), "a window short of room draws");
        check(!jgui::detail::TestAccess::backend(*app).grown_to(), room ? "too little room: it does not grow" : "no room said: it does not grow");
        check(app->size().width() == jgui::Width{row.normal - 10}, "and stays its size");
        check(row_wanted(*app, jgui::tight_padding) == row.tight && !app->too_narrow_by(), "the padding goes tight, at the full text size, and it fits");
    }
}

void test_smaller_text(Checker& check, const Measured& row) {
    auto app = make_app(row.tight - 10);
    check(app.has_value() && app->process_pending().has_value(), "too narrow even with tight padding draws");
    if (!app) return;
    check(!app->too_narrow_by(), "it fits");
    check(captions_across(*app, jgui::tight_padding) < row.captions, "with smaller text");
    check(row_wanted(*app, jgui::tight_padding) <= row.tight - 10, "and tight padding");
}

void test_too_small(Checker& check, const Measured& row) {
    auto app = make_app(row.tight / 3);
    check(app.has_value() && app->process_pending().has_value(), "far too narrow still draws");
    if (!app) return;
    const auto short_by = app->too_narrow_by();
    check(short_by.has_value() && *short_by > jgui::Width{}, "and says the screen is too small, and by how much");
    check(captions_across(*app, jgui::tight_padding) < row.captions, "at the smallest text");
    // The notice's edge is drawn in the focus colour over the top of the window, and nothing else in it uses that colour.
    const auto& backend = jgui::detail::TestAccess::backend(*app);
    bool edge_seen = false;
    for (std::int32_t x = 0; x < row.tight / 3 && !edge_seen; ++x)
        edge_seen = backend.pixel(jgui::Point{jgui::Left{x}, jgui::Top{jgui_test::number(jgui::theme::label_pad_y)}}) == jgui::theme::focus_edge.argb;
    check(edge_seen, "in a notice over the window");

    jgui::detail::TestAccess::backend(*app).inject(jgui::ResizeEvent{jgui::Size{jgui::Width{row.normal}, jgui::Height{window_height}}});
    check(app->process_pending().has_value() && !app->too_narrow_by(), "made wide enough, the notice goes");
    check(row_wanted(*app, jgui::normal_padding) == row.normal, "and the padding and text are back to normal");
}

}  // namespace

int main() {
    Checker check;
    const Measured row = measure(check);
    test_fits(check, row);
    test_grows_where_there_is_room(check, row);
    test_tight_padding_when_there_is_no_room(check, row);
    test_smaller_text(check, row);
    test_too_small(check, row);
    return check.finish();
}
