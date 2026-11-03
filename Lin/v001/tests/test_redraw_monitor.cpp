// File: tests/test_redraw_monitor.cpp
// Purpose: The redraw monitor. With a test clock and no file, what each frame records is known exactly, and runs in
//          static_assert through the real application and the demo's tree. At run time: the real font, the real file
//          with everything that can be wrong with its path, and a run with and without a monitor compared pixel by pixel.
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "app_helpers.hpp"
#include "check.hpp"
#include "file_io.hpp"
#include "help_ui.hpp"
#include "jCppGUI/v001/redraw_monitor.hpp"
#include "monitor_helpers.hpp"

namespace {

using namespace jgui;
using jgui_test::BlockShaper;
using jgui_test::MemorySink;
using jgui_test::TemporaryDirectory;
using jgui_test::TestClock;

using TestMonitor = FrameMonitor<TestClock, MemorySink>;
constexpr Size small_window{Width{120}, Height{60}};
constexpr Size other_window{Width{130}, Height{60}};
constexpr Rect small_whole{Point{}, small_window};

[[nodiscard]] constexpr TestMonitor test_monitor(FrameMonitorOptions options = FrameMonitorOptions{}) {
    return TestMonitor{TestClock{}, MemorySink{}, options};
}

// --- Choosing by type ---
static_assert(std::is_empty_v<NoMonitor> && RedrawMonitor<NoMonitor> && RedrawMonitor<TestMonitor>);
static_assert(!RedrawMonitor<int>);
static_assert(std::same_as<ApplicationCore<HeadlessBackend, Window<>>, ApplicationCore<HeadlessBackend, Window<>, NoMonitor>>);
static_assert(std::same_as<Application<HeadlessBackend, Window<>>, Application<HeadlessBackend, Window<>, NoMonitor>>);
static_assert(std::same_as<decltype(ApplicationCore{HeadlessBackend{small_window}, Window<>{}}), ApplicationCore<HeadlessBackend, Window<>>>);
static_assert(std::same_as<decltype(ApplicationCore{HeadlessBackend{small_window}, Window<>{}, test_monitor()}), ApplicationCore<HeadlessBackend, Window<>, TestMonitor>>);

// The core's members as they would be with no monitor at all, in the same order. The compiler cannot be asked about a
// member that is not there, so this is as far as it can be shown: if NoMonitor took any room, these would differ.
template <class B, class Root>
struct CoreWithNoMonitorMember {
    B backend;
    Root root;
    std::unique_ptr<detail::FrameLedger> painting;
    std::optional<Error> failure;
    std::uint64_t frames;
    std::optional<MonotonicTime> wake;
    std::optional<CursorShape> cursor;
    bool have_size;
    bool closed;
    bool damaged;
    bool entered;
};
static_assert(sizeof(ApplicationCore<HeadlessBackend, Window<>>) == sizeof(CoreWithNoMonitorMember<HeadlessBackend, Window<>>));
static_assert(sizeof(ApplicationCore<HeadlessBackend, decltype(demo::make_ui())>) == sizeof(CoreWithNoMonitorMember<HeadlessBackend, decltype(demo::make_ui())>));
static_assert(sizeof(ApplicationCore<HeadlessBackend, Window<>, TestMonitor>) > sizeof(ApplicationCore<HeadlessBackend, Window<>>));

// --- Nothing a callback holds leads to the monitor ---
template <class T>
concept ReachesMonitor = requires(T& held) { held.monitor(); } || requires(T& held) { held.monitor_; } || requires(T& held) { held.core_; };
static_assert(!ReachesMonitor<Window<>>);
static_assert(!ReachesMonitor<ApplicationCore<HeadlessBackend, Window<>, TestMonitor>>);
static_assert(!ReachesMonitor<Application<HeadlessBackend, Window<>, TestMonitor>>);

// --- Compile-time names ---
struct NamedTag;
static_assert(detail::type_name_of<int>() == "int");
static_assert(detail::type_name_of<const int*>().contains("int"));
static_assert(detail::type_name_of<demo::HelpButton>() == "demo::HelpButton");
static_assert(detail::type_name_of<NamedTag>().ends_with("NamedTag"));
static_assert(detail::class_name_of<Label<NamedTag>>().ends_with("Label") && !detail::class_name_of<Label<NamedTag>>().contains('<'));
static_assert(detail::class_name_of<Dynamic<NamedTag, Label<NamedTag>>>().ends_with("Dynamic"));
static_assert(detail::class_name_of<demo::HelpPanel<NamedTag>>() == "demo::HelpPanel");

// --- Numbers and dates as text ---
static_assert(detail::DecimalText{0}.view() == "0" && detail::DecimalText{7}.view() == "7" && detail::DecimalText{-1}.view() == "-1");
static_assert(detail::DecimalText{std::numeric_limits<std::int64_t>::min()}.view() == "-9223372036854775808");
static_assert(detail::DecimalText{std::numeric_limits<std::uint64_t>::max()}.view() == "18446744073709551615");
static_assert(detail::date_text(std::chrono::year{2026} / std::chrono::month{1} / std::chrono::day{9}) == "2026-01-09");
static_assert(detail::date_text(std::chrono::year{2026} / std::chrono::month{11} / std::chrono::day{30}) == "2026-11-30");

// --- The hash ---
consteval bool hash_sees_pixels_and_not_gaps() {
    std::array<std::uint32_t, 6> tight{1, 2, 3, 4, 5, 6};
    std::array<std::uint32_t, 9> gaps{1, 2, 77, 3, 4, 88, 5, 6, 99};
    const Size size{Width{2}, Height{3}};
    const auto a = PixelBuffer::create(tight, size, 8).value().content_hash();
    const auto b = PixelBuffer::create(gaps, size, 12).value().content_hash();
    if (a != b) return false;
    tight[5] = 7;
    if (PixelBuffer::create(tight, size, 8).value().content_hash() == a) return false;
    tight[5] = 6;
    tight[0] = 9;
    return PixelBuffer::create(tight, size, 8).value().content_hash() != a;
}
static_assert(hash_sees_pixels_and_not_gaps());

// --- The writer's rule for formatters ---
template <std::size_t N>
[[nodiscard]] consteval std::optional<std::string> line_of(const std::array<std::string_view, N>& values) {
    std::string out;
    if (!detail::append_line(out, 1, values)) return std::nullopt;
    return out;
}

static_assert(line_of(std::array<std::string_view, 2>{"a", "b"}).value() == " 1. \xE2\x96\xAE\xE2\x96\x8C a\xE2\x96\xAE" "b\n", "the default");
static_assert(line_of(std::array<std::string_view, 2>{"a|b", "c"}).value() == " 1. \xE2\x96\xAE\xE2\x96\x8C a|b\xE2\x96\xAE" "c\n", "a pipe is only data");
static_assert(line_of(std::array<std::string_view, 2>{"a\xE2\x96\xAE", "b"}).value() == " 1. #\xE2\x96\x8C a\xE2\x96\xAE#b\n", "the splice in the data");
static_assert(line_of(std::array<std::string_view, 2>{"a\xE2\x96\x8C", "b"}).value() == " 1. \xE2\x96\xAE# a\xE2\x96\x8C\xE2\x96\xAE" "b\n", "the comment character in the data");
static_assert(line_of(std::array<std::string_view, 2>{"\xE2\x96\xAE\xE2\x96\x8C", "b"}).value() == " 1. ^# \xE2\x96\xAE\xE2\x96\x8C^b\n", "both, which also take different marks");
static_assert(line_of(std::array<std::string_view, 1>{"\xE2\x96\xAE#"}).value() == " 1. ^\xE2\x96\x8C \xE2\x96\xAE#\n", "a mark in the data is passed over");
static_assert(line_of(std::array<std::string_view, 1>{"\xE2\x96\xAE#^"}).value() == " 1. %\xE2\x96\x8C \xE2\x96\xAE#^\n", "and the next");
static_assert(!line_of(std::array<std::string_view, 1>{"\xE2\x96\xAE#^%~!@$&*+=:;?\"'(),./<>\\_`{}|-"}), "no mark left refuses the row");
static_assert(!line_of(std::array<std::string_view, 1>{"a\nb"}), "a newline");
static_assert(!line_of(std::array<std::string_view, 1>{"a\rb"}), "a carriage return");
static_assert(!line_of(std::array<std::string_view, 1>{"a\tb"}), "a tab");
static_assert(!line_of(std::array<std::string_view, 1>{"\x7F"}), "delete");
static_assert(!line_of(std::array<std::string_view, 1>{"\xC2\x85"}), "a control character above ASCII");
static_assert(line_of(std::array<std::string_view, 1>{"\xC2\xA0x"}).has_value(), "the first character past those");
static_assert(!line_of(std::array<std::string_view, 1>{std::string_view{"a\0b", 3}}), "a NUL");
static_assert(!line_of(std::array<std::string_view, 1>{"\xFF"}), "not UTF-8");
static_assert(!line_of(std::array<std::string_view, 1>{" a"}) && !line_of(std::array<std::string_view, 1>{"a "}), "space a reader would trim");
static_assert(line_of(std::array<std::string_view, 2>{"", ""}).value() == " 1. \xE2\x96\xAE\xE2\x96\x8C \xE2\x96\xAE\n", "empty values");

consteval bool line_number_is_two_columns() {
    std::string out;
    const std::array<std::string_view, 1> one{"x"};
    return detail::append_line(out, 9, one) && detail::append_line(out, 10, one) && out.starts_with(" 9. ") && out.contains("\n10. ");
}
static_assert(line_number_is_two_columns());

// --- File names ---
static_assert(detail::plain_file_name_fits("redraw_monitor.jtext") && detail::plain_file_name_fits(".hidden"));
static_assert(!detail::plain_file_name_fits("") && !detail::plain_file_name_fits(".") && !detail::plain_file_name_fits(".."));
static_assert(!detail::plain_file_name_fits("a/b") && !detail::plain_file_name_fits("a\nb") && !detail::plain_file_name_fits("\xFF"));

// --- The newest rows ---
consteval bool ring_keeps_the_newest() {
    NewestRows<int> rows{3};
    for (int i = 1; i <= 2; ++i) rows.push(i);
    if (rows.size() != 2 || rows.dropped() != 0 || rows[0] != 1 || rows[1] != 2) return false;
    for (int i = 3; i <= 7; ++i) rows.push(i);
    if (rows.size() != 3 || rows.seen() != 7 || rows.dropped() != 4) return false;
    if (rows[0] != 5 || rows[1] != 6 || rows[2] != 7) return false;
    NewestRows<int> least{0};
    least.push(1);
    least.push(2);
    return least.size() == 1 && least[0] == 2 && least.dropped() == 1;
}
static_assert(ring_keeps_the_newest());

// --- Each frame's row, with every number known ---
constexpr BlockShaper shaper{};

template <class Core>
[[nodiscard]] constexpr auto& rows_of(Core& core) {
    return jgui_test::monitor(core);
}

consteval bool first_frame_has_every_number() {
    ApplicationCore core{HeadlessBackend{small_window}, demo::make_ui<std::vector>(), test_monitor()};
    // Before the frame, layout, acquire, paint, hash, present.
    rows_of(core).clock().queue({5, 10, 20, 30, 40, 50});
    if (!core.process_pending(shaper).has_value() || core.frames() != 1) return false;
    const auto& frames = rows_of(core).frames();
    if (frames.size() != 1) return false;
    const FrameRow& row = frames[0];
    return row.frame_id == 1 && row.start_ns == 5 && row.reason == RedrawReason::FirstFrame && row.area == small_whole &&
           row.layout_ns == 10 && row.acquire_ns == 20 && row.paint_ns == 30 && row.hash_ns == 40 && row.present_ns == 50 &&
           row.total_ns == 110 && row.last_write_ns == 0 && !row.slow && !row.same_pixels;
}
static_assert(first_frame_has_every_number());

consteval bool slow_is_over_the_budget_and_not_at_it() {
    ApplicationCore core{HeadlessBackend{small_window}, demo::make_ui<std::vector>(), test_monitor()};
    constexpr std::int64_t budget = 16'000'000;
    rows_of(core).clock().queue({0, budget, 0, 0, 999'999'999, 0});
    if (!core.process_pending(shaper).has_value() || rows_of(core).frames()[0].slow) return false;
    // A reading for the check on writing, then a second frame.
    jgui_test::backend(core).inject(ResizeEvent{other_window});
    rows_of(core).clock().queue({0, 0, budget + 1, 0, 0, 0, 0});
    if (!core.process_pending(shaper).has_value() || rows_of(core).frames().size() != 2) return false;
    return rows_of(core).frames()[1].slow;
}
static_assert(slow_is_over_the_budget_and_not_at_it());

consteval bool budget_is_what_was_given() {
    ApplicationCore core{HeadlessBackend{small_window}, demo::make_ui<std::vector>(), test_monitor(FrameMonitorOptions{.slow_budget_ns = 9})};
    rows_of(core).clock().queue({0, 10, 0, 0, 0, 0});
    return core.process_pending(shaper).has_value() && rows_of(core).frames()[0].slow;
}
static_assert(budget_is_what_was_given());

// Each reason, each with its own flag, and a redraw that changes nothing.
consteval bool reasons_and_wasted_redraws() {
    ApplicationCore core{HeadlessBackend{small_window}, demo::make_ui<std::vector>(), test_monitor()};
    if (!core.process_pending(shaper).has_value()) return false;
    const auto& frames = rows_of(core).frames();
    if (frames[0].reason != RedrawReason::FirstFrame || frames[0].same_pixels) return false;

    // The same size again: a redraw, because a resize always damages, with nothing different in it. The wasted one.
    jgui_test::backend(core).inject(ResizeEvent{small_window});
    if (!core.process_pending(shaper).has_value() || frames.size() != 2) return false;
    if (frames[1].reason != RedrawReason::Resize || !frames[1].same_pixels) return false;

    // A new size is never the same, even if it were to hash alike.
    jgui_test::backend(core).inject(ResizeEvent{other_window});
    if (!core.process_pending(shaper).has_value() || frames.size() != 3) return false;
    if (frames[2].reason != RedrawReason::Resize || frames[2].same_pixels || frames[2].area.size() != other_window) return false;

    // A widget's damage, with pixels that really change.
    const Point on_help = jgui_test::inside(core.ui().get<demo::HelpButton>().bounds(), 1, 1);
    jgui_test::backend(core).inject(PointerButton{on_help, MouseButton::Left, ButtonState::Pressed, jgui_test::serial(1)});
    if (!core.process_pending(shaper).has_value() || frames.size() != 4) return false;
    if (frames[3].reason != RedrawReason::WidgetDamage || frames[3].same_pixels) return false;

    // Back to how it looked two frames ago is still not the same as the frame before.
    jgui_test::backend(core).inject(PointerButton{on_help, MouseButton::Left, ButtonState::Released, jgui_test::serial(2)});
    if (!core.process_pending(shaper).has_value() || frames.size() != 5 || frames[4].same_pixels) return false;
    return core.frames() == 5;
}
static_assert(reasons_and_wasted_redraws());

// Two sizes with the same number of pixels, all alike, hash the same. They are still different pictures.
consteval bool same_hash_at_another_size_is_not_the_same_picture() {
    TestMonitor monitor = test_monitor();
    std::array<std::uint32_t, 6> pixels{};
    pixels.fill(0xFF112233U);
    const Size wide{Width{3}, Height{2}};
    const Size tall{Width{2}, Height{3}};
    const auto wide_buffer = PixelBuffer::create(pixels, wide, 12);
    const auto tall_buffer = PixelBuffer::create(pixels, tall, 8);
    if (wide_buffer->content_hash() != tall_buffer->content_hash()) return false;
    const auto frame = [&](const PixelBuffer& buffer) {
        monitor.frame_begins(RedrawReason::Resize, Rect{Point{}, buffer.size()});
        monitor.phases_begin();
        monitor.painted(buffer);
        monitor.presented();
    };
    frame(*wide_buffer);
    frame(*tall_buffer);
    frame(*tall_buffer);
    const auto& frames = monitor.frames();
    return !frames[0].same_pixels && !frames[1].same_pixels && frames[2].same_pixels;
}
static_assert(same_hash_at_another_size_is_not_the_same_picture());

// A label given the text it already has: widget damage, and nothing on screen changes.
consteval bool wasted_redraw_from_a_widget() {
    struct Line;
    ApplicationCore core{HeadlessBackend{small_window}, window(label<Line>("same")), test_monitor()};
    if (!core.process_pending(shaper).has_value()) return false;
    if (!core.ui().get<Line>().set_text("same").has_value()) return false;
    if (!core.process_pending(shaper).has_value()) return false;
    const auto& frames = rows_of(core).frames();
    return frames.size() == 2 && frames[1].reason == RedrawReason::WidgetDamage && frames[1].same_pixels;
}
static_assert(wasted_redraw_from_a_widget());

// --- What caused the damage: the demo's tree is frame 1, bar 2, Help 3, panel 4 ---
consteval bool damage_names_each_widget_at_its_place() {
    ApplicationCore core{HeadlessBackend{small_window}, demo::make_ui<std::vector>(), test_monitor()};
    if (!core.process_pending(shaper).has_value()) return false;
    const auto& damage = rows_of(core).damage();
    if (damage.size() != 0) return false;  // a first frame has no widget damage

    const Point on_help = jgui_test::inside(core.ui().get<demo::HelpButton>().bounds(), 1, 1);
    jgui_test::backend(core).inject(PointerButton{on_help, MouseButton::Left, ButtonState::Pressed, jgui_test::serial(1)});
    if (!core.process_pending(shaper).has_value() || damage.size() != 1) return false;
    const DamageRow& button_row = damage[0];
    if (button_row.record_id != 1 || button_row.frame_id != 2 || button_row.widget.position != 3) return false;
    if (!button_row.widget.class_name.ends_with("Button") || button_row.widget.tag_name != "demo::HelpButton") return false;

    // The release fires the click, so both the button and the panel it toggled are damaged, in tree order.
    jgui_test::backend(core).inject(PointerButton{on_help, MouseButton::Left, ButtonState::Released, jgui_test::serial(2)});
    if (!core.process_pending(shaper).has_value() || damage.size() != 3) return false;
    if (damage[1].record_id != 2 || damage[1].frame_id != 3 || damage[1].widget.position != 3) return false;
    if (damage[2].record_id != 3 || damage[2].frame_id != 3 || damage[2].widget.position != 4) return false;
    if (damage[2].widget.class_name != "demo::HelpPanel" || damage[2].widget.tag_name != "demo::HelpLine") return false;

    // Nothing damaged, no frame and no rows.
    if (!core.process_pending(shaper).has_value() || damage.size() != 3 || core.frames() != 3) return false;

    // The frame's own damage, and a widget's in the same frame: the frame is first, at position 1, and the bar, which holds no
    // damage of its own, is not named although a child of it is damaged.
    const Point on_close = jgui_test::inside(WindowFrame<DefaultFrameMetrics>::close_box(small_window), 1, 1);
    jgui_test::backend(core).inject(PointerMove{on_close});
    jgui_test::backend(core).inject(PointerButton{on_help, MouseButton::Left, ButtonState::Pressed, jgui_test::serial(3)});
    if (!core.process_pending(shaper).has_value() || damage.size() != 5) return false;
    return damage[3].widget.position == 1 && damage[3].widget.class_name.ends_with("WindowFrame") && damage[3].widget.tag_name.ends_with("WindowFrameTag") &&
           damage[4].widget.position == 3;
}
static_assert(damage_names_each_widget_at_its_place());

// A composite with no way to say it alone: its children's damage hides its own, so it is named only when none is damaged.
template <class Tag>
struct Group {
    using tag = Tag;
    using children_type = std::tuple<Label<struct GroupChild>>;
    children_type held{Label<GroupChild>{"x"}};
    bool own{};

    [[nodiscard]] constexpr children_type& children() noexcept { return held; }
    [[nodiscard]] constexpr Rect bounds() const noexcept { return Rect{}; }
    constexpr void paint(const TreeKey&, Canvas&) {}
    template <class Shaper>
    constexpr std::expected<Rect, Error> layout(const TreeKey& key, Rect area, const Shaper& text) {
        return detail::layout_in_order(held, key, area, text);
    }
    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const QueryDamage& event) {
        return detail::broadcast(held, key, ui, event) || own;
    }
    template <class U, class E>
    constexpr bool handle(const TreeKey&, U&, const E&) {
        return false;
    }
    template <class U>
    constexpr void notify(const TreeKey& key, U& ui, const ClearDamage& event) {
        own = false;
        detail::notify_all(held, key, ui, event);
    }
    template <class U, class E>
    constexpr void notify(const TreeKey&, U&, const E&) {}
};

consteval bool composite_that_cannot_say_alone() {
    struct GroupTag;
    ApplicationCore core{HeadlessBackend{small_window}, window(Group<GroupTag>{}), test_monitor()};
    if (!core.process_pending(shaper).has_value()) return false;
    const auto& damage = rows_of(core).damage();
    core.ui().get<GroupTag>().own = true;
    if (!core.process_pending(shaper).has_value() || damage.size() != 1 || damage[0].widget.position != 1) return false;
    if (!core.ui().get<GroupChild>().set_text("y").has_value()) return false;
    core.ui().get<GroupTag>().own = true;
    if (!core.process_pending(shaper).has_value() || damage.size() != 2) return false;
    return damage[1].widget.position == 2;
}
static_assert(composite_that_cannot_say_alone());

// --- Deferred ---
consteval bool deferred_rows_and_then_the_frame() {
    ApplicationCore core{HeadlessBackend{small_window}, demo::make_ui<std::vector>(), test_monitor()};
    if (!core.process_pending(shaper).has_value()) return false;
    const auto& deferred = rows_of(core).deferred_rows();
    jgui_test::backend(core).set_display_holding(true);

    // No damage: nothing was held back.
    if (!core.process_pending(shaper).has_value() || deferred.size() != 0) return false;

    rows_of(core).clock().queue({100, 7, 7});
    jgui_test::backend(core).inject(ResizeEvent{other_window});
    if (!core.process_pending(shaper).has_value() || core.frames() != 1 || deferred.size() != 1) return false;
    const DeferredRow& row = deferred[0];
    if (row.record_id != 1 || row.next_frame_id != 2 || row.reason != RedrawReason::Resize || row.size != other_window) return false;
    if (row.time_ns == 0) return false;
    // Still held back: another row, and still no frame.
    if (!core.process_pending(shaper).has_value() || core.frames() != 1 || deferred.size() != 2 || deferred[1].record_id != 2) return false;

    jgui_test::backend(core).set_display_holding(false);
    if (!core.process_pending(shaper).has_value() || core.frames() != 2 || deferred.size() != 2) return false;
    return rows_of(core).frames()[1].frame_id == 2 && rows_of(core).frames()[1].reason == RedrawReason::Resize;
}
static_assert(deferred_rows_and_then_the_frame());

// --- The bound ---
consteval bool ring_limit_is_counted_in_the_file() {
    ApplicationCore core{HeadlessBackend{small_window}, demo::make_ui<std::vector>(), test_monitor(FrameMonitorOptions{.row_limit = 3})};
    if (!core.process_pending(shaper).has_value()) return false;
    for (int i = 0; i < 4; ++i) {
        jgui_test::backend(core).inject(ResizeEvent{small_window});
        if (!core.process_pending(shaper).has_value()) return false;
    }
    const auto& frames = rows_of(core).frames();
    if (frames.size() != 3 || frames.seen() != 5 || frames.dropped() != 2 || frames[0].frame_id != 3 || frames[2].frame_id != 5) return false;
    const std::string text = rows_of(core).file_text();
    const auto rows = jgui_test::rows_of_section(text, "Frames");
    if (rows.size() != 3 || rows[0][0] != "3" || rows[2][0] != "5") return false;
    const auto run = jgui_test::rows_of_section(text, "Run");
    return run.size() == 1 && run[0][3] == "3" && run[0][4] == "5" && run[0][5] == "2" && jgui_test::blocks_all_close(text);
}
static_assert(ring_limit_is_counted_in_the_file());

// --- Writing: only when due, once at the end, and never again after a failure ---
consteval bool writes_at_most_once_an_interval_and_at_the_end() {
    ApplicationCore core{HeadlessBackend{small_window}, demo::make_ui<std::vector>(), test_monitor(FrameMonitorOptions{.write_interval_ns = 1000})};
    auto& monitor = rows_of(core);
    if (!core.process_pending(shaper).has_value() || monitor.sink().saves != 0) return false;  // no time has passed
    monitor.clock().queue({999});
    jgui_test::backend(core).inject(ResizeEvent{small_window});
    if (!core.process_pending(shaper).has_value() || monitor.sink().saves != 0) return false;  // 999 of the 1000
    monitor.clock().queue({1});
    jgui_test::backend(core).inject(ResizeEvent{small_window});
    if (!core.process_pending(shaper).has_value() || monitor.sink().saves != 1) return false;  // now it is due
    if (!monitor.sink().saved.contains("=== End File ===")) return false;
    if (!core.process_pending(shaper).has_value() || monitor.sink().saves != 1) return false;  // nothing new since
    monitor.clock().queue({5000});
    if (!core.process_pending(shaper).has_value() || monitor.sink().saves != 1) return false;  // nothing new, however long
    core.finish_monitor();
    return monitor.sink().saves == 2 && jgui_test::rows_of_section(monitor.sink().saved, "Run")[0][13] == "Yes";
}
static_assert(writes_at_most_once_an_interval_and_at_the_end());

consteval bool write_time_shows_in_the_next_frame_and_outside_the_phases() {
    ApplicationCore core{HeadlessBackend{small_window}, demo::make_ui<std::vector>(), test_monitor(FrameMonitorOptions{.write_interval_ns = 1})};
    auto& monitor = rows_of(core);
    // The frame's six readings, one to see that a write is due, and the two around the write.
    monitor.clock().queue({10, 1, 1, 1, 1, 1, 0, 0, 345});
    if (!core.process_pending(shaper).has_value() || monitor.sink().saves != 1) return false;
    const auto& frames = monitor.frames();
    if (frames[0].total_ns != 4 || frames[0].last_write_ns != 0) return false;
    jgui_test::backend(core).inject(ResizeEvent{small_window});
    if (!core.process_pending(shaper).has_value()) return false;
    return frames[1].last_write_ns == 345 && frames[1].total_ns == 0;
}
static_assert(write_time_shows_in_the_next_frame_and_outside_the_phases());

consteval bool failed_write_stops_writing_and_nothing_else() {
    ApplicationCore core{HeadlessBackend{small_window}, demo::make_ui<std::vector>(), test_monitor(FrameMonitorOptions{.write_interval_ns = 0})};
    auto& monitor = rows_of(core);
    monitor.sink().fail = true;
    if (core.monitor_error().has_value()) return false;
    if (!core.process_pending(shaper).has_value() || core.frames() != 1) return false;
    if (monitor.sink().attempts != 1 || !core.monitor_error().has_value()) return false;
    monitor.sink().fail = false;
    jgui_test::backend(core).inject(ResizeEvent{small_window});
    if (!core.process_pending(shaper).has_value() || core.frames() != 2 || monitor.frames().size() != 2) return false;
    core.finish_monitor();
    return monitor.sink().attempts == 1 && monitor.sink().saves == 0 && core.monitor_error()->message.contains("told to fail");
}
static_assert(failed_write_stops_writing_and_nothing_else());

consteval bool no_monitor_answers_no_error() {
    ApplicationCore core{HeadlessBackend{small_window}, demo::make_ui<std::vector>()};
    if (!core.process_pending(shaper).has_value()) return false;
    core.finish_monitor();
    return !core.monitor_error().has_value();
}
static_assert(no_monitor_answers_no_error());

// --- The file's text ---
consteval bool file_text_has_all_its_parts() {
    ApplicationCore core{HeadlessBackend{small_window}, demo::make_ui<std::vector>(), test_monitor(FrameMonitorOptions{.label = "a|b\xE2\x96\xAE\xE2\x96\x8C"})};
    rows_of(core).clock().queue({5, 10, 20, 30, 40, 50});
    if (!core.process_pending(shaper).has_value()) return false;
    const std::string text = rows_of(core).file_text();
    if (!text.starts_with("//File:    test.jtext\n//Date:    2026-11-02\n//Purpose: jText Data File\n")) return false;
    if (!text.contains("//\n=== jText File ===\n 1. \xE2\x96\xAE\xE2\x96\x8C test.jtext\n 2. \xE2\x96\xAE\xE2\x96\x8C 2026-11-02\n")) return false;
    if (!jgui_test::blocks_all_close(text)) return false;
    for (const std::string_view section : {"Run", "Frames", "Damage", "Deferred"})
        if (!text.contains("=== Section: " + std::string{section} + " ===\n=== Fields ===\n")) return false;
    const auto frames = jgui_test::rows_of_section(text, "Frames");
    const std::vector<std::string> want{"1", "5", "FirstFrame", "0", "0", "120", "60", "10", "20", "30", "40", "50", "110", "0", "No", "No"};
    if (frames.size() != 1 || frames[0] != want) return false;
    const auto run = jgui_test::rows_of_section(text, "Run");
    // The label reads back as itself, past every character the format uses.
    return run.size() == 1 && run[0].size() == 14 && run[0][1] == "a|b\xE2\x96\xAE\xE2\x96\x8C" && run[0][10] == "0";
}
static_assert(file_text_has_all_its_parts());

consteval bool bad_label_cannot_break_the_file() {
    for (const std::string_view bad : {std::string_view{"two\nlines"}, std::string_view{"=== End File ===\n"}, std::string_view{"\xFF"}, std::string_view{" spaced "}}) {
        const TestMonitor monitor = test_monitor(FrameMonitorOptions{.label = bad});
        const std::string text = monitor.file_text();
        if (!jgui_test::blocks_all_close(text) || text.contains("two")) return false;
        const auto run = jgui_test::rows_of_section(text, "Run");
        if (run.size() != 1 || !run[0][1].empty() || run[0][10] != "1") return false;
    }
    return true;
}
static_assert(bad_label_cannot_break_the_file());

// A class name is compile-time text, and it still goes through the same check on its way out: a row that cannot be
// written is left out and counted, and the file around it stays whole.
consteval bool unwritable_row_is_counted_and_left_out() {
    ApplicationCore core{HeadlessBackend{small_window}, window(window_frame()), test_monitor()};
    auto& monitor = rows_of(core);
    if (!core.process_pending(shaper).has_value()) return false;
    monitor.frame_begins(RedrawReason::WidgetDamage, small_whole);
    monitor.widget_damaged(DamagedWidget{"good", "tag", 1});
    monitor.widget_damaged(DamagedWidget{"bad\nname", "tag", 2});
    monitor.phases_begin();
    monitor.presented();
    const std::string text = monitor.file_text();
    const auto damage = jgui_test::rows_of_section(text, "Damage");
    return damage.size() == 1 && damage[0][2] == "good" && jgui_test::rows_of_section(text, "Run")[0][10] == "1" && jgui_test::blocks_all_close(text);
}
static_assert(unwritable_row_is_counted_and_left_out());

// --- At run time ---

[[nodiscard]] std::string read_whole_file(const std::filesystem::path& path) {
    std::string text;
    const detail::UniqueFd file{::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW)};
    struct stat status {};
    if (file.get() < 0 || ::fstat(file.get(), &status) != 0) return text;
    text.resize(static_cast<std::size_t>(status.st_size));
    if (!detail::read_at(file.get(), std::span{text}, 0)) text.clear();
    return text;
}

using RealTimeMonitor = FrameMonitor<SteadyClock, FileSink>;

[[nodiscard]] RealTimeMonitor monitor_in(const std::string& directory, std::int64_t write_interval_ns = 1'000'000'000) {
    return RealTimeMonitor{SteadyClock{}, FileSink{directory}, FrameMonitorOptions{.write_interval_ns = write_interval_ns}};
}

// Runs the demo's window through a few things on the real font, and leaves it open. Each step's whole picture and the
// count of frames goes in the record, with and without a monitor alike.
struct RunRecord {
    std::vector<std::uint64_t> pictures{};
    std::vector<std::uint64_t> frame_counts{};
    std::vector<std::uint64_t> presented{};
};

template <class App>
void record_step(App& app, RunRecord& record) {
    if (!app.process_pending()) std::abort();
    auto& backend = jgui_test::backend(app);
    const Size size = backend.size();
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (std::int32_t y = 0; y < jgui_test::number(size.height()); ++y) {
        for (std::int32_t x = 0; x < jgui_test::number(size.width()); ++x) {
            hash ^= backend.pixel(Point{Left{x}, Top{y}});
            hash *= 0x100000001B3ULL;
        }
    }
    record.pictures.push_back(hash);
    record.frame_counts.push_back(app.frames());
    record.presented.push_back(backend.presented());
}

template <class App>
[[nodiscard]] RunRecord run_the_script(App& app) {
    RunRecord record;
    auto& backend = jgui_test::backend(app);
    record_step(app, record);
    const Point help = jgui_test::centre(app.ui().template get<demo::HelpButton>().bounds());
    const Point on_close = jgui_test::inside(WindowFrame<DefaultFrameMetrics>::close_box(Size{Width{320}, Height{200}}), 1, 1);
    backend.inject(PointerMove{help});
    record_step(app, record);
    backend.inject(PointerButton{help, MouseButton::Left, ButtonState::Pressed, jgui_test::test_serial});
    record_step(app, record);
    backend.inject(PointerButton{help, MouseButton::Left, ButtonState::Released, jgui_test::test_serial});
    record_step(app, record);
    backend.inject(PointerMove{on_close});
    record_step(app, record);
    backend.set_display_holding(true);
    backend.inject(ResizeEvent{Size{Width{340}, Height{210}}});
    record_step(app, record);
    backend.set_display_holding(false);
    record_step(app, record);
    backend.inject(ResizeEvent{Size{Width{340}, Height{210}}});
    record_step(app, record);
    backend.inject(PointerLeave{});
    record_step(app, record);
    return record;
}

void test_monitor_changes_nothing(jgui_test::Checker& check) {
    const TemporaryDirectory directory;
    const Size size{Width{320}, Height{200}};
    auto plain = make_application(HeadlessBackend{size}, demo::make_ui());
    auto watched = make_application(HeadlessBackend{size}, demo::make_ui(), monitor_in(directory.path(), 0));
    check(plain.has_value() && watched.has_value(), "both start");
    if (!plain || !watched) return;
    const RunRecord without = run_the_script(*plain);
    const RunRecord with = run_the_script(*watched);
    check(without.pictures == with.pictures, "every picture is the same, pixel for pixel, as far as a 64-bit hash of each shows");
    check(without.frame_counts == with.frame_counts && without.presented == with.presented, "and the same number of frames at every step");
    check(without.frame_counts.back() >= 6, "and the script really drew");
    check(jgui_test::monitor(*watched).frames().seen() == with.frame_counts.back(), "the monitor saw each frame");
    check(!watched->monitor_error().has_value(), "with no error");
    check(jgui_test::monitor(*watched).writes() >= 6, "and it wrote after each, with a write interval of zero");
}

void test_real_file(jgui_test::Checker& check) {
    const TemporaryDirectory directory;
    auto app = make_application(HeadlessBackend{Size{Width{320}, Height{200}}}, demo::make_ui(), monitor_in(directory.path()));
    check(app.has_value(), "it starts");
    if (!app) return;
    check(app->process_pending().has_value() && app->frames() == 1, "a frame");
    check(!std::filesystem::exists(directory.file()), "no write yet, within the first second");
    jgui_test::backend(*app).inject(CloseEvent{});
    check(app->run().has_value(), "run ends");
    check(!app->monitor_error().has_value(), "with no monitor error");

    const std::string text = read_whole_file(directory.file());
    check(jgui_test::blocks_all_close(text), "the end of the run wrote a whole file");
    check(jgui_test::rows_of_section(text, "Frames").size() == 1, "with the frame in it");
    check(jgui_test::rows_of_section(text, "Run").at(0).at(13) == "Yes", "and it says the run ended");
    check(text.starts_with("//File:    redraw_monitor.jtext\n"), "named by its name and no path");
    check(!text.contains(directory.path()), "and no path appears in it");
    struct stat status {};
    check(::lstat(directory.file().c_str(), &status) == 0 && S_ISREG(status.st_mode) && (status.st_mode & 0777U) == 0600U, "a regular file, mode 0600");
    check(!std::filesystem::exists(directory.temporary()), "with no temporary file left behind");
}

void test_replaces_an_old_file(jgui_test::Checker& check) {
    const TemporaryDirectory directory;
    FileSink sink{directory.path()};
    check(sink.save("one\n").has_value() && sink.save("two\n").has_value(), "two saves");
    check(read_whole_file(directory.file()) == "two\n", "the second replaced the first, and did not add to it");
    // A leftover of a run that crashed.
    {
        const detail::UniqueFd leftover{::open(directory.temporary().c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, S_IRUSR | S_IWUSR)};
        check(leftover.get() >= 0, "a leftover temporary file");
    }
    check(sink.save("three\n").has_value() && read_whole_file(directory.file()) == "three\n", "does not stop the next save");
    check(!std::filesystem::exists(directory.temporary()), "and it is gone");

    // An old file more open than this one is replaced by one that is not.
    check(::chmod(directory.file().c_str(), 0644) == 0 && sink.save("four\n").has_value(), "a save over a wider file");
    struct stat status {};
    check(::stat(directory.file().c_str(), &status) == 0 && (status.st_mode & 0777U) == 0600U, "leaves mode 0600");
}

void test_directory_is_held(jgui_test::Checker& check) {
    const TemporaryDirectory first;
    const TemporaryDirectory second;
    const std::filesystem::path before = std::filesystem::current_path();
    std::filesystem::current_path(first.path());
    FileSink sink{"."};
    std::filesystem::current_path(second.path());
    check(sink.file_name() == "redraw_monitor.jtext" && sink.save("x\n").has_value(), "the default name, in a directory given by a relative path");
    std::filesystem::current_path(before);
    check(std::filesystem::exists(first.file()), "where that path led when the sink was made");
    check(!std::filesystem::exists(second.file()), "and not where it leads now");
}

// Whatever is wrong with the place, the program goes on and the reason can be read. The monitor stops at the first failure.
void check_program_carries_on(jgui_test::Checker& check, RealTimeMonitor monitor, std::string_view expected, const char* what) {
    auto app = make_application(HeadlessBackend{Size{Width{320}, Height{200}}}, demo::make_ui(), std::move(monitor));
    if (!app) {
        check(false, what);
        return;
    }
    const bool first = app->process_pending().has_value() && app->frames() == 1;
    app->finish_monitor();
    const std::optional<Error> error = app->monitor_error();
    const bool reported = error.has_value() && error->message.contains(expected);
    if (error && !reported) std::cerr << "the error was: " << error->message << '\n';
    jgui_test::backend(*app).inject(ResizeEvent{Size{Width{330}, Height{200}}});
    const bool second = app->process_pending().has_value() && app->frames() == 2;
    jgui_test::backend(*app).inject(CloseEvent{});
    const bool closed = app->run().has_value() && app->closed();
    check(first && reported && second && closed, what);
}

void test_failed_writes(jgui_test::Checker& check) {
    {
        const TemporaryDirectory directory;
        std::filesystem::create_directory(directory.file());
        check_program_carries_on(check, monitor_in(directory.path()), "not a regular file", "a directory at the path");
        check(std::filesystem::is_directory(directory.file()) && !std::filesystem::exists(directory.temporary()), "is left as it was, with nothing beside it");
    }
    {
        const TemporaryDirectory directory;
        const std::filesystem::path target = std::filesystem::path{directory.path()} / "target";
        { const detail::UniqueFd made{::open(target.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, S_IRUSR | S_IWUSR)}; }
        std::filesystem::create_symlink(target, directory.file());
        check_program_carries_on(check, monitor_in(directory.path()), "not a regular file", "a symlink at the path");
        check(std::filesystem::is_symlink(directory.file()) && std::filesystem::file_size(target) == 0, "is left as it was, and what it points at is untouched");
    }
    {
        const TemporaryDirectory directory;
        std::filesystem::create_symlink(std::filesystem::path{directory.path()} / "elsewhere", directory.file());
        check_program_carries_on(check, monitor_in(directory.path()), "not a regular file", "a dangling symlink at the path");
        check(!std::filesystem::exists(std::filesystem::path{directory.path()} / "elsewhere"), "creates nothing where it points");
    }
    {
        const TemporaryDirectory directory;
        const std::filesystem::path target = std::filesystem::path{directory.path()} / "target";
        std::filesystem::create_symlink(target, directory.temporary());
        check_program_carries_on(check, monitor_in(directory.path()), "not a regular file", "a symlink at the temporary name");
        check(!std::filesystem::exists(target) && std::filesystem::is_symlink(directory.temporary()), "is not followed and not removed");
    }
    {
        const TemporaryDirectory directory;
        const std::filesystem::path pipe = directory.file();
        check(::mkfifo(pipe.c_str(), 0600) == 0, "a pipe at the path");
        check_program_carries_on(check, monitor_in(directory.path()), "not a regular file", "is refused, with no wait on it");
    }
    {
        const TemporaryDirectory directory;
        check_program_carries_on(check, monitor_in(directory.path() + "/no/such/directory"), "cannot open the directory", "a directory that does not exist");
        check_program_carries_on(check, RealTimeMonitor{SteadyClock{}, FileSink{directory.path(), "a/b"}}, "plain name", "a file name with a directory in it");
        check_program_carries_on(check, RealTimeMonitor{SteadyClock{}, FileSink{directory.path(), ".."}}, "plain name", "a file name that is the parent");
        check(std::filesystem::is_empty(directory.path()), "and nothing is written for any of them");
    }
    if (::geteuid() != 0) {
        const TemporaryDirectory directory;
        check(::chmod(directory.path().c_str(), 0500) == 0, "a directory that cannot be written");
        check_program_carries_on(check, monitor_in(directory.path()), "cannot create", "refuses the file, with the program going on");
    }
}

// A real failure in the middle of writing: the file size limit, with the signal that comes with it ignored.
[[nodiscard]] bool short_write_is_clean(const std::string& directory) {
    const pid_t child = ::fork();
    if (child == 0) {
        static_cast<void>(std::signal(SIGXFSZ, SIG_IGN));
        const rlimit limit{16, 16};
        if (::setrlimit(RLIMIT_FSIZE, &limit) != 0) ::_exit(3);
        FileSink sink{directory};
        const auto saved = sink.save(std::string(4096, 'x'));
        if (saved.has_value() || !saved.error().message.contains("cannot write")) ::_exit(4);
        ::_exit(0);
    }
    int status = 0;
    return child > 0 && ::waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

void test_write_cut_short(jgui_test::Checker& check) {
    const TemporaryDirectory directory;
    FileSink sink{directory.path()};
    check(sink.save("whole\n").has_value(), "a file already there");
    check(short_write_is_clean(directory.path()), "a write that fails part way is an Error");
    check(read_whole_file(directory.file()) == "whole\n", "the file a reader sees is still the whole old one");
    check(!std::filesystem::exists(directory.temporary()), "and the part written is gone");
}

}  // namespace

int main() {
    jgui_test::Checker check;
    test_monitor_changes_nothing(check);
    test_real_file(check);
    test_replaces_an_old_file(check);
    test_directory_is_held(check);
    test_failed_writes(check);
    test_write_cut_short(check);
    return check.finish();
}
