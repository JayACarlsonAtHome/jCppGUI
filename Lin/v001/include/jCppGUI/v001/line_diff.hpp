// File: include/jCppGUI/v001/line_diff.hpp
// Purpose: LineDiffer: which lines of two texts differ, by Myers' shortest-edit search, a little at a time so the window goes on, and rows to show of it. The texts are
//          untrusted: anything may be in them, so each line is made safe to show, and sizes, line counts and the work to be done all have limits. All of this is constexpr.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "error.hpp"
#include "file_listing.hpp"
#include "text.hpp"

namespace jgui::inline v001 {

inline constexpr std::size_t most_diff_file_bytes = std::size_t{16} * 1024 * 1024;
inline constexpr std::size_t most_diff_lines = 200'000;     // on each side
inline constexpr std::int32_t most_diff_changes = 4'000;    // the search's memory grows with the square of this: about 32 MiB at the most
inline constexpr std::size_t diff_context_lines = 3;        // shown each side of a change

// One row of a diff. A line in both sides has both numbers. A row with neither stands for lines the same that are not shown.
struct DiffRow {
    FileBytes left{};
    FileBytes right{};
    std::string only_in{};
    std::string text{};
};

struct DiffOutcome {
    std::vector<DiffRow> rows{};
    std::uint64_t left_only{};
    std::uint64_t right_only{};
    bool too_different{};  // the search gave up, so rows is empty and says nothing
};

namespace detail {

// A line of a text, with its newline if it has one, so a last line with none differs from one with.
struct LineSpan {
    std::size_t at{};
    std::size_t length{};
};

[[nodiscard]] constexpr std::vector<LineSpan> lines_of(std::string_view text) {
    std::vector<LineSpan> lines;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t newline = text.find('\n', at);
        const std::size_t end = newline == std::string_view::npos ? text.size() : newline + 1;
        lines.push_back(LineSpan{at, end - at});
        at = end;
    }
    return lines;
}

}  // namespace detail

// A line as it may be shown. A tab becomes spaces. A carriage return shows as its own picture, so two lines differing by only that do not look alike. Other control characters, and those
// that break a line or change the direction of text, which could make what shows differ from what is there, become the replacement character. A line that is not UTF-8 is not shown at all.
[[nodiscard]] constexpr std::string shown_line(std::string_view line) {
    constexpr std::string_view replacement = "\xEF\xBF\xBD";
    constexpr std::string_view return_picture = "\xE2\x90\x8D";
    const bool has_newline = line.ends_with('\n');
    if (has_newline) line.remove_suffix(1);
    if (!valid_utf8(line)) return "(a line that is not UTF-8, " + decimal_text(line.size(), false) + " bytes)";
    std::string out;
    out.reserve(std::min(line.size(), max_caption_bytes));
    for (std::size_t i = 0; i < line.size() && out.size() < max_caption_bytes; ++i) {
        const auto byte = static_cast<unsigned char>(line[i]);
        const auto next = [&](std::size_t ahead) { return static_cast<unsigned char>(line[i + ahead]); };  // valid UTF-8, so a lead byte's others are there
        if (byte == '\t') {
            out.append("    ");
        } else if (byte == '\r') {
            out.append(return_picture);
        } else if (byte < 0x20U || byte == 0x7FU) {
            out.append(replacement);
        } else if (byte == 0xC2U && next(1) < 0xA0U) {
            out.append(replacement);
            i += 1;
        } else if (byte == 0xE2U && next(1) == 0x80U && next(2) >= 0xA8U && next(2) <= 0xAEU) {  // U+2028 to U+202E
            out.append(replacement);
            i += 2;
        } else if (byte == 0xE2U && next(1) == 0x81U && next(2) >= 0xA6U && next(2) <= 0xA9U) {  // U+2066 to U+2069
            out.append(replacement);
            i += 2;
        } else {
            out.push_back(line[i]);
        }
    }
    if (!has_newline) out.append(" (no newline at the end)");
    return std::string{utf8_prefix(out, max_caption_bytes)};
}

class LineDiffer {
public:
    // Refuses what is too long, and what holds a NUL, which is how a file that is not text is told apart.
    [[nodiscard]] static constexpr std::expected<LineDiffer, Error> start(std::string first, std::string second) {
        if (first.size() > most_diff_file_bytes || second.size() > most_diff_file_bytes) return make_error(ErrorCode::SizeLimit, "a file to diff can be at most 16 MiB");
        if (first.contains('\0') || second.contains('\0')) return make_error(ErrorCode::System, "those are not both text, so Compare is the way to check them");
        LineDiffer differ{std::move(first), std::move(second)};
        if (differ.first_lines_.size() > most_diff_lines || differ.second_lines_.size() > most_diff_lines) return make_error(ErrorCode::SizeLimit, "a file to diff can have at most 200,000 lines");
        differ.number_the_lines();
        differ.trim_the_ends();
        return differ;
    }

    // One round of the search, each allowing one more change than the last. True when it has ended, either way.
    [[nodiscard]] constexpr bool step() {
        if (ended_) return true;
        const auto d = static_cast<std::int32_t>(trace_.size());
        if (d > most_diff_changes) {
            too_different_ = true;
            ended_ = true;
            return true;
        }
        std::vector<std::int32_t> furthest(static_cast<std::size_t>(d) + 1);
        for (std::int32_t k = -d; k <= d; k += 2) {
            std::int32_t x = d == 0 ? 0 : arrive(k, at_(k - 1), at_(k + 1)).x;
            if (x >= 0) {
                while (x < across_ && x - k < down_ && first_ids_[to_index(start_ + x)] == second_ids_[to_index(start_ + x - k)]) ++x;
            }
            furthest[to_index((k + d) / 2)] = x;
            if (x == across_ && x - k == down_) ended_ = true;
        }
        // Each diagonal is only needed from the round before, and those of the other parity are overwritten next round, so they are all up to date when read.
        for (std::int32_t k = -d; k <= d; k += 2) at_(k) = furthest[to_index((k + d) / 2)];
        trace_.push_back(std::move(furthest));
        return ended_;
    }

    [[nodiscard]] constexpr bool ended() const noexcept { return ended_; }

    // What the search came to. Only worth asking once it has ended.
    [[nodiscard]] constexpr DiffOutcome outcome() const {
        DiffOutcome out;
        if (too_different_) {
            out.too_different = true;
            return out;
        }
        const std::vector<Edit> edits = walk_back();
        std::vector<bool> shown(edits.size(), false);
        for (std::size_t i = 0; i < edits.size(); ++i) {
            if (edits[i].kind == EditKind::Same) continue;
            const std::size_t from = i < diff_context_lines ? 0 : i - diff_context_lines;
            const std::size_t to = std::min(edits.size(), i + diff_context_lines + 1);
            for (std::size_t j = from; j < to; ++j) shown[j] = true;
        }
        std::size_t hidden = 0;
        for (std::size_t i = 0; i < edits.size(); ++i) {
            const Edit& edit = edits[i];
            if (!shown[i]) {
                ++hidden;
                continue;
            }
            if (hidden != 0) out.rows.push_back(same_lines_row(hidden));
            hidden = 0;
            switch (edit.kind) {
                case EditKind::OnlyFirst:
                    ++out.left_only;
                    out.rows.push_back(DiffRow{number(edit.first), {}, "Left", shown_line(line_of(first_, first_lines_[edit.first]))});
                    break;
                case EditKind::OnlySecond:
                    ++out.right_only;
                    out.rows.push_back(DiffRow{{}, number(edit.second), "Right", shown_line(line_of(second_, second_lines_[edit.second]))});
                    break;
                case EditKind::Same:
                default: out.rows.push_back(DiffRow{number(edit.first), number(edit.second), {}, shown_line(line_of(first_, first_lines_[edit.first]))}); break;
            }
        }
        if (hidden != 0 && (out.left_only != 0 || out.right_only != 0)) out.rows.push_back(same_lines_row(hidden));
        return out;
    }

private:
    enum class EditKind : std::uint8_t { Same, OnlyFirst, OnlySecond };
    struct Edit {
        EditKind kind{};
        std::size_t first{};
        std::size_t second{};
    };
    struct Arrival {
        std::int32_t x{-1};
        std::int32_t from_k{};
    };

    constexpr LineDiffer(std::string first, std::string second) : first_{std::move(first)}, second_{std::move(second)}, first_lines_{detail::lines_of(first_)}, second_lines_{detail::lines_of(second_)} {}

    [[nodiscard]] static constexpr std::size_t to_index(std::int32_t value) noexcept { return static_cast<std::size_t>(value); }
    [[nodiscard]] static constexpr std::string_view line_of(std::string_view text, detail::LineSpan span) noexcept { return text.substr(span.at, span.length); }
    [[nodiscard]] static constexpr FileBytes number(std::size_t index) noexcept { return FileBytes{true, std::uint64_t{index} + 1}; }
    [[nodiscard]] static constexpr DiffRow same_lines_row(std::size_t count) {
        return DiffRow{{}, {}, {}, "... " + decimal_text(count, false) + (count == 1 ? " line the same ..." : " lines the same ...")};
    }

    // Each different line gets a number of its own, and the search compares only those.
    constexpr void number_the_lines() {
        struct Numbered {
            std::string_view text{};
            std::size_t where{};
        };
        std::vector<Numbered> all;
        all.reserve(first_lines_.size() + second_lines_.size());
        for (std::size_t i = 0; i < first_lines_.size(); ++i) all.push_back(Numbered{line_of(first_, first_lines_[i]), i});
        for (std::size_t i = 0; i < second_lines_.size(); ++i) all.push_back(Numbered{line_of(second_, second_lines_[i]), first_lines_.size() + i});
        std::ranges::sort(all, {}, &Numbered::text);
        first_ids_.resize(first_lines_.size());
        second_ids_.resize(second_lines_.size());
        std::uint32_t id = 0;
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (i != 0 && all[i].text != all[i - 1].text) ++id;
            if (all[i].where < first_ids_.size()) {
                first_ids_[all[i].where] = id;
            } else {
                second_ids_[all[i].where - first_ids_.size()] = id;
            }
        }
    }

    // What is the same at the start and the end costs the search nothing.
    constexpr void trim_the_ends() {
        const std::size_t shorter = std::min(first_ids_.size(), second_ids_.size());
        std::size_t same_start = 0;
        while (same_start < shorter && first_ids_[same_start] == second_ids_[same_start]) ++same_start;
        std::size_t same_end = 0;
        while (same_end < shorter - same_start && first_ids_[first_ids_.size() - 1 - same_end] == second_ids_[second_ids_.size() - 1 - same_end]) ++same_end;
        start_ = static_cast<std::int32_t>(same_start);
        same_end_ = same_end;
        across_ = static_cast<std::int32_t>(first_ids_.size() - same_start - same_end);
        down_ = static_cast<std::int32_t>(second_ids_.size() - same_start - same_end);
        // Room for every diagonal the search may reach, and one each side, marked as not reached.
        diagonals_.assign(to_index(2 * most_diff_changes + 5), -1);
    }

    // The furthest along this diagonal by one more change, from the one below by a line only in the first, or from the one above by a line only in the second. A move off the end of a text does not count,
    // so nothing is ever past the end. The walk back asks this too, so it retraces the same choice.
    [[nodiscard]] constexpr Arrival arrive(std::int32_t k, std::int32_t from_below, std::int32_t from_above) const noexcept {
        Arrival best;
        if (from_above >= 0 && from_above - k <= down_) best = Arrival{from_above, k + 1};
        if (from_below >= 0 && from_below + 1 <= across_ && from_below + 1 > best.x) best = Arrival{from_below + 1, k - 1};
        return best;
    }

    [[nodiscard]] constexpr std::int32_t& at_(std::int32_t k) noexcept { return diagonals_[to_index(k + most_diff_changes + 2)]; }

    // What a round found on a diagonal, or not reached for one that round did not hold.
    [[nodiscard]] constexpr std::int32_t in_round(std::int32_t d, std::int32_t k) const noexcept {
        if (k < -d || k > d) return -1;
        return trace_[to_index(d)][to_index((k + d) / 2)];
    }

    [[nodiscard]] constexpr std::vector<Edit> walk_back() const {
        std::vector<Edit> edits;
        edits.reserve(first_ids_.size() + second_ids_.size());
        const auto same = [&](std::size_t i) { return Edit{EditKind::Same, i, i + (second_ids_.size() - first_ids_.size())}; };
        for (std::size_t i = first_ids_.size(); i-- > first_ids_.size() - same_end_;) edits.push_back(same(i));
        std::int32_t x = across_;
        std::int32_t y = down_;
        for (auto d = static_cast<std::int32_t>(trace_.size()) - 1; d >= 0; --d) {
            const std::int32_t k = x - y;
            std::int32_t before_x = 0;
            std::int32_t before_y = 0;
            if (d != 0) {
                const Arrival arrival = arrive(k, in_round(d - 1, k - 1), in_round(d - 1, k + 1));
                before_x = in_round(d - 1, arrival.from_k);
                before_y = before_x - arrival.from_k;
                // The lines after the change are the same, and then comes the change itself.
                while (x > arrival.x) {
                    --x;
                    --y;
                    edits.push_back(Edit{EditKind::Same, to_index(start_ + x), to_index(start_ + y)});
                }
                if (arrival.from_k == k - 1) {
                    edits.push_back(Edit{EditKind::OnlyFirst, to_index(start_ + before_x), 0});
                } else {
                    edits.push_back(Edit{EditKind::OnlySecond, 0, to_index(start_ + before_y)});
                }
                x = before_x;
                y = before_y;
            } else {
                while (x > 0) {
                    --x;
                    --y;
                    edits.push_back(Edit{EditKind::Same, to_index(start_ + x), to_index(start_ + y)});
                }
            }
        }
        for (std::size_t i = to_index(start_); i-- > 0;) edits.push_back(Edit{EditKind::Same, i, i});
        std::ranges::reverse(edits);
        return edits;
    }

    std::string first_;
    std::string second_;
    std::vector<detail::LineSpan> first_lines_;
    std::vector<detail::LineSpan> second_lines_;
    std::vector<std::uint32_t> first_ids_{};
    std::vector<std::uint32_t> second_ids_{};
    std::vector<std::int32_t> diagonals_{};
    std::vector<std::vector<std::int32_t>> trace_{};
    std::size_t same_end_{};
    std::int32_t start_{};
    std::int32_t across_{};
    std::int32_t down_{};
    bool too_different_{};
    bool ended_{};
};

}  // namespace jgui::inline v001
