// File: tests/test_viewer.cpp
// Purpose: The viewer's own helpers, OneShown on its own, and the viewer's whole window, from a file's text to the state and the
//          pixels, in static_assert with a shaper that needs no font. The real program is tested in tests/test_fake_compositor.cpp.
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "jCppGUI/v001/headless_backend.hpp"
#include "monitor_helpers.hpp"
#include "test_access.hpp"
#include "viewer_helpers.hpp"
#include "viewer_ui.hpp"

namespace {

using namespace jgui;
using jgui_test::BlockShaper;
using jgui_test::number;

// --- Small helpers ---
static_assert(viewer::file_name_in("a/b.jtext") == "b.jtext" && viewer::file_name_in("b.jtext") == "b.jtext" && viewer::file_name_in("a/").empty() && viewer::file_name_in("").empty());
static_assert(viewer::checked_path("x.jtext").has_value() && !viewer::checked_path("").has_value());
static_assert(viewer::checked_path(std::string(4096, 'x')).has_value() && !viewer::checked_path(std::string(4097, 'x')).has_value());
static_assert(!viewer::checked_path("a\xFF").has_value() && !viewer::checked_path("a\nb").has_value());
static_assert(viewer::count_of(0, "row") == "0 rows" && viewer::count_of(1, "row") == "1 row" && viewer::count_of(2, "row") == "2 rows");
static_assert(viewer::section_status("Run", 1, 0) == "Run: 1 row");
static_assert(viewer::section_status("Run", 3, 2) == "Run: 3 rows, 2 rows left out: a value did not convert");

[[nodiscard]] constexpr bool titles_are_right() {
    using Path = std::expected<std::string, Error>;
    if (viewer::window_title_for(Path{"dir/some.jtext"}) != "jCppGUI viewer: some.jtext") return false;
    if (viewer::window_title_for(Path{"dir/"}) != "jCppGUI viewer") return false;
    if (viewer::window_title_for(Path{std::unexpected(Error{ErrorCode::System, "no"})}) != "jCppGUI viewer") return false;
    // A name past what any file has must not cost the window its title, which a backend would refuse at 4083 bytes.
    return viewer::window_title_for(Path{std::string(4096, 'x')}).size() == std::string_view{"jCppGUI viewer: "}.size() + viewer::most_title_name_bytes;
}
static_assert(titles_are_right());

static_assert(viewer::status_fitting_a_line(std::string_view{"short"}) == "short");

[[nodiscard]] constexpr bool a_long_status_is_cut_at_a_character() {
    std::string text(max_caption_bytes - 1, 'x');
    text += "\xE2\x96\xAE";
    const std::string_view cut = viewer::status_fitting_a_line(text);
    return cut.size() == max_caption_bytes - 1 && valid_utf8(cut);
}
static_assert(a_long_status_is_cut_at_a_character());

// --- OneShown on its own ---

// Counts what reaches it.
template <class Tag>
struct Counter {
    using tag = Tag;
    int laid_out{};
    int painted{};
    int presses{};
    int leaves{};
    int focus_cleared{};
    int damage_cleared{};
    Rect area{};
    bool wants_redraw{};
    [[nodiscard]] constexpr Rect bounds() const noexcept { return area; }
    constexpr void paint(const TreeKey&, Canvas&) { ++painted; }
    template <class Shaper>
    constexpr std::expected<Rect, Error> layout(const TreeKey&, Rect offered, const Shaper&) {
        ++laid_out;
        area = offered;
        return Rect{Point{offered.left(), top_after(offered.bottom())}, Size{offered.width(), Height{}}};
    }
    template <class U>
    constexpr bool handle(const TreeKey&, U&, const PointerPress& event) {
        if (!area.contains(event.position)) return false;
        ++presses;
        return true;
    }
    template <class U>
    constexpr bool handle(const TreeKey&, U&, const QueryDamage&) {
        return wants_redraw;
    }
    template <class U, class E>
    constexpr bool handle(const TreeKey&, U&, const E&) {
        return false;
    }
    template <class U>
    constexpr void notify(const TreeKey&, U&, const PointerLeave&) {
        ++leaves;
    }
    template <class U>
    constexpr void notify(const TreeKey&, U&, const ClearFocus&) {
        ++focus_cleared;
    }
    template <class U>
    constexpr void notify(const TreeKey&, U&, const ClearDamage&) {
        ++damage_cleared;
        wants_redraw = false;
    }
    template <class U, class E>
    constexpr void notify(const TreeKey&, U&, const E&) {}
};
struct FirstTag;
struct SecondTag;
struct ShownTag;
struct AfterTag;
using Pair = OneShown<ShownTag, Counter<FirstTag>, Counter<SecondTag>>;
static_assert(Widget<Pair>);

[[nodiscard]] constexpr auto pair_tree() { return window(one_shown<ShownTag>(Counter<FirstTag>{}, Counter<SecondTag>{}), Counter<AfterTag>{}); }
constexpr Rect pair_area{Point{Left{5}, Top{6}}, Size{Width{100}, Height{50}}};

[[nodiscard]] constexpr bool nothing_shown_passes_everything_on() {
    auto tree = pair_tree();
    const auto left = jgui_test::layout(tree, pair_area, BlockShaper{});
    auto& shown = tree.template get<ShownTag>();
    return left && shown.shown() == std::nullopt && tree.template get<FirstTag>().laid_out == 0 && tree.template get<SecondTag>().laid_out == 0 && shown.bounds() == pair_area && tree.template get<AfterTag>().area == pair_area;
}
static_assert(nothing_shown_passes_everything_on(), "and the next widget gets all of it");

[[nodiscard]] constexpr bool the_shown_one_alone_gets_it_all() {
    auto tree = pair_tree();
    tree.template get<ShownTag>().show(1);
    if (!jgui_test::layout(tree, pair_area, BlockShaper{})) return false;
    if (tree.template get<SecondTag>().area != pair_area || tree.template get<SecondTag>().laid_out != 1 || tree.template get<FirstTag>().laid_out != 0) return false;
    jgui_test::dispatch(tree, PointerPress{Point{Left{6}, Top{7}}, MouseButton::Left, jgui_test::serial(1)});
    if (tree.template get<SecondTag>().presses != 1 || tree.template get<FirstTag>().presses != 0) return false;
    std::vector<std::uint32_t> pixels(4, 0);
    const Size size{Width{2}, Height{2}};
    const auto buffer = PixelBuffer::create(pixels, size, PixelBuffer::tight_stride(size));
    Canvas canvas{*buffer};
    jgui_test::paint(tree, canvas);
    return tree.template get<SecondTag>().painted == 1 && tree.template get<FirstTag>().painted == 0;
}
static_assert(the_shown_one_alone_gets_it_all());

[[nodiscard]] constexpr bool past_the_last_means_none() {
    auto tree = pair_tree();
    auto& shown = tree.template get<ShownTag>();
    shown.show(1);
    shown.show(2);
    if (shown.shown() != std::nullopt) return false;
    shown.show(0);
    shown.show(std::numeric_limits<std::size_t>::max());
    return shown.shown() == std::nullopt;
}
static_assert(past_the_last_means_none());

[[nodiscard]] constexpr bool the_one_going_away_is_told_after_the_delivery() {
    auto tree = pair_tree();
    auto& shown = tree.template get<ShownTag>();
    shown.show(0);
    shown.show(1);
    if (tree.template get<FirstTag>().leaves != 0) return false;
    jgui_test::notify(tree, CollectDead{});
    if (tree.template get<FirstTag>().leaves != 1 || tree.template get<FirstTag>().focus_cleared != 1 || tree.template get<SecondTag>().leaves != 0) return false;
    jgui_test::notify(tree, CollectDead{});
    if (tree.template get<FirstTag>().leaves != 1) return false;
    // Away and back within one delivery: nothing went anywhere.
    shown.show(0);
    shown.show(1);
    jgui_test::notify(tree, CollectDead{});
    return tree.template get<FirstTag>().leaves == 1 && tree.template get<SecondTag>().leaves == 0;
}
static_assert(the_one_going_away_is_told_after_the_delivery());

[[nodiscard]] constexpr bool a_change_is_damage_and_the_same_is_not() {
    auto tree = pair_tree();
    auto& shown = tree.template get<ShownTag>();
    if (jgui_test::damaged(tree)) return false;
    shown.show(1);
    if (!jgui_test::damaged(tree) || !shown.own_damage()) return false;
    jgui_test::clear_damage(tree);
    if (jgui_test::damaged(tree)) return false;
    shown.show(1);
    if (jgui_test::damaged(tree)) return false;
    tree.template get<FirstTag>().wants_redraw = true;
    if (jgui_test::damaged(tree)) return false;
    tree.template get<SecondTag>().wants_redraw = true;
    return jgui_test::damaged(tree) && tree.template get<FirstTag>().damage_cleared == 1;
}
static_assert(a_change_is_damage_and_the_same_is_not(), "only the one shown can ask for a redraw");

// --- The whole window, through the real event loop ---

constexpr Size window_size{Width{220}, Height{110}};

template <std::size_t = 0>
struct Rig {
    using Tree = decltype(viewer::make_ui(viewer::LoadedFile{}));
    constexpr explicit Rig(std::string_view text, Size size = window_size) : core{HeadlessBackend{size}, viewer::make_ui(viewer::load_file(parse_jtext(text)))} { settle(); }
    constexpr Rig(std::string_view text, std::string_view settings, Size size = window_size)
        : core{HeadlessBackend{size}, viewer::make_ui(viewer::load_file(parse_jtext(text)), viewer::load_settings_file(parse_jtext(settings)))} {
        settle();
    }
    constexpr void settle() { require(core.process_pending(BlockShaper{}).has_value(), "a frame failed"); }
    template <class E>
    constexpr void deliver(const E& event) {
        jgui_test::backend(core).inject(event);
        settle();
    }
    template <class Tag>
    [[nodiscard]] constexpr auto& get() {
        return core.ui().template get<Tag>();
    }
    template <class ButtonTag>
    constexpr void click() {
        const Point middle = jgui_test::inside(get<ButtonTag>().bounds(), 2, 2);
        deliver(PointerMove{middle});
        deliver(PointerButton{middle, MouseButton::Left, ButtonState::Pressed, jgui_test::serial(1)});
        deliver(PointerButton{middle, MouseButton::Left, ButtonState::Released, jgui_test::serial(1)});
    }
    [[nodiscard]] constexpr std::optional<std::size_t> shown() { return get<viewer::SectionGrids>().shown(); }
    [[nodiscard]] constexpr std::string_view status() { return get<viewer::StatusLine>().text(); }

    ApplicationCore<HeadlessBackend, Tree> core;
};

constexpr std::int32_t rows_in_the_file = 10;

[[nodiscard]] constexpr bool it_opens_on_frames_with_all_that_is_left() {
    Rig<> rig{jgui_test::redraw_file_text(rows_in_the_file)};
    if (rig.shown() != viewer::frames_position || rig.status() != "Frames: 10 rows") return false;
    auto& grid = rig.get<viewer::FramesGridTag>();
    if (grid.rows().size() != 10 || grid.rows()[3].frame_id != 4 || grid.rows()[0].reason != "FirstFrame" || grid.rows()[3].reason != "WidgetDamage") return false;
    if (grid.rows()[3].width != Width{640} || grid.rows()[3].height != Height{480} || grid.rows()[3].total_ns != 11 + 22 + 33 + 55) return false;
    // It starts where the status line ends, and reaches the client area's far corner.
    const Rect client = WindowFrame<DefaultFrameMetrics>::client_rect(window_size);
    return grid.bounds().top() == top_after(rig.get<viewer::StatusLine>().bounds().bottom()) && grid.bounds().bottom() == client.bottom() && grid.bounds().left() == client.left() && grid.bounds().right() == client.right();
}
static_assert(it_opens_on_frames_with_all_that_is_left());

[[nodiscard]] constexpr bool a_new_size_is_all_taken() {
    Rig<> rig{jgui_test::redraw_file_text(rows_in_the_file)};
    constexpr Size bigger{Width{257}, Height{155}};
    rig.deliver(ResizeEvent{bigger});
    const Rect client = WindowFrame<DefaultFrameMetrics>::client_rect(bigger);
    const Rect grid = rig.get<viewer::FramesGridTag>().bounds();
    return grid.bottom() == client.bottom() && grid.right() == client.right();
}
static_assert(a_new_size_is_all_taken());

[[nodiscard]] constexpr bool each_button_shows_its_section() {
    Rig<> rig{jgui_test::redraw_file_text(rows_in_the_file)};
    rig.click<viewer::RunButton>();
    if (rig.shown() != viewer::run_position || rig.status() != "Run: 1 row") return false;
    const auto& run = rig.get<viewer::RunGridTag>().rows();
    if (run.size() != 1 || run[0].label != "for the viewer" || run[0].frames_seen != 10 || !run[0].ended) return false;
    rig.click<viewer::DamageButton>();
    if (rig.shown() != viewer::damage_position || rig.status() != "Damage: 10 rows" || rig.get<viewer::DamageGridTag>().rows()[9].class_name != "Button" || rig.get<viewer::DamageGridTag>().rows()[9].tag != "HelpButton") return false;
    rig.click<viewer::DeferredButton>();
    if (rig.shown() != viewer::deferred_position || rig.status() != "Deferred: 1 row" || rig.get<viewer::DeferredGridTag>().rows()[0].reason != "Resize" || rig.get<viewer::DeferredGridTag>().rows()[0].width != Width{800}) return false;
    rig.click<viewer::FramesButton>();
    return rig.shown() == viewer::frames_position && rig.status() == "Frames: 10 rows";
}
static_assert(each_button_shows_its_section());

[[nodiscard]] constexpr bool a_section_keeps_its_place_when_shown_again() {
    Rig<> rig{jgui_test::redraw_file_text(100)};
    const Point over_grid = jgui_test::inside(rig.get<viewer::FramesGridTag>().body_area(), 5, 5);
    rig.deliver(PointerScroll{over_grid, ScrollDown::of_steps(2), ScrollAcross{}});
    const Height scrolled = rig.get<viewer::FramesGridTag>().scroll_down();
    if (scrolled == Height{}) return false;
    rig.click<viewer::RunButton>();
    // A wheel over the other section's grid leaves this one alone.
    rig.deliver(PointerScroll{over_grid, ScrollDown::of_steps(2), ScrollAcross{}});
    if (rig.get<viewer::FramesGridTag>().scroll_down() != scrolled) return false;
    rig.click<viewer::FramesButton>();
    return rig.get<viewer::FramesGridTag>().scroll_down() == scrolled;
}
static_assert(a_section_keeps_its_place_when_shown_again());

[[nodiscard]] constexpr std::string file_with_section(std::string_view name, std::string_view fields, std::string_view rows) {
    std::string text{"=== jText File ===\n=== Section: "};
    text.append(name).append(" ===\n=== Fields ===\n").append(fields).append("=== End Fields ===\n=== Data ===\n").append(rows).append("=== End Data ===\n=== End Section ===\n=== End File ===\n");
    return text;
}

[[nodiscard]] constexpr bool a_refused_file_says_why_where_the_grid_would_be() {
    Rig<> rig{"=== jText File ===\n=== Section: Frames ===\n"};
    if (rig.shown() != std::nullopt || !rig.status().starts_with("jText line 2: ")) return false;
    rig.click<viewer::DamageButton>();
    return rig.shown() == std::nullopt && rig.status().starts_with("jText line 2: ");
}
static_assert(a_refused_file_says_why_where_the_grid_would_be());

[[nodiscard]] constexpr bool the_rest_of_the_window_is_plain_when_refused() {
    Rig<> rig{"nonsense"};
    if (rig.shown() != std::nullopt) return false;
    const Rect client = WindowFrame<DefaultFrameMetrics>::client_rect(window_size);
    const Point under_the_line{client.left(), top_after(rig.get<viewer::StatusLine>().bounds().bottom())};
    return jgui_test::backend(rig.core).pixel(under_the_line) == theme::background.argb && rig.get<viewer::FramesGridTag>().bounds() == Rect{};
}
static_assert(the_rest_of_the_window_is_plain_when_refused());

[[nodiscard]] constexpr bool a_missing_section_or_column_is_that_sections_trouble_alone() {
    constexpr std::string_view damage_fields = " 1. /# Record_ID/Integer\n 2. /# Frame_ID/Integer\n 3. /# Class_Name/String\n 4. /# Tag/String\n 5. /# Tree_Position/Integer\n";
    Rig<> rig{file_with_section("Damage", damage_fields, " 1. |# 1|2|C|T|3\n")};
    if (rig.shown() != std::nullopt || rig.status() != "the file has no section named Frames") return false;
    rig.click<viewer::DamageButton>();
    if (rig.shown() != viewer::damage_position || rig.status() != "Damage: 1 row") return false;
    Rig<> wrong{file_with_section("Frames", " 1. /# Frame_ID/Integer\n", "")};
    return wrong.shown() == std::nullopt && wrong.status() == "the section Frames has no field named Start_ns";
}
static_assert(a_missing_section_or_column_is_that_sections_trouble_alone());

[[nodiscard]] constexpr bool left_out_rows_are_counted_on_the_line() {
    constexpr std::string_view fields = " 1. /# Record_ID/Integer\n 2. /# Next_Frame_ID/Integer\n 3. /# Time_ns/Integer\n 4. /# Reason/String\n 5. /# Width/Integer\n 6. /# Height/Integer\n";
    constexpr std::string_view rows = " 1. |# 1|2|3|Resize|4|5\n 1. |# x|2|3|Resize|4|5\n 1. |# 2|2|3|Resize|-4|5\n 1. ?# 3\n";
    const std::string text = file_with_section("Deferred", fields, rows);
    Rig<> rig{text};
    rig.click<viewer::DeferredButton>();
    return rig.status() == "Deferred: 1 row, 3 rows left out: a value did not convert" && rig.get<viewer::DeferredGridTag>().rows().size() == 1;
}
static_assert(left_out_rows_are_counted_on_the_line(), "a word for a number, a negative length, and fields missing");

// --- The settings record, a file of its own ---

[[nodiscard]] constexpr bool the_two_settings_buttons_show_their_sections() {
    const std::string settings = jgui_test::settings_file_text();
    Rig<> rig{jgui_test::redraw_file_text(rows_in_the_file), settings};
    if (rig.shown() != viewer::frames_position) return false;
    rig.click<viewer::ItemsButton>();
    if (rig.shown() != viewer::items_position || rig.status() != "Items: 3 rows") return false;
    const auto& items = rig.get<viewer::ItemsGridTag>().rows();
    if (items.size() != 3 || items[0].name != "" || items[0].parent_id != 0 || items[1].class_name != "jgui::Button" || items[1].name != "demo::HelpButton" || items[1].left != Left{7} || items[1].top != Top{9} || items[1].bottom != Bottom{29} || items[1].right != Right{57} ||
        items[1].height != Height{20} || items[1].width != Width{50})
        return false;
    rig.click<viewer::ProblemsButton>();
    if (rig.shown() != viewer::problems_position || rig.status() != "Problems: 1 row") return false;
    const auto& problems = rig.get<viewer::ProblemsGridTag>().rows();
    return problems.size() == 1 && problems[0].record_id == 1 && problems[0].item_id == 3 && problems[0].what_failed == "Outside its parent";
}
static_assert(the_two_settings_buttons_show_their_sections());

// Neither file's trouble touches the other's sections, in either direction, and each section says for itself.
[[nodiscard]] constexpr bool each_file_stands_alone() {
    const std::string monitor = jgui_test::redraw_file_text(rows_in_the_file);
    const std::string settings = jgui_test::settings_file_text();
    Rig<> no_settings{monitor, "nonsense"};
    if (no_settings.shown() != viewer::frames_position || no_settings.status() != "Frames: 10 rows") return false;
    no_settings.click<viewer::ItemsButton>();
    if (no_settings.shown() != std::nullopt || !no_settings.status().starts_with("jText line 1: ")) return false;
    no_settings.click<viewer::ProblemsButton>();
    if (no_settings.shown() != std::nullopt || !no_settings.status().starts_with("jText line 1: ")) return false;
    no_settings.click<viewer::RunButton>();
    if (no_settings.shown() != viewer::run_position || no_settings.status() != "Run: 1 row") return false;

    Rig<> no_monitor{"nonsense", settings};
    if (no_monitor.shown() != std::nullopt || !no_monitor.status().starts_with("jText line 1: ")) return false;
    no_monitor.click<viewer::ItemsButton>();
    return no_monitor.shown() == viewer::items_position && no_monitor.status() == "Items: 3 rows";
}
static_assert(each_file_stands_alone());

// One bad row, and one section missing from the file, are that section's trouble alone.
[[nodiscard]] constexpr bool a_bad_settings_row_is_counted_and_a_missing_section_is_said() {
    std::string bad = jgui_test::settings_file_text();
    bad.replace(bad.find(jgui_test::settings_test_left), jgui_test::settings_test_left.size(), "nope");
    Rig<> rig{jgui_test::redraw_file_text(rows_in_the_file), bad};
    rig.click<viewer::ItemsButton>();
    if (rig.shown() != viewer::items_position || rig.status() != "Items: 2 rows, 1 row left out: a value did not convert" || rig.get<viewer::ItemsGridTag>().rows().size() != 2) return false;
    rig.click<viewer::ProblemsButton>();
    if (rig.shown() != viewer::problems_position || rig.status() != "Problems: 1 row") return false;

    // The monitor's file has neither section, so a wrong file given for the settings says so.
    Rig<> wrong{jgui_test::redraw_file_text(rows_in_the_file), jgui_test::redraw_file_text(rows_in_the_file)};
    wrong.click<viewer::ItemsButton>();
    return wrong.shown() == std::nullopt && wrong.status() == "the file has no section named Items";
}
static_assert(a_bad_settings_row_is_counted_and_a_missing_section_is_said());

}  // namespace

int main() {
    std::cout << "the viewer holds at compile time\n";
    return 0;
}
