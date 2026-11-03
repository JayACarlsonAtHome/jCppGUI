// File: tests/compile_fail/caption_too_long_at_compile_time.cpp
// Purpose: The same for the length limit. The text is built at compile time, and is one byte over or exactly at it.
#include <array>
#include <cstddef>
#include <string_view>

#include "common.hpp"

template <std::size_t N>
struct Filled {
    std::array<char, N> text{};
    consteval Filled() { text.fill('x'); }
};

int main() {
#ifdef JGUI_BREAK
    constexpr std::size_t length = jgui::max_caption_bytes + 1;
#else
    constexpr std::size_t length = jgui::max_caption_bytes;
#endif
    static constexpr Filled<length> filled{};
    auto ui = jgui::label<First>(std::string_view{filled.text.data(), length});
    static_cast<void>(ui);
}
