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
    static constexpr bool holds_items = true;

    constexpr bool on(ToggleHelp) {
        if (line_.shown()) {
            line_.hide();
        } else {
            line_.show(jgui::CheckedText{help_text});
        }
        return line_.shown();
    }

    [[nodiscard]] constexpr bool shown() const noexcept { return line_.shown(); }
    [[nodiscard]] constexpr jgui::Rect bounds() const noexcept { return line_.bounds(); }
    template <class Visit>
    constexpr void visit_items(const jgui::TreeKey& key, Visit&& visit) {
        line_.visit_items(key, visit);
    }
    constexpr void paint(const jgui::TreeKey& key, jgui::Canvas& canvas) { line_.paint(key, canvas); }
    template <jgui::TextShaper Shaper>
    constexpr std::expected<jgui::Rect, jgui::Error> layout(const jgui::TreeKey& key, jgui::Rect area, const Shaper& text) {
        return line_.layout(key, area, text);
    }

    template <class U, class E>
    constexpr bool handle(const jgui::TreeKey& key, U& ui, const E& event) {
        return line_.handle(key, ui, event);
    }

    template <class U, class E>
    constexpr void notify(const jgui::TreeKey& key, U& ui, const E& event) {
        line_.notify(key, ui, event);
    }

private:
    jgui::Dynamic<Tag, jgui::Label<Tag>, Slots> line_{};
};

// What Help does. A generic lambda works in a real window. This is a named type because the compile-time
// tests run this tree on a vector-backed pool, where the call becomes immediate, and clang 22 refuses that for
// a generic lambda with a stated return type ("used before it is defined"). g++ accepts both.
struct ToggleOnClick {
    template <class U>
    constexpr void operator()(U& u) const {
        u.template send<ToggleHelp>();
    }
};

template <template <class...> class Slots = std::deque>
[[nodiscard]] constexpr auto make_ui() {
    return jgui::window(jgui::window_frame(
        jgui::menu_bar(jgui::button<HelpButton>("Help", ToggleOnClick{})),
        HelpPanel<HelpLine, Slots>{}));
}

}  // namespace demo
