// File: tests/fuzz/fuzz_font.cpp
// Purpose: Fuzzes font bytes through the real engine: TextEngine::from_font_bytes runs every check on the font,
//          and shape runs HarfBuzz, FreeType's rendering and our copying of every glyph.
#include <cstddef>
#include <cstdint>

#include "c_resource.hpp"
#include "jCppGUI/v001/text.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    constexpr std::size_t limit = std::size_t{16} << 20U;
    const auto bytes = jgui::detail::copy_from_c(data, size, limit);
    if (!bytes) return 0;
    const auto engine = jgui::TextEngine::from_font_bytes(*bytes);
    if (!engine) return 0;
    // Plain letters, ligatures for substitution rules, and marks and other scripts for shaping paths.
    for (const char* text : {"Help", "office ffi fl", "e\xCC\x81 \xC3\xA9 \xD8\xB9\xD8\xB1\xD8\xA8", "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7"}) {
        const auto run = engine->shape(text);
        if (run && !run->size().empty()) static_cast<void>(run->coverage_at(jgui::Point{jgui::Left{} + run->size().width() - jgui::Width{1}, jgui::Top{} + run->size().height() - jgui::Height{1}}));
    }
    return 0;
}
