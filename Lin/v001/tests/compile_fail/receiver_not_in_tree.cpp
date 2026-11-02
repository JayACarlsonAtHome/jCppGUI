// File: tests/compile_fail/receiver_not_in_tree.cpp
// Purpose: A message sent to a receiver that is not in the tree must not compile.
#include "common.hpp"

using Ping = jgui::Message<"ping", jgui::To<First>>;
using Lost = jgui::Message<"lost", jgui::To<First, Second>>;

template <class Tag>
struct Answers {
    using tag = Tag;
    Plain<Tag> plain;
    [[nodiscard]] constexpr jgui::Rect bounds() const noexcept { return plain.bounds(); }
    constexpr void paint(jgui::Canvas& canvas) { plain.paint(canvas); }
    template <class Shaper>
    constexpr std::expected<jgui::Rect, jgui::Error> layout(jgui::Rect area, const Shaper& s) { return plain.layout(area, s); }
    template <class U, class E>
    constexpr bool handle(U& u, const E& e) { return plain.handle(u, e); }
    constexpr int on(Ping) { return 1; }
    constexpr int on(Lost) { return 2; }
};

int main() {
    auto ui = jgui::window(Answers<First>{});
#ifdef JGUI_BREAK
    static_cast<void>(ui.send<Lost>());
#else
    static_cast<void>(ui.send<Ping>());
#endif
}
