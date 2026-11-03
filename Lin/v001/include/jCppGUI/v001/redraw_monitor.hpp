// File: include/jCppGUI/v001/redraw_monitor.hpp
// Purpose: A development tool: records what each frame did and writes it to one jText file. FrameMonitor owns the time
//          and the file, and the application only tells it what happens. It is not in jgui.hpp. A program that
//          uses the default FileSink links jgui_monitor. The clock and the sink are template parameters, so the
//          logic and the file's text run in static_assert with a test clock and no file.
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "canvas.hpp"
#include "error.hpp"
#include "geometry.hpp"
#include "redraw_points.hpp"
#include "text.hpp"

namespace jgui::inline v001 {

// now_ns only ever goes up, and today is the date for the file's header.
template <class C>
concept MonitorClock = requires(C& clock) {
    { clock.now_ns() } -> std::same_as<std::int64_t>;
    { clock.today() } -> std::same_as<std::chrono::year_month_day>;
};

// save replaces what was saved before with the whole of the text, all or nothing, and file_name is the name it goes by.
template <class S>
concept MonitorSink = requires(S& sink, const S& constant, std::string_view text) {
    { sink.save(text) } -> std::same_as<std::expected<void, Error>>;
    { constant.file_name() } -> std::same_as<std::string_view>;
};

struct SteadyClock {
    [[nodiscard]] std::int64_t now_ns() const noexcept {
        const auto since = std::chrono::steady_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(since).count();
    }
    [[nodiscard]] std::chrono::year_month_day today() const noexcept {
        return std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now())};
    }
};

inline constexpr std::string_view default_redraw_file_name = "redraw_monitor.jtext";

namespace detail {
// A file's name and nothing more, so it stays in the directory it was given for, and fit to stand in a jText line.
[[nodiscard]] constexpr bool redraw_file_name_fits(std::string_view name) noexcept {
    if (name.empty() || name == "." || name == ".." || !valid_utf8(name)) return false;
    return std::ranges::none_of(name, [](char c) { return c == '/' || static_cast<std::uint8_t>(c) < 0x20 || c == 0x7F; });
}
}  // namespace detail

// Saves to a file named in a directory, by writing a temporary file beside it and renaming that over it, so a reader
// sees a whole file or the one before. The directory is opened once, here, and held, so the file's place cannot change
// later. Anything wrong, including no base directory to be found, is an Error from save: this never ends the program.
// Defined in src/monitor/.
class FileSink {
public:
    // In the application's base directory: see base_directory.hpp.
    FileSink();
    // In a directory the caller names, such as a test's own.
    explicit FileSink(std::string_view directory, std::string_view file_name = default_redraw_file_name);
    FileSink(const FileSink&) = delete;
    FileSink& operator=(const FileSink&) = delete;
    FileSink(FileSink&& other) noexcept;
    FileSink& operator=(FileSink&&) = delete;
    ~FileSink();

    [[nodiscard]] std::expected<void, Error> save(std::string_view text);
    [[nodiscard]] std::string_view file_name() const noexcept { return name_; }

private:
    void open_directory(const std::string& directory);

    std::string name_;
    int directory_{-1};
    std::optional<Error> refused_{};
};

struct FrameMonitorOptions {
    std::int64_t slow_budget_ns{16'000'000};
    std::size_t row_limit{65536};  // for each kind of row, keeping the newest
    std::int64_t write_interval_ns{1'000'000'000};
    std::string_view label{};  // for the program to say what this run was
};

// One frame. total_ns is the sum of the four phases, so what the monitor adds is not in it: not hash_ns, and no file write.
struct FrameRow {
    std::uint64_t frame_id{};
    std::int64_t start_ns{};
    RedrawReason reason{RedrawReason::FirstFrame};
    Rect area{};
    std::int64_t layout_ns{};
    std::int64_t acquire_ns{};
    std::int64_t paint_ns{};
    std::int64_t hash_ns{};
    std::int64_t present_ns{};
    std::int64_t total_ns{};
    std::int64_t last_write_ns{};
    bool slow{};
    bool same_pixels{};
};

// One widget that needed a redraw for one frame.
struct DamageRow {
    std::uint64_t record_id{};
    std::uint64_t frame_id{};
    DamagedWidget widget{};
};

// A time there was damage and the display could not take a frame. next_frame_id is the frame that was waiting.
struct DeferredRow {
    std::uint64_t record_id{};
    std::uint64_t next_frame_id{};
    std::int64_t time_ns{};
    RedrawReason reason{RedrawReason::FirstFrame};
    Size size{};
};

// The newest rows of one kind, up to a limit, with a count of all there ever were.
template <class Row>
class NewestRows {
public:
    constexpr explicit NewestRows(std::size_t limit) : limit_{std::max<std::size_t>(limit, 1)} {}

    constexpr void push(const Row& row) {
        if (rows_.size() < limit_) {
            rows_.push_back(row);
        } else {
            rows_[oldest_] = row;
            oldest_ = (oldest_ + 1) % limit_;
        }
        ++seen_;
    }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return rows_.size(); }
    [[nodiscard]] constexpr std::uint64_t seen() const noexcept { return seen_; }
    [[nodiscard]] constexpr std::uint64_t dropped() const noexcept { return seen_ - rows_.size(); }
    // Counted from the oldest kept.
    [[nodiscard]] constexpr const Row& operator[](std::size_t index) const { return rows_[(oldest_ + index) % rows_.size()]; }

private:
    std::vector<Row> rows_{};
    std::size_t limit_;
    std::size_t oldest_{};
    std::uint64_t seen_{};
};

namespace detail {

// The one reader of the number in a geometry value for this file: only to write it.
struct RedrawNumbers {
    template <class Tag>
    [[nodiscard]] static constexpr std::int32_t number(Edge<Tag> value) noexcept {
        return value.value_;
    }
    template <class Tag>
    [[nodiscard]] static constexpr std::int32_t number(Extent<Tag> value) noexcept {
        return value.value_;
    }
};

[[nodiscard]] constexpr std::string_view reason_text(RedrawReason reason) noexcept {
    switch (reason) {
        case RedrawReason::FirstFrame: return "FirstFrame";
        case RedrawReason::Resize: return "Resize";
        case RedrawReason::WidgetDamage: return "WidgetDamage";
        default: return "Unknown";
    }
}

[[nodiscard]] constexpr std::string_view yes_no(bool value) noexcept { return value ? "Yes" : "No"; }

// A number as text, held right here, so a row needs no allocation for each.
class DecimalText {
public:
    template <std::integral I>
    constexpr explicit DecimalText(I value) {
        const bool negative = std::cmp_less(value, 0);
        // Negated as unsigned, so the lowest value of a signed type is safe.
        auto left = static_cast<std::uint64_t>(value);
        if (negative) left = std::uint64_t{0} - left;
        do {
            digits_[--start_] = static_cast<char>('0' + left % 10U);
            left /= 10U;
        } while (left != 0);
        if (negative) digits_[--start_] = '-';
    }
    [[nodiscard]] constexpr std::string_view view() const noexcept { return std::string_view{digits_}.substr(start_); }

private:
    std::array<char, 21> digits_{};
    std::size_t start_{digits_.size()};
};

inline constexpr std::string_view default_splice = "\xE2\x96\xAE";   // U+25AE
inline constexpr std::string_view default_comment = "\xE2\x96\x8C";  // U+258C
// What the spec's reference writer falls back on, in its order. '[' is not here: as a splice it is reserved.
inline constexpr std::string_view fallback_marks = "#^%~!@$&*+=:;?\"'(),./<>\\_`{}|-";

// A line's formatter, which is the two characters that say how its data is split.
struct LineFormatter {
    std::string_view splice;
    std::string_view comment;
};

// Whether one value can stand in a line and read back as itself: UTF-8, no control character of any kind, so no line
// break, and no space around it, which a reader trims. The format has no escapes, so anything else is refused.
[[nodiscard]] constexpr bool value_fits_a_line(std::string_view value) noexcept {
    if (!valid_utf8(value)) return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto byte = static_cast<std::uint8_t>(value[i]);
        if (byte < 0x20 || byte == 0x7F) return false;
        if (byte == 0xC2 && i + 1 < value.size() && static_cast<std::uint8_t>(value[i + 1]) < 0xA0) return false;
    }
    return value.empty() || (value.front() != ' ' && value.back() != ' ');
}

[[nodiscard]] constexpr bool any_value_holds(std::span<const std::string_view> values, std::string_view text) noexcept {
    return std::ranges::any_of(values, [&](std::string_view value) { return value.find(text) != std::string_view::npos; });
}

// The next mark not in the data and not already taken. Empty when every one of them is in the data.
[[nodiscard]] constexpr std::string_view fallback_mark(std::span<const std::string_view> values, std::string_view taken) noexcept {
    for (std::size_t i = 0; i < fallback_marks.size(); ++i) {
        const std::string_view mark = fallback_marks.substr(i, 1);
        if (mark != taken && !any_value_holds(values, mark)) return mark;
    }
    return {};
}

// The spec's writer rule: the default, unless the line's data holds that character. Empty when no mark is left.
[[nodiscard]] constexpr std::optional<LineFormatter> choose_formatter(std::span<const std::string_view> values) noexcept {
    LineFormatter chosen{default_splice, default_comment};
    if (any_value_holds(values, default_comment)) chosen.comment = fallback_mark(values, {});
    if (any_value_holds(values, default_splice)) chosen.splice = fallback_mark(values, chosen.comment);
    if (chosen.splice.empty() || chosen.comment.empty()) return std::nullopt;
    return chosen;
}

// Appends one line of values, starting at that field position. False, with nothing appended, when the line cannot be
// written so that it reads back as these values.
[[nodiscard]] constexpr bool append_line(std::string& out, std::size_t position, std::span<const std::string_view> values) {
    if (!std::ranges::all_of(values, value_fits_a_line)) return false;
    const std::optional<LineFormatter> formatter = choose_formatter(values);
    if (!formatter) return false;
    if (position < 10) out += ' ';
    out += DecimalText{position}.view();
    out += ". ";
    out += formatter->splice;
    out += formatter->comment;
    out += ' ';
    bool first = true;
    for (const std::string_view value : values) {
        if (!first) out += formatter->splice;
        first = false;
        out += value;
    }
    out += '\n';
    return true;
}

// One field of a table, as the Fields block declares it.
struct FieldDeclaration {
    std::string_view name;
    std::string_view type;
};

// A whole section: its banner, its fields and its rows, with every block closed. Field 1 is never empty or null.
[[nodiscard]] constexpr std::string section_text(std::string_view name, std::span<const FieldDeclaration> fields, std::string_view rows) {
    std::string out;
    out.reserve(rows.size() + 1024);
    out += "=== Section: ";
    out += name;
    out += " ===\n=== Fields ===\n";
    std::size_t position = 0;
    for (const FieldDeclaration& field : fields) {
        ++position;
        const std::array<std::string_view, 2> plain{field.name, field.type};
        const std::array<std::string_view, 4> required{field.name, field.type, "Not Null", "Not Empty"};
        const bool written = position == 1 ? append_line(out, position, required) : append_line(out, position, plain);
        require(written, "a redraw monitor field declaration cannot be written");
    }
    out += "=== End Fields ===\n=== Data ===\n";
    out += rows;
    out += "=== End Data ===\n=== End Section ===\n";
    return out;
}

inline constexpr std::array<FieldDeclaration, 14> run_fields{{
    {"Run_ID", "Integer"}, {"Label", "String"}, {"Slow_Budget_ns", "Integer"}, {"Row_Limit", "Integer"},
    {"Frames_Seen", "Integer"}, {"Frames_Dropped", "Integer"}, {"Damage_Seen", "Integer"}, {"Damage_Dropped", "Integer"},
    {"Deferred_Seen", "Integer"}, {"Deferred_Dropped", "Integer"}, {"Rows_Refused", "Integer"}, {"Writes_Before_This", "Integer"},
    {"Previous_Write_ns", "Integer"}, {"Ended", "Bool"},
}};
inline constexpr std::array<FieldDeclaration, 16> frame_fields{{
    {"Frame_ID", "Integer"}, {"Start_ns", "Integer"}, {"Reason", "String"}, {"Left", "Integer"}, {"Top", "Integer"},
    {"Width", "Integer"}, {"Height", "Integer"}, {"Layout_ns", "Integer"}, {"Acquire_ns", "Integer"}, {"Paint_ns", "Integer"},
    {"Hash_ns", "Integer"}, {"Present_ns", "Integer"}, {"Total_ns", "Integer"}, {"Last_Write_ns", "Integer"}, {"Slow", "Bool"},
    {"SamePixels", "Bool"},
}};
inline constexpr std::array<FieldDeclaration, 5> damage_fields{{
    {"Record_ID", "Integer"}, {"Frame_ID", "Integer"}, {"Class_Name", "String"}, {"Tag", "String"}, {"Tree_Position", "Integer"},
}};
inline constexpr std::array<FieldDeclaration, 6> deferred_fields{{
    {"Record_ID", "Integer"}, {"Next_Frame_ID", "Integer"}, {"Time_ns", "Integer"}, {"Reason", "String"}, {"Width", "Integer"}, {"Height", "Integer"},
}};

[[nodiscard]] constexpr std::string date_text(std::chrono::year_month_day date) {
    const auto two_digits = [](unsigned value, std::string& out) {
        if (value < 10) out += '0';
        out += DecimalText{value}.view();
    };
    std::string out{DecimalText{static_cast<int>(date.year())}.view()};
    out += '-';
    two_digits(static_cast<unsigned>(date.month()), out);
    out += '-';
    two_digits(static_cast<unsigned>(date.day()), out);
    return out;
}

}  // namespace detail

template <MonitorClock Clock = SteadyClock, MonitorSink Sink = FileSink>
class FrameMonitor {
public:
    static constexpr bool watches = true;

    constexpr explicit FrameMonitor(Clock clock = Clock{}, Sink sink = Sink{}, FrameMonitorOptions options = FrameMonitorOptions{})
        : clock_{std::move(clock)},
          sink_{std::move(sink)},
          date_{detail::date_text(clock_.today())},
          slow_budget_ns_{options.slow_budget_ns},
          row_limit_{std::max<std::size_t>(options.row_limit, 1)},
          write_interval_ns_{options.write_interval_ns},
          frames_{row_limit_},
          damage_{row_limit_},
          deferred_{row_limit_},
          origin_ns_{clock_.now_ns()},
          last_write_end_ns_{origin_ns_} {
        if (detail::value_fits_a_line(options.label)) {
            label_ = options.label;
        } else {
            ++rows_refused_at_start_;
        }
    }

    // --- The points of a frame ---

    constexpr void frame_begins(RedrawReason reason, const Rect& area) {
        pending_ = FrameRow{.frame_id = frames_.seen() + 1, .reason = reason, .area = area};
        pending_damage_.clear();
    }

    constexpr void widget_damaged(const DamagedWidget& widget) { pending_damage_.push_back(widget); }

    constexpr void phases_begin() {
        lap_start_ns_ = clock_.now_ns();
        pending_.start_ns = lap_start_ns_ - origin_ns_;
    }

    constexpr void laid_out() { pending_.layout_ns = lap(); }
    constexpr void acquired() { pending_.acquire_ns = lap(); }

    // Hashes inside this call, keeps only the number, and times it apart from the painting.
    constexpr void painted(const PixelBuffer& pixels) {
        pending_.paint_ns = lap();
        pending_hash_ = pixels.content_hash();
        pending_.hash_ns = lap();
    }

    constexpr void presented() {
        pending_.present_ns = lap();
        pending_.total_ns = pending_.layout_ns + pending_.acquire_ns + pending_.paint_ns + pending_.present_ns;
        pending_.last_write_ns = last_write_ns_;
        pending_.slow = pending_.total_ns > slow_budget_ns_;
        pending_.same_pixels = previous_hash_ == pending_hash_ && previous_size_ == pending_.area.size();
        previous_hash_ = pending_hash_;
        previous_size_ = pending_.area.size();
        frames_.push(pending_);
        for (const DamagedWidget& widget : pending_damage_) damage_.push(DamageRow{damage_.seen() + 1, pending_.frame_id, widget});
        unsaved_ = true;
        ended_ = false;
    }

    constexpr void deferred(RedrawReason reason, Size size) {
        deferred_.push(DeferredRow{deferred_.seen() + 1, frames_.seen() + 1, clock_.now_ns() - origin_ns_, reason, size});
        unsaved_ = true;
    }

    // Writes when there is something new and the last write is long enough ago. The clock is read only then.
    constexpr void between_frames() {
        if (!unsaved_ || failure_) return;
        if (clock_.now_ns() - last_write_end_ns_ >= write_interval_ns_) write_file();
    }

    constexpr void finish() {
        ended_ = true;
        if (!failure_) write_file();
    }

    // Why writing stopped. It stops at the first failure and the program goes on.
    [[nodiscard]] constexpr std::optional<Error> error() const { return failure_; }

    // --- What was recorded ---

    [[nodiscard]] constexpr const NewestRows<FrameRow>& frames() const noexcept { return frames_; }
    [[nodiscard]] constexpr const NewestRows<DamageRow>& damage() const noexcept { return damage_; }
    [[nodiscard]] constexpr const NewestRows<DeferredRow>& deferred_rows() const noexcept { return deferred_; }
    [[nodiscard]] constexpr std::uint64_t writes() const noexcept { return writes_; }
    [[nodiscard]] constexpr const Clock& clock() const noexcept { return clock_; }
    [[nodiscard]] constexpr Clock& clock() noexcept { return clock_; }
    [[nodiscard]] constexpr const Sink& sink() const noexcept { return sink_; }
    [[nodiscard]] constexpr Sink& sink() noexcept { return sink_; }

    // The whole file as it would be written now.
    [[nodiscard]] constexpr std::string file_text() const {
        std::size_t refused = rows_refused_at_start_;
        std::string frame_rows;
        frame_rows.reserve(frames_.size() * 96);
        for (std::size_t i = 0; i < frames_.size(); ++i) append_frame(frame_rows, frames_[i], refused);
        std::string damage_rows;
        damage_rows.reserve(damage_.size() * 64);
        for (std::size_t i = 0; i < damage_.size(); ++i) append_damage(damage_rows, damage_[i], refused);
        std::string deferred_rows_text;
        for (std::size_t i = 0; i < deferred_.size(); ++i) append_deferred(deferred_rows_text, deferred_[i], refused);

        std::string out;
        out.reserve(frame_rows.size() + damage_rows.size() + deferred_rows_text.size() + 8192);
        append_header(out);
        out += detail::section_text("Run", detail::run_fields, run_row(refused));
        out += detail::section_text("Frames", detail::frame_fields, frame_rows);
        out += detail::section_text("Damage", detail::damage_fields, damage_rows);
        out += detail::section_text("Deferred", detail::deferred_fields, deferred_rows_text);
        out += "=== End File ===\n";
        return out;
    }

private:
    [[nodiscard]] constexpr std::int64_t lap() {
        const std::int64_t now = clock_.now_ns();
        return now - std::exchange(lap_start_ns_, now);
    }

    constexpr void write_file() {
        const std::int64_t start = clock_.now_ns();
        const std::string text = file_text();
        auto saved = sink_.save(text);
        last_write_end_ns_ = clock_.now_ns();
        last_write_ns_ = last_write_end_ns_ - start;
        ++writes_;
        unsaved_ = false;
        if (!saved) failure_ = std::move(saved.error());
    }

    constexpr void append_header(std::string& out) const {
        const std::string_view name = sink_.file_name();
        out += "//File:    ";
        out += name;
        out += "\n//Date:    ";
        out += date_;
        out += "\n//Purpose: jText Data File\n//Related: type=jCppGUI_redraw_monitor\n//\n=== jText File ===\n";
        const auto one = [&](std::size_t position, std::string_view value) {
            const std::array<std::string_view, 1> values{value};
            static_cast<void>(detail::append_line(out, position, values));
        };
        one(1, name);
        one(2, date_);
        one(3, "jCppGUI redraw monitor");
        one(4, "0.8");
        one(5, "Sensitive");
        one(8, "true");
    }

    [[nodiscard]] constexpr std::string run_row(std::size_t refused) const {
        const detail::DecimalText budget{slow_budget_ns_};
        const detail::DecimalText limit{row_limit_};
        const detail::DecimalText frames_seen{frames_.seen()};
        const detail::DecimalText frames_dropped{frames_.dropped()};
        const detail::DecimalText damage_seen{damage_.seen()};
        const detail::DecimalText damage_dropped{damage_.dropped()};
        const detail::DecimalText deferred_seen{deferred_.seen()};
        const detail::DecimalText deferred_dropped{deferred_.dropped()};
        const detail::DecimalText refused_text{refused};
        const detail::DecimalText writes{writes_};
        const detail::DecimalText previous{last_write_ns_};
        const std::array<std::string_view, detail::run_fields.size()> values{
            "1", label_, budget.view(), limit.view(), frames_seen.view(), frames_dropped.view(), damage_seen.view(), damage_dropped.view(),
            deferred_seen.view(), deferred_dropped.view(), refused_text.view(), writes.view(), previous.view(), detail::yes_no(ended_)};
        std::string out;
        // The label was checked when it was given, and all the rest is numbers.
        require(detail::append_line(out, 1, values), "a redraw monitor run row cannot be written");
        return out;
    }

    static constexpr void append_frame(std::string& out, const FrameRow& row, std::size_t& refused) {
        using detail::DecimalText;
        using detail::RedrawNumbers;
        const DecimalText id{row.frame_id};
        const DecimalText start{row.start_ns};
        const DecimalText left{RedrawNumbers::number(row.area.left())};
        const DecimalText top{RedrawNumbers::number(row.area.top())};
        const DecimalText width{RedrawNumbers::number(row.area.width())};
        const DecimalText height{RedrawNumbers::number(row.area.height())};
        const DecimalText layout{row.layout_ns};
        const DecimalText acquire{row.acquire_ns};
        const DecimalText paint{row.paint_ns};
        const DecimalText hash{row.hash_ns};
        const DecimalText present{row.present_ns};
        const DecimalText total{row.total_ns};
        const DecimalText last_write{row.last_write_ns};
        const std::array<std::string_view, detail::frame_fields.size()> values{
            id.view(), start.view(), detail::reason_text(row.reason), left.view(), top.view(), width.view(), height.view(), layout.view(),
            acquire.view(), paint.view(), hash.view(), present.view(), total.view(), last_write.view(), detail::yes_no(row.slow), detail::yes_no(row.same_pixels)};
        if (!detail::append_line(out, 1, values)) ++refused;
    }

    static constexpr void append_damage(std::string& out, const DamageRow& row, std::size_t& refused) {
        const detail::DecimalText id{row.record_id};
        const detail::DecimalText frame{row.frame_id};
        const detail::DecimalText position{row.widget.position};
        const std::array<std::string_view, detail::damage_fields.size()> values{id.view(), frame.view(), row.widget.class_name, row.widget.tag_name, position.view()};
        if (!detail::append_line(out, 1, values)) ++refused;
    }

    static constexpr void append_deferred(std::string& out, const DeferredRow& row, std::size_t& refused) {
        const detail::DecimalText id{row.record_id};
        const detail::DecimalText next{row.next_frame_id};
        const detail::DecimalText time{row.time_ns};
        const detail::DecimalText width{detail::RedrawNumbers::number(row.size.width())};
        const detail::DecimalText height{detail::RedrawNumbers::number(row.size.height())};
        const std::array<std::string_view, detail::deferred_fields.size()> values{id.view(), next.view(), time.view(), detail::reason_text(row.reason), width.view(), height.view()};
        if (!detail::append_line(out, 1, values)) ++refused;
    }

    Clock clock_;
    Sink sink_;
    std::string date_;
    std::string label_{};
    std::int64_t slow_budget_ns_;
    std::size_t row_limit_;
    std::int64_t write_interval_ns_;
    NewestRows<FrameRow> frames_;
    NewestRows<DamageRow> damage_;
    NewestRows<DeferredRow> deferred_;
    std::int64_t origin_ns_;
    std::int64_t last_write_end_ns_;
    std::int64_t lap_start_ns_{};
    std::int64_t last_write_ns_{};
    FrameRow pending_{};
    std::vector<DamagedWidget> pending_damage_{};
    std::uint64_t pending_hash_{};
    std::optional<std::uint64_t> previous_hash_{};
    Size previous_size_{};
    std::uint64_t writes_{};
    std::size_t rows_refused_at_start_{};
    std::optional<Error> failure_{};
    bool unsaved_{};
    bool ended_{};
};

template <MonitorClock Clock, MonitorSink Sink>
FrameMonitor(Clock, Sink) -> FrameMonitor<Clock, Sink>;
template <MonitorClock Clock, MonitorSink Sink>
FrameMonitor(Clock, Sink, FrameMonitorOptions) -> FrameMonitor<Clock, Sink>;

static_assert(RedrawMonitor<FrameMonitor<>>);

}  // namespace jgui::inline v001
