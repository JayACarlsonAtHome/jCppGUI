// File: tests/test_text.cpp
// Purpose: Tests 15 and 16 with the real font: "Help" draws inside its measured bounds and nowhere
//          outside the clip, and invalid UTF-8 and over-long captions return an Error and draw nothing.
#include <cstdint>
#include <string>
#include <vector>

#include "check.hpp"
#include "jCppGUI/v001/canvas.hpp"

namespace {

using namespace jgui;

constexpr std::uint32_t background = 0xFFFFFFFFU;
constexpr Color ink{0xFF000000U};
constexpr Size buffer_size{200, 60};

struct Target {
    std::vector<std::uint32_t> storage = std::vector<std::uint32_t>(200U * 60U, background);

    [[nodiscard]] PixelBuffer buffer() { return PixelBuffer::create(storage, buffer_size, 200 * 4).value(); }
    [[nodiscard]] int count_inked(const Rect& area, bool inside) const {
        int count = 0;
        for (std::int32_t y = 0; y < buffer_size.height(); ++y) {
            for (std::int32_t x = 0; x < buffer_size.width(); ++x) {
                const auto index = static_cast<std::size_t>(y) * 200U + static_cast<std::size_t>(x);
                if (area.contains(Point{x, y}) == inside && storage[index] != background) ++count;
            }
        }
        return count;
    }
};

void test_help_text(jgui_test::Checker& check, const TextEngine& engine) {
    const auto run = engine.shape("Help");
    check(run.has_value(), "\"Help\" shapes");
    if (!run) return;
    check(run->size().width() > 0 && run->size().height() == engine.line_height(), "it has a measured size");

    Target whole;
    const Point at{10, 10};
    Canvas{whole.buffer()}.draw_text(at, *run, ink);
    const Rect measured{at, run->size()};
    check(whole.count_inked(measured, true) > 0, "non-background pixels inside its measured bounds");
    check(whole.count_inked(measured, false) == 0, "none outside its measured bounds");

    Target clipped;
    const Rect clip{12, 12, 6, 6};
    Canvas{clipped.buffer()}.clipped(clip).draw_text(at, *run, ink);
    check(clipped.count_inked(clip, false) == 0, "none outside the clip");
    check(clipped.count_inked(clip, true) > 0, "some inside the clip");

    Target off;
    Canvas{off.buffer()}.draw_text(Point{-1000, -1000}, *run, ink);
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
    check(empty.has_value() && empty->size().width() == 0, "an empty caption is valid and has no width");
    check(engine.shape("caf\xC3\xA9 \xE2\x82\xAC").has_value(), "valid multi-byte text is accepted");
}

}  // namespace

int main() {
    jgui_test::Checker check;
    auto engine = TextEngine::create();
    check(engine.has_value(), "a default sans-serif font was found");
    if (engine) {
        test_help_text(check, *engine);
        test_refusals(check, *engine);
    }
    return check.finish();
}
