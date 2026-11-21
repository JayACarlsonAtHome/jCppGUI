// File: include/jCppGUI/v001/settings_recorder.hpp
// Purpose: Writes down what a window holds, in one jText file, so it can be looked up, reported and checked. It is a record: nothing is
//          built from this file, and no program but the viewer reads it. SettingsRecorder fits the same slot as a redraw monitor, so a
//          program pays nothing unless it names it. BothMonitors puts two monitors in that one slot. It is not in jgui.hpp. A program
//          that uses the default FileSink links jgui_monitor. The clock and the sink are template parameters, so all of this
//          runs in static_assert with a test clock and no file.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "canvas.hpp"
#include "error.hpp"
#include "geometry.hpp"
#include "jtext_writer.hpp"
#include "redraw_points.hpp"

namespace jgui::inline v001 {

struct SettingsRecorderOptions {
    std::size_t item_limit{65536};  // live items of Dynamics, and what is inside them. The compile-time tree is always written.
    std::int64_t write_interval_ns{1'000'000'000};
};

// One item, as the file has it. The names are in static storage.
struct ItemRow {
    std::uint64_t id{};
    std::uint64_t parent_id{};
    std::string_view class_name{};
    std::string_view name{};
    Left left{};
    Top top{};
    Bottom bottom{};
    Right right{};
    Height height{};
    Width width{};
    friend constexpr bool operator==(const ItemRow&, const ItemRow&) = default;
};

[[nodiscard]] constexpr ItemRow item_row_of(const ItemFound& item) noexcept {
    const Rect& area = item.bounds;
    return ItemRow{item.id, item.parent_id, item.class_name, item.name, area.left(), area.top(), area.bottom(), area.right(), area.height(), area.width()};
}

// --- The checks on one row. Each says whether it holds, so each can fail alone ---

[[nodiscard]] constexpr bool width_matches_its_edges(const ItemRow& row) noexcept { return row.right - row.left == row.width; }
[[nodiscard]] constexpr bool height_matches_its_edges(const ItemRow& row) noexcept { return row.bottom - row.top == row.height; }
// A Width or a Height cannot be negative, as a type. A size below zero can only show as a far edge before the near one.
[[nodiscard]] constexpr bool right_is_not_before_left(const ItemRow& row) noexcept { return left_after(row.right) >= row.left; }
[[nodiscard]] constexpr bool bottom_is_not_before_top(const ItemRow& row) noexcept { return top_after(row.bottom) >= row.top; }

// Every widget's layout places it inside the rectangle it is offered, and every parent offers rectangles inside its own, which is the
// promise this checks. A rectangle with nothing in it has no pixels to be outside with, and a parent that has run out of room can leave
// one just past its edge, or at the corner for a widget not shown.
[[nodiscard]] constexpr bool lies_inside_its_parent(const ItemRow& row, const ItemRow& parent) noexcept {
    if (row.width == Width{} || row.height == Height{}) return true;
    return row.left >= parent.left && row.top >= parent.top && row.right <= parent.right && row.bottom <= parent.bottom;
}

inline constexpr std::uint64_t root_item_id = 1;

// Where the row with this ID stands, in rows sorted by ID.
[[nodiscard]] constexpr std::optional<std::size_t> position_of_item(std::span<const ItemRow> rows, std::uint64_t id) noexcept {
    const auto found = std::ranges::lower_bound(rows, id, {}, &ItemRow::id);
    if (found == rows.end() || found->id != id) return std::nullopt;
    return static_cast<std::size_t>(found - rows.begin());
}

inline constexpr std::string_view problem_width = "Right - Left is not Width";
inline constexpr std::string_view problem_height = "Bottom - Top is not Height";
inline constexpr std::string_view problem_right = "Right is before Left";
inline constexpr std::string_view problem_bottom = "Bottom is before Top";
inline constexpr std::string_view problem_parent = "The parent is not in the file";
inline constexpr std::string_view problem_outside = "Outside its parent";
inline constexpr std::string_view problem_not_written = "This row could not be written";

// One thing found wrong. record_id counts from 1, item_id is the item's ID, and 0 means no one item.
struct ProblemRow {
    std::uint64_t record_id{};
    std::uint64_t item_id{};
    std::string what{};
    friend constexpr bool operator==(const ProblemRow&, const ProblemRow&) = default;
};

// Problems as they are found, up to a limit, and a last row saying how many more there were.
class ProblemList {
public:
    constexpr explicit ProblemList(std::size_t limit) : limit_{std::max<std::size_t>(limit, 1)} {}

    constexpr void add(std::uint64_t item_id, std::string_view what) {
        if (rows_.size() < limit_) {
            rows_.push_back(ProblemRow{rows_.size() + 1, item_id, std::string{what}});
        } else {
            ++not_listed_;
        }
    }

    [[nodiscard]] constexpr std::vector<ProblemRow> take_rows() {
        if (not_listed_ != 0) rows_.push_back(ProblemRow{rows_.size() + 1, 0, std::string{detail::DecimalText{not_listed_}.view()} + " more problems were not listed"});
        return std::move(rows_);
    }

private:
    std::vector<ProblemRow> rows_{};
    std::size_t limit_;
    std::uint64_t not_listed_{};
};

// Every check, on every row of rows sorted by ID.
constexpr void find_item_problems(std::span<const ItemRow> rows, ProblemList& problems) {
    for (const ItemRow& row : rows) {
        if (!width_matches_its_edges(row)) problems.add(row.id, problem_width);
        if (!height_matches_its_edges(row)) problems.add(row.id, problem_height);
        if (!right_is_not_before_left(row)) problems.add(row.id, problem_right);
        if (!bottom_is_not_before_top(row)) problems.add(row.id, problem_bottom);
        if (row.parent_id == 0 && row.id == root_item_id) continue;
        const std::optional<std::size_t> parent = position_of_item(rows, row.parent_id);
        if (!parent) {
            problems.add(row.id, problem_parent);
        } else if (!lies_inside_its_parent(row, rows[*parent])) {
            problems.add(row.id, problem_outside);
        }
    }
}

// Numbers for what has no place in the compile-time tree, above every number that tree uses. An item keeps its number for as long as it
// lives and each walk sees it. A number is never used again in a run, and a slot used again is a new item with a new number. An item that
// comes and goes between two walks takes no number. At most limit items hold a number at a time, and the rest are counted.
class ItemNumbers {
public:
    constexpr explicit ItemNumbers(std::size_t limit) : limit_{limit} {}

    constexpr void begin_walk(std::uint64_t fixed_items) {
        next_ = std::max(next_, fixed_items + 1);
        seen_.clear();
        refused_ = 0;
    }

    [[nodiscard]] constexpr std::uint64_t id_for(const ItemKey& key) {
        if (seen_.size() >= limit_) {
            ++refused_;
            return 0;
        }
        const auto known = std::ranges::lower_bound(known_, key, {}, &Entry::key);
        const std::uint64_t id = known != known_.end() && known->key == key ? known->id : next_++;
        seen_.push_back(Entry{key, id});
        return id;
    }

    constexpr void end_walk() {
        std::ranges::sort(seen_, {}, &Entry::key);
        known_.swap(seen_);
    }

    [[nodiscard]] constexpr std::uint64_t refused() const noexcept { return refused_; }

private:
    struct Entry {
        ItemKey key{};
        std::uint64_t id{};
    };

    std::vector<Entry> known_{};
    std::vector<Entry> seen_{};
    std::size_t limit_;
    std::uint64_t next_{1};
    std::uint64_t refused_{};
};

namespace detail {

inline constexpr std::array<FieldDeclaration, 10> item_fields{{
    {"ID", "Integer"}, {"Parent_ID", "Integer"}, {"Class_Name", "String"}, {"Name", "String"}, {"Left", "Integer"},
    {"Top", "Integer"}, {"Bottom", "Integer"}, {"Right", "Integer"}, {"Height", "Integer"}, {"Width", "Integer"},
}};
inline constexpr std::array<FieldDeclaration, 3> problem_fields{{{"Record_ID", "Integer"}, {"Item_ID", "Integer"}, {"What_Failed", "String"}}};

// False, with nothing appended, when the row cannot stand in a line.
[[nodiscard]] constexpr bool append_item_line(std::string& out, const ItemRow& row) {
    const DecimalText id{row.id};
    const DecimalText parent{row.parent_id};
    const DecimalText left{FileNumbers::number(row.left)};
    const DecimalText top{FileNumbers::number(row.top)};
    const DecimalText bottom{FileNumbers::number(row.bottom)};
    const DecimalText right{FileNumbers::number(row.right)};
    const DecimalText height{FileNumbers::number(row.height)};
    const DecimalText width{FileNumbers::number(row.width)};
    const std::array<std::string_view, item_fields.size()> values{id.view(), parent.view(), row.class_name, row.name, left.view(), top.view(), bottom.view(), right.view(), height.view(), width.view()};
    return append_line(out, 1, values);
}

// What a problem says is this file's own text and numbers, so it always stands in a line.
constexpr void append_problem_line(std::string& out, const ProblemRow& row) {
    const DecimalText record{row.record_id};
    const DecimalText item{row.item_id};
    const std::array<std::string_view, problem_fields.size()> values{record.view(), item.view(), row.what};
    require(append_line(out, 1, values), "a settings problem row cannot be written");
}

}  // namespace detail

template <MonitorClock Clock = SteadyClock, MonitorSink Sink = FileSink>
class SettingsRecorder {
public:
    static constexpr bool watches = false;

    constexpr SettingsRecorder(Clock clock, Sink sink, SettingsRecorderOptions options = SettingsRecorderOptions{})
        : clock_{std::move(clock)},
          sink_{std::move(sink)},
          date_{detail::date_text(clock_.today())},
          item_limit_{options.item_limit},
          write_interval_ns_{options.write_interval_ns},
          numbers_{options.item_limit} {}

    // --- The points of a frame, which this has no use for ---

    constexpr void frame_begins(RedrawReason, const Rect&) const noexcept {}
    constexpr void widget_damaged(const DamagedWidget&) const noexcept {}
    constexpr void phases_begin() const noexcept {}
    constexpr void laid_out() const noexcept {}
    constexpr void acquired() const noexcept {}
    constexpr void painted(const PixelBuffer&) const noexcept {}
    constexpr void presented() const noexcept {}
    constexpr void deferred(RedrawReason, Size) const noexcept {}

    // --- The walk of the tree, after each frame ---

    constexpr void items_begin(std::uint64_t fixed_items) {
        walked_.clear();
        numbers_.begin_walk(fixed_items);
    }

    [[nodiscard]] constexpr std::uint64_t item_id(const ItemKey& key) { return numbers_.id_for(key); }

    constexpr void item_found(const ItemFound& item) { walked_.push_back(item_row_of(item)); }

    // Only when something recorded is different is there anything new to write.
    constexpr void items_end() {
        numbers_.end_walk();
        std::ranges::sort(walked_, {}, &ItemRow::id);
        if (walked_ == rows_ && numbers_.refused() == over_limit_) return;
        rows_.swap(walked_);
        over_limit_ = numbers_.refused();
        make_sections();
        unsaved_ = true;
    }

    // --- Writing, which is between frames and at the end ---

    // The first write is at once, so the file is there from the first frame. After that, at most once an interval.
    constexpr void between_frames() {
        if (!unsaved_ || failure_) return;
        if (writes_ == 0 || clock_.now_ns() - last_write_end_ns_ >= write_interval_ns_) write_file();
    }

    constexpr void finish() {
        if (!failure_) write_file();
    }

    // Why writing stopped. It stops at the first failure and the program goes on.
    [[nodiscard]] constexpr std::optional<Error> error() const { return failure_; }

    // --- What was recorded ---

    [[nodiscard]] constexpr const std::vector<ItemRow>& rows() const noexcept { return rows_; }
    [[nodiscard]] constexpr const std::vector<ProblemRow>& problems() const noexcept { return problems_; }
    [[nodiscard]] constexpr std::uint64_t writes() const noexcept { return writes_; }
    [[nodiscard]] constexpr Clock& clock() noexcept { return clock_; }
    [[nodiscard]] constexpr Sink& sink() noexcept { return sink_; }
    [[nodiscard]] constexpr const Sink& sink() const noexcept { return sink_; }

    // The whole file as it would be written now.
    [[nodiscard]] constexpr std::string file_text() const {
        std::string out;
        out.reserve(item_lines_.size() + problem_lines_.size() + 4096);
        detail::append_file_header(out, detail::FileHeader{.file_name = sink_.file_name(), .date = date_, .related_type = "jCppGUI_settings_record", .title = "jCppGUI settings record"});
        out += detail::section_text("Items", detail::item_fields, item_lines_);
        out += detail::section_text("Problems", detail::problem_fields, problem_lines_);
        out += "=== End File ===\n";
        return out;
    }

private:
    // Each section's lines are made once for each new set of rows, so a write costs only the join and the file.
    constexpr void make_sections() {
        ProblemList problems{item_limit_};
        find_item_problems(rows_, problems);
        if (over_limit_ != 0) {
            problems.add(0, std::string{detail::DecimalText{over_limit_}.view()} + " items are over the limit of " + std::string{detail::DecimalText{item_limit_}.view()} + " and not in this file");
        }
        item_lines_.clear();
        for (const ItemRow& row : rows_)
            if (!detail::append_item_line(item_lines_, row)) problems.add(row.id, problem_not_written);
        problems_ = problems.take_rows();
        problem_lines_.clear();
        for (const ProblemRow& row : problems_) detail::append_problem_line(problem_lines_, row);
    }

    constexpr void write_file() {
        auto saved = sink_.save(file_text());
        last_write_end_ns_ = clock_.now_ns();
        ++writes_;
        unsaved_ = false;
        if (!saved) failure_ = std::move(saved.error());
    }

    Clock clock_;
    Sink sink_;
    std::string date_;
    std::size_t item_limit_;
    std::int64_t write_interval_ns_;
    ItemNumbers numbers_;
    std::vector<ItemRow> rows_{};
    std::vector<ItemRow> walked_{};
    std::vector<ProblemRow> problems_{};
    std::string item_lines_{};
    std::string problem_lines_{};
    std::uint64_t over_limit_{};
    std::uint64_t writes_{};
    std::int64_t last_write_end_ns_{};
    std::optional<Error> failure_{};
    bool unsaved_{};
};

template <MonitorClock Clock, MonitorSink Sink>
SettingsRecorder(Clock, Sink) -> SettingsRecorder<Clock, Sink>;
template <MonitorClock Clock, MonitorSink Sink>
SettingsRecorder(Clock, Sink, SettingsRecorderOptions) -> SettingsRecorder<Clock, Sink>;

static_assert(RedrawMonitor<SettingsRecorder<>> && ItemRecorder<SettingsRecorder<>>);

// The recorder a program usually wants: settings.jtext in the application's base directory, with the real clock.
[[nodiscard]] inline SettingsRecorder<> make_settings_recorder(SettingsRecorderOptions options = SettingsRecorderOptions{}) {
    return SettingsRecorder<>{SteadyClock{}, FileSink::in_base_directory(default_settings_file_name), options};
}

// Two monitors in the one slot. Each is told everything, the first one first. Two recorders would each number the same items, so
// at most one may be one.
template <RedrawMonitor A, RedrawMonitor B>
class BothMonitors {
    static_assert(!(ItemRecorder<A> && ItemRecorder<B>), "only one of two monitors can record the tree's items");

public:
    static constexpr bool watches = A::watches || B::watches;

    constexpr BothMonitors(A first, B second) : first_{std::move(first)}, second_{std::move(second)} {}

    constexpr void frame_begins(RedrawReason reason, const Rect& area) {
        first_.frame_begins(reason, area);
        second_.frame_begins(reason, area);
    }
    constexpr void widget_damaged(const DamagedWidget& widget) {
        first_.widget_damaged(widget);
        second_.widget_damaged(widget);
    }
    constexpr void phases_begin() {
        first_.phases_begin();
        second_.phases_begin();
    }
    constexpr void laid_out() {
        first_.laid_out();
        second_.laid_out();
    }
    constexpr void acquired() {
        first_.acquired();
        second_.acquired();
    }
    constexpr void painted(const PixelBuffer& pixels) {
        first_.painted(pixels);
        second_.painted(pixels);
    }
    constexpr void presented() {
        first_.presented();
        second_.presented();
    }
    constexpr void deferred(RedrawReason reason, Size size) {
        first_.deferred(reason, size);
        second_.deferred(reason, size);
    }
    constexpr void between_frames() {
        first_.between_frames();
        second_.between_frames();
    }
    constexpr void finish() {
        first_.finish();
        second_.finish();
    }

    constexpr void items_begin(std::uint64_t fixed_items)
        requires(ItemRecorder<A> || ItemRecorder<B>)
    {
        recorder().items_begin(fixed_items);
    }
    [[nodiscard]] constexpr std::uint64_t item_id(const ItemKey& key)
        requires(ItemRecorder<A> || ItemRecorder<B>)
    {
        return recorder().item_id(key);
    }
    constexpr void item_found(const ItemFound& item)
        requires(ItemRecorder<A> || ItemRecorder<B>)
    {
        recorder().item_found(item);
    }
    constexpr void items_end()
        requires(ItemRecorder<A> || ItemRecorder<B>)
    {
        recorder().items_end();
    }

    // Each one's reason, when both have one, so neither hides the other.
    [[nodiscard]] constexpr std::optional<Error> error() const {
        std::optional<Error> first_error = first_.error();
        std::optional<Error> second_error = second_.error();
        if (first_error && second_error) return Error{first_error->code, first_error->text() + "; " + second_error->text()};
        return first_error ? first_error : second_error;
    }

    [[nodiscard]] constexpr A& first() noexcept { return first_; }
    [[nodiscard]] constexpr B& second() noexcept { return second_; }

private:
    [[nodiscard]] constexpr auto& recorder() noexcept {
        if constexpr (ItemRecorder<A>) {
            return first_;
        } else {
            return second_;
        }
    }

    A first_;
    B second_;
};

template <class A, class B>
BothMonitors(A, B) -> BothMonitors<A, B>;

}  // namespace jgui::inline v001
