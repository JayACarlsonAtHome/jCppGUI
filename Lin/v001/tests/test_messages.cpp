// File: tests/test_messages.cpp
// Purpose: Test 16a at run time: no values, a string, and a string plus a vector reach every listed
//          receiver, responses arrive in receiver order, and void handlers give NoReply.
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "check.hpp"
#include "jCppGUI/v001/jgui.hpp"

// Outside the anonymous namespace because only the TextShaper concept reads it: these widgets shape no
// text, and a compiler rightly reports a member nobody calls on a type with internal linkage.
struct NoText {
    [[nodiscard]] std::expected<jgui::TextRun, jgui::Error> shape(std::string_view) const { return jgui::TextRun{}; }
    [[nodiscard]] std::int32_t line_height() const noexcept { return 0; }
};

namespace {

struct HelpLine;
struct StatusBar;
struct Quiet;

using ToggleHelp = jgui::Message<"toggle_help", jgui::To<HelpLine, StatusBar>>;
using SetHelp = jgui::Message<"set_help", jgui::To<HelpLine>, std::string>;
using Rows = jgui::Message<"rows", jgui::To<HelpLine, StatusBar>, std::string, std::vector<int>>;
using Hush = jgui::Message<"hush", jgui::To<Quiet, HelpLine, Quiet>>;
// Same receivers and values as ToggleHelp, but a distinct type because of its name.
using OtherToggle = jgui::Message<"other_toggle", jgui::To<HelpLine, StatusBar>>;

// The event and layout members every widget needs, none of them doing anything here.
template <class Tag>
struct Passive {
    using tag = Tag;
    [[nodiscard]] jgui::Rect bounds() const { return jgui::Rect{}; }
    void paint(jgui::Canvas&) {}
    template <class Shaper>
    std::expected<jgui::Rect, jgui::Error> layout(jgui::Rect area, const Shaper&) { return area; }
    template <class U, class E>
    bool handle(U&, const E&) { return false; }
    template <class U, class E>
    void notify(U&, const E&) {}
};

struct HelpLineWidget {
    using tag = HelpLine;
    Passive<HelpLine> passive;
    bool shown = false;
    std::string text;
    int order = 0;
    [[nodiscard]] jgui::Rect bounds() const { return passive.bounds(); }
    void paint(jgui::Canvas& canvas) { passive.paint(canvas); }
    template <class Shaper>
    std::expected<jgui::Rect, jgui::Error> layout(jgui::Rect area, const Shaper& s) { return passive.layout(area, s); }
    template <class U, class E>
    bool handle(U& u, const E& e) { return passive.handle(u, e); }
    template <class U, class E>
    void notify(U& u, const E& e) { passive.notify(u, e); }

    bool on(ToggleHelp) { return shown = !shown; }
    std::string on(OtherToggle) { return "other"; }
    int on(SetHelp, const std::string& value) {
        text = value;
        return static_cast<int>(text.size());
    }
    std::size_t on(Rows, const std::string& name, const std::vector<int>& values) { return name.size() + values.size(); }
    void on(Hush) { ++order; }
};

struct StatusBarWidget {
    using tag = StatusBar;
    Passive<StatusBar> passive;
    int count = 0;
    [[nodiscard]] jgui::Rect bounds() const { return passive.bounds(); }
    void paint(jgui::Canvas& canvas) { passive.paint(canvas); }
    template <class Shaper>
    std::expected<jgui::Rect, jgui::Error> layout(jgui::Rect area, const Shaper& s) { return passive.layout(area, s); }
    template <class U, class E>
    bool handle(U& u, const E& e) { return passive.handle(u, e); }
    template <class U, class E>
    void notify(U& u, const E& e) { passive.notify(u, e); }

    int on(ToggleHelp) { return ++count; }
    int on(OtherToggle) { return count += 100; }
    std::string on(Rows, const std::string& name, const std::vector<int>&) { return name + "!"; }
};

struct QuietWidget {
    using tag = Quiet;
    Passive<Quiet> passive;
    int calls = 0;
    [[nodiscard]] jgui::Rect bounds() const { return passive.bounds(); }
    void paint(jgui::Canvas& canvas) { passive.paint(canvas); }
    template <class Shaper>
    std::expected<jgui::Rect, jgui::Error> layout(jgui::Rect area, const Shaper& s) { return passive.layout(area, s); }
    template <class U, class E>
    bool handle(U& u, const E& e) { return passive.handle(u, e); }
    template <class U, class E>
    void notify(U& u, const E& e) { passive.notify(u, e); }

    void on(Hush) { ++calls; }
};

// Receivers are whole widgets, which the tree lays out and paints like any other.
void test_receivers_are_widgets(jgui_test::Checker& check) {
    auto ui = jgui::window(HelpLineWidget{}, StatusBarWidget{}, QuietWidget{});
    const jgui::Rect area{0, 0, 2, 2};
    const auto left = ui.layout(area, NoText{});
    check(left.has_value() && *left == area, "layout passes through them");
    std::array<std::uint32_t, 4> pixels{};
    auto buffer = jgui::PixelBuffer::create(pixels, area.size(), 8);
    check(buffer.has_value(), "a small buffer");
    if (buffer) {
        jgui::Canvas canvas{*buffer};
        ui.paint(canvas);
    }
    check(pixels == std::array<std::uint32_t, 4>{}, "they paint nothing");
    check(!ui.capture_active() && !ui.damaged(), "they hold nothing and ask for nothing");
    check(ui.get<HelpLine>().bounds().empty() && ui.get<StatusBar>().bounds().empty() && ui.get<Quiet>().bounds().empty(), "and have no bounds");
}

}  // namespace

int main() {
    jgui_test::Checker check;
    test_receivers_are_widgets(check);
    auto ui = jgui::window(HelpLineWidget{}, StatusBarWidget{}, QuietWidget{});

    auto [shown, count] = ui.send<ToggleHelp>();
    static_assert(std::same_as<decltype(shown), bool> && std::same_as<decltype(count), int>);
    check(shown && count == 1, "no values: every receiver answered");

    auto [length] = ui.send<SetHelp>(std::string{"Help text"});
    check(length == 9 && ui.get<HelpLine>().text == "Help text", "a string reached its receiver");

    auto [total, echoed] = ui.send<Rows>(std::string{"rows"}, std::vector<int>{1, 2, 3});
    static_assert(std::same_as<decltype(total), std::size_t> && std::same_as<decltype(echoed), std::string>);
    check(total == 7 && echoed == "rows!", "a string and a vector: responses in receiver order");

    auto [other_text, other_count] = ui.send<OtherToggle>();
    check(other_text == "other" && other_count == 101, "the name keeps two like messages apart");
    check(ui.get<HelpLine>().shown, "the other message did not reach the toggle handler");

    const auto replies = ui.send<Hush>();
    static_assert(std::same_as<std::remove_const_t<decltype(replies)>, std::tuple<jgui::NoReply, jgui::NoReply, jgui::NoReply>>);
    check(ui.get<Quiet>().calls == 2, "a void handler ran once for each time it is listed");
    check(ui.get<HelpLine>().order == 1, "and the receiver between them ran too");
    return check.finish();
}
