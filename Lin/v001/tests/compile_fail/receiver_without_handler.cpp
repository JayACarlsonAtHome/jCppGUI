// File: tests/compile_fail/receiver_without_handler.cpp
// Purpose: A message sent to a widget with no handler for it must not compile.
#include "common.hpp"

using Handled = jgui::Message<"handled", jgui::To<First>>;
using Unhandled = jgui::Message<"unhandled", jgui::To<First>>;

struct Answers {
    using tag = First;
    Plain<First> plain;
    [[nodiscard]] constexpr jgui::Rect bounds() const noexcept { return plain.bounds(); }
    constexpr void paint(jgui::Canvas& canvas) { plain.paint(canvas); }
    template <class Shaper>
    constexpr std::expected<jgui::Rect, jgui::Error> layout(jgui::Rect area, const Shaper& s) { return plain.layout(area, s); }
    template <class U, class E>
    constexpr bool handle(U& u, const E& e) { return plain.handle(u, e); }
    constexpr int on(Handled) { return 1; }
};

int main() {
    auto ui = jgui::window(Answers{});
#ifdef JGUI_BREAK
    static_cast<void>(ui.send<Unhandled>());
#else
    static_cast<void>(ui.send<Handled>());
#endif
}
