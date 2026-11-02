// File: demo/help_ui.hpp
// Purpose: The demo's window: a menu bar with a Help button, and a line of text that the button
//          toggles by sending a message. The tests include this too, so they check the real demo.
#pragma once

#include <deque>
#include <expected>
#include <string_view>

#include "jCppGUI/v001/jgui.hpp"

namespace demo {

struct HelpButton;
struct HelpLine;

using ToggleHelp = jgui::Message<"toggle_help", jgui::To<HelpLine>>;

inline constexpr std::string_view help_text = "jCppGUI prototype. Click Help again to hide this line.";

// Receives ToggleHelp, and holds the line in a Dynamic node: showing creates it, hiding destroys it.
// Slots stays at its default in the real demo. The compile-time tests pass std::vector.
template <class Tag, template <class...> class Slots = std::deque>
class HelpPanel {
public:
    using tag = Tag;

    constexpr bool on(ToggleHelp) {
        if (line_.shown()) {
            line_.hide();
        } else {
            line_.show(help_text);
        }
        return line_.shown();
    }

    [[nodiscard]] constexpr bool shown() const noexcept { return line_.shown(); }
    [[nodiscard]] constexpr jgui::Rect bounds() const noexcept { return line_.bounds(); }
    constexpr void paint(jgui::Canvas& canvas) { line_.paint(canvas); }
    template <jgui::TextShaper Shaper>
    constexpr std::expected<jgui::Rect, jgui::Error> layout(jgui::Rect area, const Shaper& text) {
        return line_.layout(area, text);
    }

    template <class U, class E>
    constexpr bool handle(U& ui, const E& event) {
        return line_.handle(ui, event);
    }

    template <class U, class E>
    constexpr void notify(U& ui, const E& event) {
        line_.notify(ui, event);
    }

private:
    jgui::Dynamic<Tag, jgui::Label<Tag>, Slots> line_;
};

template <template <class...> class Slots = std::deque>
[[nodiscard]] constexpr auto make_ui() {
    return jgui::window(jgui::window_frame(
        jgui::menu_bar(jgui::button<HelpButton>("Help", [](auto& u) -> void { u.template send<ToggleHelp>(); })),
        HelpPanel<HelpLine, Slots>{}));
}

}  // namespace demo
