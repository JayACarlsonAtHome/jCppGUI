// File: tests/test_bad_text.cpp
// Purpose: Text that cannot be drawn must not end the program. At run time, with the real font where a real font can
//          be made to refuse, and with a shaper that refuses where it cannot: invalid UTF-8 and over-long text are refused
//          when they are set, a refusal while shaping stays with its widget, and the window goes on working in all three.
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "app_helpers.hpp"
#include "check.hpp"

namespace {

using namespace jgui;
using jgui_test::Checker;

struct Shown;
struct Other;
struct Other2;

constexpr Size window_size{Width{320}, Height{120}};

[[nodiscard]] int ink_in(const HeadlessBackend& backend, const Rect& area) {
    return jgui_test::count_other_than(backend, area, theme::background);
}

// A font that cannot do one text, the way a font's rules can expand one past the glyph limit, and an engine that fails on another.
struct PickyShaper {
    [[nodiscard]] std::expected<TextRun, Error> shape(std::string_view text) const {
        if (text == "expands") return make_error(ErrorCode::GlyphTooLarge, "the shaped line has too many glyphs");
        if (text == "breaks") return make_error(ErrorCode::Font, "the font engine failed");
        const Size size{Width{20}, Height{10}};
        return TextRun{size, std::vector<std::uint8_t>(200, 255)};
    }
    [[nodiscard]] Height line_height() const noexcept { return Height{10}; }
};

void test_refused_when_set(Checker& check) {
    auto app = make_application(HeadlessBackend{window_size}, window(label<Shown>("Before")));
    check(app.has_value() && app->process_pending().has_value() && app->frames() == 1, "first frame, with the real font");
    if (!app) return;
    auto& line = app->ui().get<Shown>();
    const Rect row = line.bounds();
    const int ink = ink_in(jgui_test::backend(*app), row);
    check(ink > 0, "its text is drawn");

    const std::string invalid = "He\xC3";
    const auto bad_utf8 = line.set_text(invalid);
    check(!bad_utf8.has_value() && bad_utf8.error().code == ErrorCode::InvalidUtf8, "invalid UTF-8 is refused when it is set");
    const auto too_long = line.set_text(std::string(max_caption_bytes + 1, 'x'));
    check(!too_long.has_value() && too_long.error().code == ErrorCode::TextTooLong, "so is text over the limit");
    check(line.text() == "Before", "the old text stays");
    check(app->process_pending().has_value() && app->frames() == 1, "nothing to redraw, and the program goes on");

    check(line.set_text("After").has_value() && app->process_pending().has_value() && app->frames() == 2, "good text after a refusal is drawn");
    check(line.text() == "After" && !line.text_error(), "and it is the text now");
    const std::string at_limit(max_caption_bytes, 'x');
    check(line.set_text(at_limit).has_value() && app->process_pending().has_value() && app->frames() == 3, "text exactly at the limit is taken");
}

void test_checked_at_run_time(Checker& check) {
    const std::string bad = "\xED\xA0\x80";
    const auto refused = CheckedText::check(bad);
    check(!refused.has_value() && refused.error().code == ErrorCode::InvalidUtf8, "text from outside is checked before a widget is made");
    const std::string good = "From outside";
    const auto checked = CheckedText::check(good);
    check(checked.has_value(), "good text passes");
    if (!checked) return;
    Dynamic<Shown, Label<Shown>> node;
    check(node.create(*checked).has_value() && node.shown(), "and a widget is made from it");
}

void test_shaping_refusal(Checker& check) {
    ApplicationCore core{HeadlessBackend{window_size}, window(label<Shown>("expands"), label<Other>("next"), button<Other2>("expands", [](auto&) -> void {}))};
    const PickyShaper shaper;
    check(core.process_pending(shaper).has_value() && core.frames() == 1, "a refusal while shaping does not end the frame");
    auto& line = core.ui().get<Shown>();
    check(line.text_error() && line.text_error()->code == ErrorCode::GlyphTooLarge, "the refusal can be read from the widget");
    check(ink_in(jgui_test::backend(core), line.bounds()) == 0, "that widget draws no text");
    check(ink_in(jgui_test::backend(core), core.ui().get<Other>().bounds()) > 0, "and the one beside it draws its own");
    check(core.ui().get<Other2>().text_error().has_value() && ink_in(jgui_test::backend(core), core.ui().get<Other2>().bounds()) > 0, "a button with no caption still draws itself");

    // The loop itself goes on, through new damage, to an ordinary close.
    check(line.set_text("good").has_value() && core.process_pending(shaper).has_value() && core.frames() == 2, "and it redraws");
    check(!line.text_error() && ink_in(jgui_test::backend(core), line.bounds()) > 0, "with the new text");
    jgui_test::backend(core).inject(CloseEvent{});
    check(core.run(shaper).has_value() && core.closed(), "run ends only when the window closes");
}

void test_other_failures_still_end_the_run(Checker& check) {
    ApplicationCore core{HeadlessBackend{window_size}, window(label<Shown>("breaks"))};
    const auto result = core.run(PickyShaper{});
    check(!result.has_value() && result.error().code == ErrorCode::Font, "the font engine failing still ends run");
}

}  // namespace

int main() {
    Checker check;
    test_refused_when_set(check);
    test_checked_at_run_time(check);
    test_shaping_refusal(check);
    test_other_failures_still_end_the_run(check);
    return check.finish();
}
