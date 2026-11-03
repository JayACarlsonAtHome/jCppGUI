// File: tests/test_settings_recorder.cpp
// Purpose: The settings recorder. Each check on a row and how each can fail, how items are numbered, and what a known tree writes, in
//          static_assert through the real application with a test clock and no file. At run time: the real font and the real file, read
//          back by the reader the viewer uses, everything that can be wrong with its place, and a run compared with one that records nothing.
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "app_helpers.hpp"
#include "check.hpp"
#include "file_io.hpp"
#include "help_ui.hpp"
#include "jCppGUI/v001/jtext.hpp"
#include "jCppGUI/v001/redraw_monitor.hpp"
#include "jCppGUI/v001/settings_recorder.hpp"
#include "monitor_helpers.hpp"
#include "viewer_helpers.hpp"
#include "viewer_ui.hpp"

namespace {

using namespace jgui;
using jgui_test::BlockShaper;
using jgui_test::MemorySink;
using jgui_test::TemporaryDirectory;
using jgui_test::TestClock;

using TestRecorder = SettingsRecorder<TestClock, MemorySink>;
constexpr Size small_window{Width{120}, Height{60}};
constexpr Size other_window{Width{200}, Height{90}};
constexpr BlockShaper shaper{};

[[nodiscard]] constexpr TestRecorder test_recorder(SettingsRecorderOptions options = SettingsRecorderOptions{}) {
    return TestRecorder{TestClock{}, MemorySink{}, options};
}

// --- Choosing by type ---
static_assert(RedrawMonitor<TestRecorder> && ItemRecorder<TestRecorder> && !TestRecorder::watches);
static_assert(!ItemRecorder<FrameMonitor<TestClock, MemorySink>> && !ItemRecorder<NoMonitor>);
static_assert(RedrawMonitor<BothMonitors<NoMonitor, TestRecorder>> && ItemRecorder<BothMonitors<NoMonitor, TestRecorder>> && ItemRecorder<BothMonitors<TestRecorder, NoMonitor>>);
static_assert(RedrawMonitor<BothMonitors<NoMonitor, NoMonitor>> && !ItemRecorder<BothMonitors<NoMonitor, NoMonitor>>);
static_assert(BothMonitors<FrameMonitor<TestClock, MemorySink>, TestRecorder>::watches && BothMonitors<TestRecorder, FrameMonitor<TestClock, MemorySink>>::watches);
static_assert(!BothMonitors<NoMonitor, TestRecorder>::watches);
static_assert(std::is_empty_v<NoMonitor>);

// --- The checks on one row ---

// Left 10, Top 20, 30 across and 40 down, and all of it agrees.
[[nodiscard]] constexpr ItemRow sound_row(std::uint64_t id = 2, std::uint64_t parent = 1) {
    return item_row_of(ItemFound{id, parent, "C", "N", Rect{Point{Left{10}, Top{20}}, Size{Width{30}, Height{40}}}});
}
static_assert(sound_row().right == Right{40} && sound_row().bottom == Bottom{60} && sound_row().width == Width{30} && sound_row().height == Height{40});
static_assert(width_matches_its_edges(sound_row()) && height_matches_its_edges(sound_row()) && right_is_not_before_left(sound_row()) && bottom_is_not_before_top(sound_row()));

[[nodiscard]] constexpr ItemRow with_right(std::int32_t right) {
    ItemRow row = sound_row();
    row.right = Right{right};
    return row;
}
[[nodiscard]] constexpr ItemRow with_bottom(std::int32_t bottom) {
    ItemRow row = sound_row();
    row.bottom = Bottom{bottom};
    return row;
}
static_assert(!width_matches_its_edges(with_right(41)) && !width_matches_its_edges(with_right(39)) && height_matches_its_edges(with_right(41)));
static_assert(!height_matches_its_edges(with_bottom(61)) && !height_matches_its_edges(with_bottom(59)) && width_matches_its_edges(with_bottom(61)));
// One past each way, and exactly at the edge.
static_assert(right_is_not_before_left(with_right(10)) && !right_is_not_before_left(with_right(9)));
static_assert(bottom_is_not_before_top(with_bottom(20)) && !bottom_is_not_before_top(with_bottom(19)));

// At the limits of the type, a far edge saturates, and that is a width that no longer agrees.
constexpr std::int32_t int32_most = std::numeric_limits<std::int32_t>::max();
static_assert(!width_matches_its_edges(item_row_of(ItemFound{2, 1, "", "", Rect{Point{Left{int32_most}, Top{}}, Size{Width{5}, Height{}}}})));
static_assert(!height_matches_its_edges(item_row_of(ItemFound{2, 1, "", "", Rect{Point{Left{}, Top{int32_most}}, Size{Width{}, Height{5}}}})));

[[nodiscard]] constexpr ItemRow row_at(std::int32_t left, std::int32_t top, std::int32_t across, std::int32_t down) {
    return item_row_of(ItemFound{2, 1, "", "", Rect{Point{Left{left}, Top{top}}, Size{Width{across}, Height{down}}}});
}
constexpr ItemRow parent_row = row_at(10, 10, 100, 100);
static_assert(lies_inside_its_parent(row_at(10, 10, 100, 100), parent_row), "the same rectangle is inside");
static_assert(lies_inside_its_parent(row_at(50, 50, 10, 10), parent_row));
static_assert(!lies_inside_its_parent(row_at(9, 10, 10, 10), parent_row), "past the left");
static_assert(!lies_inside_its_parent(row_at(10, 9, 10, 10), parent_row), "past the top");
static_assert(!lies_inside_its_parent(row_at(10, 10, 101, 10), parent_row), "past the right");
static_assert(!lies_inside_its_parent(row_at(10, 10, 10, 101), parent_row), "past the bottom");
static_assert(lies_inside_its_parent(row_at(500, 500, 0, 10), parent_row) && lies_inside_its_parent(row_at(500, 500, 10, 0), parent_row), "nothing in it has no pixels to be outside with");
static_assert(!lies_inside_its_parent(row_at(10, 10, 1, 1), row_at(10, 10, 0, 0)), "an empty parent holds nothing");

// --- All of them, over rows ---
[[nodiscard]] constexpr std::vector<ProblemRow> problems_of(std::vector<ItemRow> rows) {
    ProblemList list{100};
    find_item_problems(rows, list);
    return list.take_rows();
}
[[nodiscard]] constexpr ItemRow window_row() { return item_row_of(ItemFound{1, 0, "W", "", Rect{Point{}, Size{Width{100}, Height{100}}}}); }

static_assert(problems_of({}).empty());
static_assert(problems_of({window_row()}).empty());
static_assert(problems_of({window_row(), sound_row()}).empty());

[[nodiscard]] constexpr bool says_one(const std::vector<ProblemRow>& rows, std::uint64_t item, std::string_view what) {
    return rows.size() == 1 && rows[0].record_id == 1 && rows[0].item_id == item && rows[0].what == what;
}
static_assert(says_one(problems_of({window_row(), with_right(41)}), 2, problem_width));
static_assert(says_one(problems_of({window_row(), with_bottom(61)}), 2, problem_height));
static_assert(says_one(problems_of({window_row(), sound_row(2, 7)}), 2, problem_parent), "a parent that is not there");
static_assert(says_one(problems_of({item_row_of(ItemFound{2, 0, "", "", Rect{}})}), 2, problem_parent), "no parent, and not the root");
static_assert(says_one(problems_of({window_row(), row_at(90, 0, 11, 1)}), 2, problem_outside));

[[nodiscard]] constexpr bool an_edge_before_the_other_is_two_problems() {
    const std::vector<ProblemRow> across = problems_of({window_row(), with_right(9)});
    const std::vector<ProblemRow> down = problems_of({window_row(), with_bottom(19)});
    return across.size() == 2 && across[0].what == problem_width && across[1].what == problem_right && across[1].record_id == 2 && down.size() == 2 && down[0].what == problem_height && down[1].what == problem_bottom;
}
static_assert(an_edge_before_the_other_is_two_problems());

[[nodiscard]] constexpr bool problems_are_bounded_and_say_so() {
    ProblemList list{2};
    list.add(1, "a");
    list.add(2, "b");
    list.add(3, "c");
    list.add(4, "d");
    const std::vector<ProblemRow> rows = list.take_rows();
    return rows.size() == 3 && rows[1].item_id == 2 && rows[2].record_id == 3 && rows[2].item_id == 0 && rows[2].what == "2 more problems were not listed";
}
static_assert(problems_are_bounded_and_say_so());

static_assert(position_of_item(std::vector<ItemRow>{}, 1) == std::nullopt);

// --- Numbers for items that come and go ---
consteval bool numbers_are_kept_and_never_used_again() {
    ItemNumbers numbers{4};
    numbers.begin_walk(5);
    const ItemKey a{5, 0, 0};
    const ItemKey b{5, 1, 0};
    if (numbers.id_for(a) != 6 || numbers.id_for(b) != 7) return false;
    numbers.end_walk();

    // Each keeps its number, in either order.
    numbers.begin_walk(5);
    if (numbers.id_for(b) != 7 || numbers.id_for(a) != 6) return false;
    numbers.end_walk();

    // a has gone. The same slot in a new generation is a new item, and a's number is not used again.
    numbers.begin_walk(5);
    if (numbers.id_for(b) != 7 || numbers.id_for(ItemKey{5, 0, 1}) != 8) return false;
    numbers.end_walk();
    numbers.begin_walk(5);
    return numbers.id_for(a) == 9;
}
static_assert(numbers_are_kept_and_never_used_again());

consteval bool numbers_start_above_the_tree_and_stop_at_the_limit() {
    ItemNumbers numbers{2};
    numbers.begin_walk(100);
    if (numbers.id_for(ItemKey{1, 0, 0}) != 101 || numbers.id_for(ItemKey{1, 1, 0}) != 102) return false;
    if (numbers.id_for(ItemKey{1, 2, 0}) != 0 || numbers.id_for(ItemKey{1, 3, 0}) != 0 || numbers.refused() != 2) return false;
    numbers.end_walk();
    numbers.begin_walk(100);
    return numbers.refused() == 0 && numbers.id_for(ItemKey{1, 3, 0}) == 103;
}
static_assert(numbers_start_above_the_tree_and_stop_at_the_limit());

// How many items a compile-time tree has: the frame, the bar, its button, and the help panel.
using DemoTree = decltype(demo::make_ui());
static_assert(detail::fixed_items_in_tuple(std::type_identity<DemoTree::children_type>{}) == 4);
struct NoneTag;
static_assert(detail::fixed_item_count<Label<NoneTag>>() == 1);
static_assert(detail::fixed_item_count<Dynamic<NoneTag, Label<NoneTag>>>() == 1, "a Dynamic is one, however many it holds");

// --- A known tree gives exact rows ---
struct BarButton;
struct Line;

struct DoNothing {
    template <class U>
    constexpr void operator()(U&) const {}
};

[[nodiscard]] constexpr auto known_tree() { return window(menu_bar(button<BarButton>("ab", DoNothing{})), label<Line>("text")); }

[[nodiscard]] constexpr bool row_is(const ItemRow& row, std::uint64_t id, std::uint64_t parent, std::int32_t left, std::int32_t top, std::int32_t bottom, std::int32_t right, std::int32_t height, std::int32_t width) {
    return row.id == id && row.parent_id == parent && row.left == Left{left} && row.top == Top{top} && row.bottom == Bottom{bottom} && row.right == Right{right} && row.height == Height{height} && row.width == Width{width};
}

consteval bool known_tree_gives_exact_rows() {
    ApplicationCore core{HeadlessBackend{small_window}, known_tree(), test_recorder()};
    if (!core.process_pending(shaper).has_value()) return false;
    const auto& rows = jgui_test::monitor(core).rows();
    if (rows.size() != 4) return false;
    if (!row_is(rows[0], 1, 0, 0, 0, 60, 120, 60, 120) || !rows[0].class_name.ends_with("Window") || !rows[0].name.empty()) return false;
    // The bar's strip is 4 of text, 3 + 3 of the button, and 2 + 2 of its own.
    if (!row_is(rows[1], 2, 1, 0, 0, 14, 120, 14, 120) || !rows[1].class_name.ends_with("MenuBar") || !rows[1].name.ends_with("MenuBarTag")) return false;
    // Two characters are 4 across, with 10 + 10 of padding, inside the bar's 2 and 2.
    if (!row_is(rows[2], 3, 2, 2, 2, 12, 26, 10, 24) || !rows[2].class_name.ends_with("Button") || rows[2].name != detail::type_name_of<BarButton>()) return false;
    // Below the bar: 4 of text with 6 + 6 of padding.
    if (!row_is(rows[3], 4, 1, 0, 14, 30, 120, 16, 120) || !rows[3].class_name.ends_with("Label") || rows[3].name != detail::type_name_of<Line>()) return false;
    return jgui_test::monitor(core).problems().empty();
}
static_assert(known_tree_gives_exact_rows());

// The text of the file: what a reader sees in each column, in the owner's order, and the first column is the record id.
consteval bool file_has_the_columns_in_order() {
    ApplicationCore core{HeadlessBackend{small_window}, known_tree(), test_recorder()};
    if (!core.process_pending(shaper).has_value()) return false;
    const std::string text = jgui_test::monitor(core).file_text();
    if (!jgui_test::blocks_all_close(text) || !text.contains("//Related: type=jCppGUI_settings_record\n")) return false;
    const auto items = jgui_test::rows_of_section(text, "Items");
    if (items.size() != 4 || !jgui_test::rows_of_section(text, "Problems").empty()) return false;
    const std::vector<std::string>& button_row = items[2];
    return button_row.size() == 10 && button_row[0] == "3" && button_row[1] == "2" && button_row[2].ends_with("Button") && button_row[3].ends_with("BarButton") && button_row[4] == "2" && button_row[5] == "2" && button_row[6] == "12" && button_row[7] == "26" &&
           button_row[8] == "10" && button_row[9] == "24" && items[0][3].empty() && text.contains("=== Section: Problems ===");
}
static_assert(file_has_the_columns_in_order());

// Through the reader the viewer uses, into the viewer's own rows.
consteval bool round_trip_through_the_reader() {
    ApplicationCore core{HeadlessBackend{small_window}, known_tree(), test_recorder()};
    if (!core.process_pending(shaper).has_value()) return false;
    const viewer::LoadedSettings loaded = viewer::load_settings_file(parse_jtext(jgui_test::monitor(core).file_text()));
    if (!loaded.items.usable || !loaded.problems.usable || loaded.items.rows.size() != 4 || !loaded.problems.rows.empty()) return false;
    if (loaded.items.status != "Items: 4 rows" || loaded.problems.status != "Problems: 0 rows") return false;
    const viewer::ItemsRow& row = loaded.items.rows[2];
    return row.id == 3 && row.parent_id == 2 && row.class_name.ends_with("Button") && row.name.ends_with("BarButton") && row.left == Left{2} && row.top == Top{2} && row.bottom == Bottom{12} && row.right == Right{26} && row.height == Height{10} && row.width == Width{24} &&
           loaded.items.rows[0].name.empty();
}
static_assert(round_trip_through_the_reader());

// --- When it writes ---

// A size that is not the last one, and everything after it.
consteval bool resize_changes_the_rows() {
    ApplicationCore core{HeadlessBackend{small_window}, known_tree(), test_recorder(SettingsRecorderOptions{.write_interval_ns = 0})};
    if (!core.process_pending(shaper).has_value()) return false;
    auto& recorder = jgui_test::monitor(core);
    const std::string before = recorder.sink().saved;
    jgui_test::backend(core).inject(ResizeEvent{other_window});
    if (!core.process_pending(shaper).has_value()) return false;
    const auto& rows = recorder.rows();
    return row_is(rows[0], 1, 0, 0, 0, 90, 200, 90, 200) && row_is(rows[1], 2, 1, 0, 0, 14, 200, 14, 200) && row_is(rows[2], 3, 2, 2, 2, 12, 26, 10, 24) && row_is(rows[3], 4, 1, 0, 14, 30, 200, 16, 200) && recorder.writes() == 2 && recorder.sink().saved != before;
}
static_assert(resize_changes_the_rows());

// A frame that changes nothing recorded writes nothing, however long it has been.
consteval bool no_change_means_no_write() {
    ApplicationCore core{HeadlessBackend{small_window}, known_tree(), test_recorder(SettingsRecorderOptions{.write_interval_ns = 0})};
    if (!core.process_pending(shaper).has_value()) return false;
    auto& recorder = jgui_test::monitor(core);
    if (recorder.writes() != 1 || recorder.sink().saves != 1) return false;
    const std::uint64_t frames = core.frames();
    // The same size again: a real frame, with nothing different in it.
    jgui_test::backend(core).inject(ResizeEvent{small_window});
    if (!core.process_pending(shaper).has_value() || core.frames() != frames + 1) return false;
    // And no frame at all.
    if (!core.process_pending(shaper).has_value()) return false;
    return recorder.writes() == 1 && recorder.sink().saves == 1;
}
static_assert(no_change_means_no_write());

// The first at once, then at most once an interval, and the last at the end.
consteval bool writes_at_most_once_an_interval_and_at_the_end() {
    ApplicationCore core{HeadlessBackend{small_window}, known_tree(), test_recorder(SettingsRecorderOptions{.write_interval_ns = 1000})};
    auto& recorder = jgui_test::monitor(core);
    if (!core.process_pending(shaper).has_value() || recorder.writes() != 1) return false;

    jgui_test::backend(core).inject(ResizeEvent{other_window});
    recorder.clock().queue({999});
    if (!core.process_pending(shaper).has_value() || recorder.writes() != 1) return false;
    if (recorder.sink().saved.contains("200")) return false;

    // Nothing new, and now the time has come: the change above goes out.
    recorder.clock().queue({1});
    if (!core.process_pending(shaper).has_value() || recorder.writes() != 2 || !recorder.sink().saved.contains("200")) return false;

    core.finish_monitor();
    return recorder.writes() == 3 && recorder.sink().saves == 3;
}
static_assert(writes_at_most_once_an_interval_and_at_the_end());

// A frame that could not go out was not drawn, and is not recorded.
consteval bool a_failed_write_stops_writing_and_nothing_else() {
    ApplicationCore core{HeadlessBackend{small_window}, known_tree(), test_recorder(SettingsRecorderOptions{.write_interval_ns = 0})};
    auto& recorder = jgui_test::monitor(core);
    recorder.sink().fail = true;
    if (!core.process_pending(shaper).has_value() || recorder.writes() != 1 || recorder.sink().attempts != 1) return false;
    const std::optional<Error> reason = core.monitor_error();
    if (!reason || reason->message != "this sink was told to fail") return false;
    jgui_test::backend(core).inject(ResizeEvent{other_window});
    if (!core.process_pending(shaper).has_value() || core.frames() != 2) return false;
    core.finish_monitor();
    return recorder.sink().attempts == 1 && recorder.rows()[0].width == Width{200};
}
static_assert(a_failed_write_stops_writing_and_nothing_else());

// --- Items that come and go, through the real demo tree ---
template <class Core>
consteval bool click_help(Core& core) {
    const Point on_help = jgui_test::inside(core.ui().template get<demo::HelpButton>().bounds(), 1, 1);
    jgui_test::backend(core).inject(PointerMove{on_help});
    jgui_test::backend(core).inject(PointerButton{on_help, MouseButton::Left, ButtonState::Pressed, jgui_test::serial(1)});
    jgui_test::backend(core).inject(PointerButton{on_help, MouseButton::Left, ButtonState::Released, jgui_test::serial(2)});
    return core.process_pending(shaper).has_value();
}

consteval bool items_keep_a_number_and_never_share_one() {
    ApplicationCore core{HeadlessBackend{small_window}, demo::make_ui<std::vector>(), test_recorder(SettingsRecorderOptions{.write_interval_ns = 0})};
    if (!core.process_pending(shaper).has_value()) return false;
    auto& recorder = jgui_test::monitor(core);
    const auto& rows = recorder.rows();
    // The window, the frame, the bar, the button and the panel, in depth-first order.
    if (rows.size() != 5 || rows[4].class_name != "demo::HelpPanel" || rows[3].name != "demo::HelpButton") return false;
    if (rows[1].parent_id != 1 || rows[2].parent_id != 2 || rows[3].parent_id != 3 || rows[4].parent_id != 2) return false;
    if (!recorder.problems().empty()) return false;
    const ItemRow panel = rows[4];

    // Shown: the sixth, under the panel, and nothing else has moved.
    if (!click_help(core) || rows.size() != 6 || rows[5].id != 6 || rows[5].parent_id != 5 || !rows[5].class_name.ends_with("Label") || rows[5].name != "demo::HelpLine" || rows[4] != panel) return false;
    if (rows[5].left != panel.left || rows[5].top != panel.top || rows[5].width != panel.width || rows[5].height == Height{}) return false;
    if (!recorder.problems().empty()) return false;

    // A frame with no change to it keeps that number, and writes nothing.
    const std::uint64_t writes = recorder.writes();
    jgui_test::backend(core).inject(ResizeEvent{small_window});
    if (!core.process_pending(shaper).has_value() || rows.size() != 6 || rows[5].id != 6 || recorder.writes() != writes) return false;

    // Hidden: its row goes, and the five stay the same.
    if (!click_help(core) || rows.size() != 5 || rows[4] != panel || recorder.writes() != writes + 1) return false;

    // Shown again: its slot is used again, and it is a new item with a new number.
    return click_help(core) && rows.size() == 6 && rows[5].id == 7 && rows[5].parent_id == 5;
}
static_assert(items_keep_a_number_and_never_share_one());

// --- A composite inside a Dynamic's item, and a Dynamic with room for only so many ---
struct BarItem;
struct BarItemButton;
struct Holder;

// A bar that is an item: a composite with its own fixed child.
struct ItemBar {
    using tag = BarItem;
    using children_type = std::tuple<Button<BarItemButton, DoNothing>>;
    children_type children_{Button<BarItemButton, DoNothing>{CheckedText{"x"}, DoNothing{}}};
    Rect area{};
    [[nodiscard]] constexpr Rect bounds() const noexcept { return area; }
    [[nodiscard]] constexpr children_type& children() noexcept { return children_; }
    constexpr void paint(const TreeKey&, Canvas&) {}
    template <TextShaper Shaper>
    constexpr std::expected<Rect, Error> layout(const TreeKey& key, Rect offered, const Shaper& text) {
        area = offered.intersect(Rect{offered.origin(), Size{offered.width(), Height{12}}});
        if (auto used = detail::layout_in_order(children_, key, area, text); !used) return std::unexpected(std::move(used.error()));
        return Rect{Point{offered.left(), top_after(area.bottom())}, Size{offered.width(), offered.height() - area.height()}};
    }
    template <class U, class E>
    constexpr bool handle(const TreeKey&, U&, const E&) {
        return false;
    }
    template <class U, class E>
    constexpr void notify(const TreeKey&, U&, const E&) {}
};

template <std::size_t Items>
consteval bool composites_in_a_dynamic(SettingsRecorderOptions options, std::size_t rows_expected, std::string_view problem) {
    ApplicationCore core{HeadlessBackend{small_window}, window(Dynamic<Holder, ItemBar, std::vector>{}), test_recorder(options)};
    auto& holder = core.ui().template get<Holder>();
    for (std::size_t i = 0; i < Items; ++i)
        if (!holder.create()) return false;
    if (!core.process_pending(shaper).has_value()) return false;
    auto& recorder = jgui_test::monitor(core);
    if (recorder.rows().size() != rows_expected) return false;
    if (problem.empty()) return recorder.problems().empty();
    return says_one(recorder.problems(), 0, problem);
}
// The window and the Dynamic, then each item and its button, numbered above them.
consteval bool numbers_inside_an_item() {
    ApplicationCore core{HeadlessBackend{small_window}, window(Dynamic<Holder, ItemBar, std::vector>{}), test_recorder()};
    auto& holder = core.ui().template get<Holder>();
    if (!holder.create() || !holder.create()) return false;
    if (!core.process_pending(shaper).has_value()) return false;
    const auto& rows = jgui_test::monitor(core).rows();
    if (rows.size() != 6 || !rows[1].class_name.ends_with("Dynamic")) return false;
    if (rows[2].id != 3 || rows[2].parent_id != 2 || rows[3].id != 4 || rows[3].parent_id != 3 || !rows[3].class_name.ends_with("Button")) return false;
    if (rows[4].id != 5 || rows[4].parent_id != 2 || rows[5].id != 6 || rows[5].parent_id != 5) return false;
    return jgui_test::monitor(core).problems().empty();
}
static_assert(numbers_inside_an_item());
static_assert(composites_in_a_dynamic<2>(SettingsRecorderOptions{}, 6, ""));
// Room for three: the first bar and its button, and the second bar with no room left for its button. The tree itself is always there.
static_assert(composites_in_a_dynamic<2>(SettingsRecorderOptions{.item_limit = 3}, 5, "1 items are over the limit of 3 and not in this file"));
static_assert(composites_in_a_dynamic<3>(SettingsRecorderOptions{.item_limit = 1}, 3, "3 items are over the limit of 1 and not in this file"));

// --- A widget not shown ---
struct Shown;
struct NotShown;
struct Pair;

consteval bool a_widget_not_shown_has_no_rectangle() {
    ApplicationCore core{HeadlessBackend{small_window}, window(one_shown<Pair>(label<Shown>("a"), label<NotShown>("b"))), test_recorder()};
    core.ui().template get<Pair>().show(std::optional<std::size_t>{1});
    if (!core.process_pending(shaper).has_value()) return false;
    const auto& rows = jgui_test::monitor(core).rows();
    if (rows.size() != 4 || rows[2].width != Width{} || rows[2].left != Left{} || rows[3].width != small_window.width()) return false;
    // Now the other one, at a smaller size: what the one going away last had must not count against it.
    jgui_test::backend(core).inject(ResizeEvent{Size{Width{50}, Height{20}}});
    core.ui().template get<Pair>().show(std::optional<std::size_t>{0});
    if (!core.process_pending(shaper).has_value()) return false;
    return rows[2].width == Width{50} && rows[3].width == Width{} && rows[3].bottom == Bottom{} && jgui_test::monitor(core).problems().empty();
}
static_assert(a_widget_not_shown_has_no_rectangle());

// --- A broken layout is written down, and ends nothing ---
struct BrokenTag;

// Says it is wherever it is told to, whatever it was offered.
struct BrokenWidget {
    using tag = BrokenTag;
    Rect claimed{};
    [[nodiscard]] constexpr Rect bounds() const noexcept { return claimed; }
    constexpr void paint(const TreeKey&, Canvas&) {}
    template <TextShaper Shaper>
    constexpr std::expected<Rect, Error> layout(const TreeKey&, Rect offered, const Shaper&) {
        return offered;
    }
    template <class U, class E>
    constexpr bool handle(const TreeKey&, U&, const E&) {
        return false;
    }
    template <class U, class E>
    constexpr void notify(const TreeKey&, U&, const E&) {}
};
static_assert(Widget<BrokenWidget>);

consteval bool a_broken_widget_is_a_problems_row() {
    const Rect too_big{Point{Left{100}, Top{10}}, Size{Width{21}, Height{5}}};
    ApplicationCore core{HeadlessBackend{small_window}, window(BrokenWidget{too_big}), test_recorder()};
    if (!core.process_pending(shaper).has_value() || core.frames() != 1) return false;
    auto& recorder = jgui_test::monitor(core);
    if (!says_one(recorder.problems(), 2, problem_outside)) return false;
    const auto problems = jgui_test::rows_of_section(recorder.sink().saved, "Problems");
    return problems.size() == 1 && problems[0] == std::vector<std::string>{"1", "2", std::string{problem_outside}};
}
static_assert(a_broken_widget_is_a_problems_row());

// One past the size and it is gone, so that the row above is about that one pixel.
consteval bool one_pixel_less_is_no_problem() {
    const Rect fits{Point{Left{100}, Top{10}}, Size{Width{20}, Height{5}}};
    ApplicationCore core{HeadlessBackend{small_window}, window(BrokenWidget{fits}), test_recorder()};
    return core.process_pending(shaper).has_value() && jgui_test::monitor(core).problems().empty();
}
static_assert(one_pixel_less_is_no_problem());

consteval bool a_width_at_the_limit_is_a_problems_row() {
    const Rect far_off{Point{Left{int32_most}, Top{}}, Size{Width{5}, Height{5}}};
    ApplicationCore core{HeadlessBackend{small_window}, window(BrokenWidget{far_off}), test_recorder()};
    if (!core.process_pending(shaper).has_value()) return false;
    const auto& problems = jgui_test::monitor(core).problems();
    return problems.size() == 2 && problems[0].what == problem_width && problems[1].what == problem_outside;
}
static_assert(a_width_at_the_limit_is_a_problems_row());

// A row that cannot stand in a line is left out, and that is a problem too. No compile-time name does this. The point is open to any walk.
consteval bool a_row_that_cannot_be_written_is_a_problems_row() {
    TestRecorder recorder = test_recorder();
    recorder.items_begin(2);
    recorder.item_found(ItemFound{1, 0, "Window", "", Rect{Point{}, small_window}});
    recorder.item_found(ItemFound{2, 1, "Bad\nName", "", Rect{}});
    recorder.items_end();
    const std::string text = recorder.file_text();
    if (!says_one(recorder.problems(), 2, problem_not_written) || !jgui_test::blocks_all_close(text)) return false;
    return jgui_test::rows_of_section(text, "Items").size() == 1 && jgui_test::rows_of_section(text, "Problems").size() == 1 && parse_jtext(text).has_value();
}
static_assert(a_row_that_cannot_be_written_is_a_problems_row());

// --- Sizes the toolkit's own widgets lay out with no room: its promise holds at the smallest windows ---
template <std::int32_t Across, std::int32_t Down>
consteval bool own_widgets_keep_the_promise() {
    ApplicationCore demo_core{HeadlessBackend{Size{Width{Across}, Height{Down}}}, demo::make_ui<std::vector>(), test_recorder()};
    if (!demo_core.process_pending(shaper).has_value() || !click_help(demo_core)) return false;
    if (!jgui_test::monitor(demo_core).problems().empty()) return false;
    ApplicationCore viewer_core{HeadlessBackend{Size{Width{Across}, Height{Down}}}, viewer::make_ui(viewer::load_file(parse_jtext(jgui_test::redraw_file_text(3))), viewer::load_settings_file(parse_jtext(jgui_test::settings_file_text()))), test_recorder()};
    return viewer_core.process_pending(shaper).has_value() && jgui_test::monitor(viewer_core).rows().size() == 19 && jgui_test::monitor(viewer_core).problems().empty();
}
static_assert(own_widgets_keep_the_promise<1, 1>());
static_assert(own_widgets_keep_the_promise<3, 3>());
static_assert(own_widgets_keep_the_promise<20, 5>());
static_assert(own_widgets_keep_the_promise<200, 120>());

// --- Both monitors in the one slot ---
using TestFrames = FrameMonitor<TestClock, MemorySink>;
using TestBoth = BothMonitors<TestFrames, TestRecorder>;

// The frame monitor writes the same file with a recorder beside it as it does alone, and the recorder gets its rows.
consteval bool both_monitors_each_do_their_own() {
    ApplicationCore alone{HeadlessBackend{small_window}, known_tree(), TestFrames{TestClock{}, MemorySink{}, FrameMonitorOptions{}}};
    ApplicationCore together{HeadlessBackend{small_window}, known_tree(), TestBoth{TestFrames{TestClock{}, MemorySink{}, FrameMonitorOptions{}}, test_recorder()}};
    // Damage for the first monitor to name, in each.
    if (!alone.ui().get<Line>().set_text("new") || !together.ui().get<Line>().set_text("new")) return false;
    jgui_test::monitor(alone).clock().queue({5, 10, 20, 30, 40, 50});
    jgui_test::monitor(together).first().clock().queue({5, 10, 20, 30, 40, 50});
    if (!alone.process_pending(shaper).has_value() || !together.process_pending(shaper).has_value()) return false;
    alone.finish_monitor();
    together.finish_monitor();
    auto& both = jgui_test::monitor(together);
    if (both.first().sink().saved != jgui_test::monitor(alone).sink().saved || both.first().damage().size() == 0) return false;
    if (both.second().rows().size() != 4 || both.second().writes() != 2) return false;
    return !both.second().sink().saved.contains("Frames") && both.error() == std::nullopt;
}
static_assert(both_monitors_each_do_their_own());

consteval bool both_monitors_say_each_failure() {
    TestBoth both{TestFrames{TestClock{}, MemorySink{}, FrameMonitorOptions{}}, test_recorder()};
    both.second().sink().fail = true;
    both.finish();
    if (!both.error() || both.error()->message != "this sink was told to fail") return false;
    both.first().sink().fail = true;
    TestBoth both_fail{TestFrames{TestClock{}, std::move(both.first().sink()), FrameMonitorOptions{}}, test_recorder()};
    both_fail.second().sink().fail = true;
    both_fail.finish();
    return both_fail.error() && both_fail.error()->message == "this sink was told to fail; this sink was told to fail";
}
static_assert(both_monitors_say_each_failure());

// --- The real thing, at run time ---
using RealRecorder = SettingsRecorder<SteadyClock, FileSink>;

[[nodiscard]] RealRecorder recorder_in(const std::string& directory) { return RealRecorder{SteadyClock{}, FileSink{directory, default_settings_file_name}}; }

void test_real_file(jgui_test::Checker& check) {
    const TemporaryDirectory directory;
    auto app = make_application(HeadlessBackend{small_window}, demo::make_ui(), recorder_in(directory.path()));
    check(app.has_value(), "an application with a recorder starts");
    if (!app) return;
    check(app->process_pending().has_value(), "a frame");
    const std::filesystem::path file = std::filesystem::path{directory.path()} / default_settings_file_name;
    check(std::filesystem::exists(file), "the file is there from the first frame");
    jgui_test::click(*app, jgui_test::centre(app->ui().get<demo::HelpButton>().bounds()));
    app->finish_monitor();
    check(!app->monitor_error().has_value(), "with nothing to report");
    const auto read = read_jtext_file(file.string());
    check(read.has_value(), "the reader takes the file");
    const viewer::LoadedSettings loaded = viewer::load_settings_file(read);
    const auto& rows = jgui_test::monitor(*app).rows();
    check(loaded.items.usable && loaded.items.rows.size() == 6 && rows.size() == 6, "six items, with the help line");
    check(loaded.problems.usable && loaded.problems.rows.empty(), "and no problems");
    for (std::size_t i = 0; i < rows.size() && i < loaded.items.rows.size(); ++i) {
        const viewer::ItemsRow& back = loaded.items.rows[i];
        check(back.id == rows[i].id && back.parent_id == rows[i].parent_id && back.class_name == rows[i].class_name && back.name == rows[i].name, "each item's names read back as written");
        check(back.left == rows[i].left && back.top == rows[i].top && back.bottom == rows[i].bottom && back.right == rows[i].right && back.height == rows[i].height && back.width == rows[i].width, "and each item's place");
    }
    check((std::filesystem::status(file).permissions() & std::filesystem::perms::all) == (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write), "the file is private");
    check(!std::filesystem::exists(file.string() + ".tmp"), "and no temporary file is left");
}

void test_refusals(jgui_test::Checker& check) {
    const auto carries_on = [&](RealRecorder recorder, std::string_view says, const char* what) {
        auto app = make_application(HeadlessBackend{small_window}, demo::make_ui(), std::move(recorder));
        if (!app) {
            check(false, "the application starts");
            return;
        }
        check(app->process_pending().has_value() && app->frames() == 1, what);
        jgui_test::click(*app, jgui_test::centre(app->ui().get<demo::HelpButton>().bounds()));
        check(app->frames() > 1, "the program goes on");
        const auto reason = app->monitor_error();
        check(reason && reason->message.contains(says), "and the reason can be read at exit");
    };
    {
        const TemporaryDirectory directory;
        const std::filesystem::path file = std::filesystem::path{directory.path()} / default_settings_file_name;
        const std::filesystem::path target = std::filesystem::path{directory.path()} / "target";
        std::filesystem::create_symlink(target, file);
        carries_on(recorder_in(directory.path()), "not a regular file", "a symlink at the name");
        check(!std::filesystem::exists(target) && std::filesystem::is_symlink(file), "is not followed and not removed");
    }
    {
        const TemporaryDirectory directory;
        const std::filesystem::path file = std::filesystem::path{directory.path()} / default_settings_file_name;
        std::filesystem::create_directory(file);
        carries_on(recorder_in(directory.path()), "not a regular file", "a directory at the name");
        check(std::filesystem::is_directory(file), "is left alone");
    }
    {
        const TemporaryDirectory directory;
        carries_on(recorder_in(directory.path() + "/no/such/directory"), "cannot open the directory", "a directory that does not exist");
        carries_on(RealRecorder{SteadyClock{}, FileSink::in_base_directory("a/b")}, "plain name", "a name with a directory in it, in the base directory");
    }
}

// A recorder changes no pixel, and no frame count.
void test_recorder_changes_nothing(jgui_test::Checker& check) {
    const TemporaryDirectory directory;
    auto plain = make_application(HeadlessBackend{small_window}, demo::make_ui());
    auto recorded = make_application(HeadlessBackend{small_window}, demo::make_ui(), recorder_in(directory.path()));
    if (!plain || !recorded) {
        check(false, "both applications start");
        return;
    }
    const auto same = [&](const char* what) {
        const HeadlessBackend& a = jgui_test::backend(*plain);
        const HeadlessBackend& b = jgui_test::backend(*recorded);
        bool all = plain->frames() == recorded->frames();
        for (std::int32_t y = 0; y < 60; ++y)
            for (std::int32_t x = 0; x < 120; ++x) all = all && a.pixel(Point{Left{x}, Top{y}}) == b.pixel(Point{Left{x}, Top{y}});
        check(all, what);
    };
    check(plain->process_pending().has_value() && recorded->process_pending().has_value(), "a first frame for each");
    same("the first frame is the same");
    const Point at = jgui_test::centre(plain->ui().get<demo::HelpButton>().bounds());
    jgui_test::click(*plain, at);
    jgui_test::click(*recorded, at);
    same("and after a click");
}

}  // namespace

int main() {
    jgui_test::Checker check;
    test_real_file(check);
    test_refusals(check);
    test_recorder_changes_nothing(check);
    return check.finish();
}
