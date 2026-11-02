// File: tests/compile_fail/pixel_buffer_copied.cpp
// Purpose: A pixel buffer is a view of pixels that move, so a copy must not compile.
#include <array>
#include <cstdint>

#include "common.hpp"

int main() {
    std::array<std::uint32_t, 1> storage{};
    const auto made = jgui::PixelBuffer::create(storage, jgui::Size{jgui::Width{1}, jgui::Height{1}}, 4);
    if (!made) return 1;
#ifdef JGUI_BREAK
    const jgui::PixelBuffer kept = *made;
#else
    const jgui::PixelBuffer& kept = *made;
#endif
    static_cast<void>(kept);
}
