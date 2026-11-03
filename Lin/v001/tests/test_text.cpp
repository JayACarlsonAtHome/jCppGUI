// File: tests/test_text.cpp
// Purpose: Tests 15 and 16 with the real font: "Help" draws inside its measured bounds and nowhere
//          outside the clip, and invalid UTF-8 and over-long captions return an Error and draw nothing.
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstdint>
#include <string>
#include <vector>

#include "check.hpp"
#include "jCppGUI/v001/canvas.hpp"
#include "test_access.hpp"

namespace {

using namespace jgui;

constexpr std::uint32_t background = 0xFFFFFFFFU;
constexpr Color ink{0xFF000000U};
constexpr Size buffer_size{Width{200}, Height{60}};

struct Target {
    std::vector<std::uint32_t> storage = std::vector<std::uint32_t>(200U * 60U, background);

    [[nodiscard]] Canvas canvas() { return Canvas{PixelBuffer::create(storage, buffer_size, 200 * 4).value()}; }
    [[nodiscard]] int count_inked(const Rect& area, bool inside) const {
        int count = 0;
        for (std::int32_t y = 0; y < jgui_test::number(buffer_size.height()); ++y) {
            for (std::int32_t x = 0; x < jgui_test::number(buffer_size.width()); ++x) {
                const auto index = static_cast<std::size_t>(y) * 200U + static_cast<std::size_t>(x);
                if (area.contains(Point{Left{x}, Top{y}}) == inside && storage[index] != background) ++count;
            }
        }
        return count;
    }
};

void test_help_text(jgui_test::Checker& check, const TextEngine& engine) {
    const auto run = engine.shape("Help");
    check(run.has_value(), "\"Help\" shapes");
    if (!run) return;
    check(run->size().width() > Width{} && run->size().height() == engine.line_height(), "it has a measured size");

    Target whole;
    const Point at{Left{10}, Top{10}};
    whole.canvas().draw_text(at, *run, ink);
    const Rect measured{at, run->size()};
    check(whole.count_inked(measured, true) > 0, "non-background pixels inside its measured bounds");
    check(whole.count_inked(measured, false) == 0, "none outside its measured bounds");

    Target clipped;
    const Rect clip{Point{Left{12}, Top{12}}, Size{Width{6}, Height{6}}};
    clipped.canvas().clipped(clip).draw_text(at, *run, ink);
    check(clipped.count_inked(clip, false) == 0, "none outside the clip");
    check(clipped.count_inked(clip, true) > 0, "some inside the clip");

    Target off;
    off.canvas().draw_text(Point{Left{-1000}, Top{-1000}}, *run, ink);
    check(off.count_inked(Rect{Point{}, buffer_size}, true) == 0, "off the buffer draws nothing");
}

void test_refusals(jgui_test::Checker& check, const TextEngine& engine) {
    for (const std::string_view bad : {std::string_view{"\xFF"}, std::string_view{"He\xC3"}, std::string_view{"\xC0\x80"},
                                       std::string_view{"\xED\xA0\x80"}, std::string_view{"\xF4\x90\x80\x80"}}) {
        const auto run = engine.shape(bad);
        check(!run.has_value() && run.error().code == ErrorCode::InvalidUtf8, "invalid UTF-8 is an Error");
    }
    const auto too_long = engine.shape(std::string(max_caption_bytes + 1, 'x'));
    check(!too_long.has_value() && too_long.error().code == ErrorCode::TextTooLong, "an over-long caption is an Error");
    check(engine.shape(std::string(max_caption_bytes, 'x')).has_value(), "exactly at the limit is accepted");
    const auto empty = engine.shape("");
    check(empty.has_value() && empty->size().width() == Width{}, "an empty caption is valid and has no width");
    check(engine.shape("caf\xC3\xA9 \xE2\x82\xAC").has_value(), "valid multi-byte text is accepted");
}

// A coverage vector that does not match the size must abort, in every build.
template <class Make>
void test_aborts(jgui_test::Checker& check, Make make, const char* what) {
    const pid_t child = ::fork();
    check(child >= 0, "fork");
    if (child == 0) {
        static_cast<void>(make());
        ::_exit(0);  // reached only if the check did not fire
    }
    int status = 0;
    check(::waitpid(child, &status, 0) == child, "waitpid");
    check(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT, what);
}

}  // namespace

int main() {
    jgui_test::Checker check;
    test_aborts(check, [] { return TextRun{Size{Width{2}, Height{2}}, std::vector<std::uint8_t>(3)}; }, "too little coverage aborts");
    test_aborts(check, [] { return TextRun{Size{Width{2}, Height{2}}, std::vector<std::uint8_t>(5)}; }, "too much coverage aborts");
    test_aborts(check, [] { return TextRun{Size{Width{46341}, Height{46341}}, std::vector<std::uint8_t>(1)}; }, "a size whose area would overflow 32 bits aborts");
    auto engine = TextEngine::create();
    check(engine.has_value(), "a default sans-serif font was found");
    const std::vector<std::uint8_t> not_a_font(1000, 0x5A);
    const auto refused_font = TextEngine::from_font_bytes(not_a_font);
    check(!refused_font.has_value() && refused_font.error().code == ErrorCode::Font, "bytes that are no font are refused");
    check(!TextEngine::from_font_bytes({}).has_value(), "and so is nothing at all");
    if (engine) {
        const auto mono = engine->monospace_face();
        check(mono.has_value(), "a monospace font was found on this system");
        if (mono) {
            const auto wide = mono->get().shape("iiii");
            const auto also = mono->get().shape("WWWW");
            check(wide && also && wide->size() == also->size(), "and its letters are all as wide as each other");
            const auto again = engine->monospace_face();
            check(again.has_value() && &again->get() == &mono->get(), "asked twice, it is the same one");
            const auto of_mono = mono->get().monospace_face();
            check(!of_mono.has_value() && of_mono.error().code == ErrorCode::Font, "a monospace face looks for no other");
        }
        test_help_text(check, *engine);
        test_refusals(check, *engine);
    }
    return check.finish();
}
