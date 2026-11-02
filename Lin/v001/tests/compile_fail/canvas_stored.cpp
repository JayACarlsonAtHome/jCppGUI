// File: tests/compile_fail/canvas_stored.cpp
// Purpose: A widget that stores the Canvas it was given would hold a view of pixels that the next frame moves.
//          This was a real heap use after free, and it must not compile.
#include <optional>

#include "common.hpp"

struct Keeper {
    using tag = Keeper;
#ifdef JGUI_BREAK
    std::optional<jgui::Canvas> kept{};
#endif
    [[nodiscard]] constexpr jgui::Rect bounds() const noexcept { return jgui::Rect{}; }
    void paint(const jgui::TreeKey&, jgui::Canvas& canvas) {
#ifdef JGUI_BREAK
        kept = canvas;
#else
        canvas.fill_rect(jgui::Rect{}, jgui::Color{});
#endif
    }
    template <class Shaper>
    constexpr std::expected<jgui::Rect, jgui::Error> layout(const jgui::TreeKey&, jgui::Rect area, const Shaper&) {
        return area;
    }
    template <class U, class E>
    constexpr bool handle(const jgui::TreeKey&, U&, const E&) {
        return false;
    }
    template <class U, class E>
    constexpr void notify(const jgui::TreeKey&, U&, const E&) {}
};

int main() {
    auto ui = jgui::window(Keeper{});
    static_cast<void>(ui);
}
