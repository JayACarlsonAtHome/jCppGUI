// File: tests/compile_fail/missing_value.cpp
// Purpose: A message sent with a value missing must not compile.
#include <string>

#include "common.hpp"

using SetText = jgui::Message<"set_text", jgui::To<First>, std::string>;

struct Answers {
    using tag = First;
    Plain<First> plain;
    [[nodiscard]] constexpr jgui::Rect bounds() const noexcept { return plain.bounds(); }
    constexpr void paint(const jgui::TreeKey& key, jgui::Canvas& canvas) { plain.paint(key, canvas); }
    template <class Shaper>
    constexpr std::expected<jgui::Rect, jgui::Error> layout(const jgui::TreeKey& key, jgui::Rect area, const Shaper& s) { return plain.layout(key, area, s); }
    template <class U, class E>
    constexpr bool handle(const jgui::TreeKey& key, U& u, const E& e) { return plain.handle(key, u, e); }
    template <class U, class E>
    constexpr void notify(const jgui::TreeKey& key, U& u, const E& e) { plain.notify(key, u, e); }
    int on(SetText, const std::string& text) { return static_cast<int>(text.size()); }
};

int main() {
    auto ui = jgui::window(Answers{});
#ifdef JGUI_BREAK
    static_cast<void>(ui.send<SetText>());
#else
    static_cast<void>(ui.send<SetText>(std::string{"text"}));
#endif
}
