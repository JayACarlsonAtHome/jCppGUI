// File: include/jCppGUI/v001/file_listing.hpp
// Purpose: What a folder holds, as typed values, and the rules for names and paths. Names on a disk are untrusted: anyone may have made them. All of this
//          is constexpr. The part that asks the system is in local_files.hpp, and anything else that can answer FileSource can take its place.
#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cell_text.hpp"
#include "error.hpp"
#include "text.hpp"

namespace jgui::inline v001 {

// The order is the one a sort by this column gives. Unknown is something that went before it could be looked at, or that could not be looked at.
enum class FileKind : std::uint8_t { Folder, File, Link, Other, Unknown };

template <>
struct CellFormat<FileKind> {
    [[nodiscard]] static constexpr std::string_view text(FileKind kind) noexcept {
        switch (kind) {
            case FileKind::Folder: return "Folder";
            case FileKind::File: return "File";
            case FileKind::Link: return "Link";
            case FileKind::Other: return "Other";
            case FileKind::Unknown:
            default: return "Unknown";
        }
    }
};

// A file's length. Only a regular file has one worth showing, and anything else has a blank cell and sorts first.
struct FileBytes {
    bool known{};
    std::uint64_t count{};
    friend constexpr auto operator<=>(const FileBytes&, const FileBytes&) = default;
};

template <>
struct CellFormat<FileBytes> {
    [[nodiscard]] static constexpr std::string text(FileBytes size) {
        std::string grouped;
        const std::string digits = size.known ? decimal_text(size.count, false) : std::string{};
        for (std::size_t i = 0; i < digits.size(); ++i) {
            if (i != 0 && (digits.size() - i) % 3 == 0) grouped.push_back(',');
            grouped.push_back(digits[i]);
        }
        return grouped;
    }
};

// When a file last changed, in whole seconds since the start of 1970, in UTC.
struct FileTime {
    bool known{};
    std::int64_t seconds{};
    friend constexpr auto operator<=>(const FileTime&, const FileTime&) = default;
};

namespace detail {

inline constexpr std::int64_t seconds_per_day = 86'400;

// The year, month and day of a day number, with day 0 as 1970-01-01, by the proleptic Gregorian calendar. Every 64-bit count of seconds gives a day number
// well inside what these sums can hold.
struct CivilDate {
    std::int64_t year{};
    std::int64_t month{};
    std::int64_t day{};
};
[[nodiscard]] constexpr CivilDate civil_from_days(std::int64_t days) noexcept {
    constexpr std::int64_t days_per_era = 146'097;
    const std::int64_t shifted = days + 719'468;  // from 1970-01-01 to 0000-03-01, where a four-hundred-year era starts
    const std::int64_t era = (shifted >= 0 ? shifted : shifted - (days_per_era - 1)) / days_per_era;
    const std::int64_t day_of_era = shifted - era * days_per_era;
    const std::int64_t year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36'524 - day_of_era / 146'096) / 365;
    const std::int64_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const std::int64_t shifted_month = (5 * day_of_year + 2) / 153;
    const std::int64_t day = day_of_year - (153 * shifted_month + 2) / 5 + 1;
    const std::int64_t month = shifted_month < 10 ? shifted_month + 3 : shifted_month - 9;
    return CivilDate{year_of_era + era * 400 + (month <= 2 ? 1 : 0), month, day};
}

[[nodiscard]] constexpr std::string two_digits(std::int64_t value) {
    std::string text = decimal_text(static_cast<std::uint64_t>(value), false);
    return text.size() < 2 ? "0" + text : text;
}

}  // namespace detail

template <>
struct CellFormat<FileTime> {
    [[nodiscard]] static constexpr std::string text(FileTime time) {
        std::string out;
        if (!time.known) return out;
        std::int64_t days = time.seconds / detail::seconds_per_day;
        std::int64_t into_day = time.seconds % detail::seconds_per_day;
        if (into_day < 0) {
            into_day += detail::seconds_per_day;
            --days;
        }
        const detail::CivilDate date = detail::civil_from_days(days);
        if (date.year < 0) out.push_back('-');
        const std::uint64_t year = date.year < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(date.year) : static_cast<std::uint64_t>(date.year);
        const std::string year_digits = decimal_text(year, false);
        if (year_digits.size() < 4) out.append(4 - year_digits.size(), '0');
        out.append(year_digits).append("-").append(detail::two_digits(date.month)).append("-").append(detail::two_digits(date.day));
        out.append(" ").append(detail::two_digits(into_day / 3600)).append(":").append(detail::two_digits(into_day / 60 % 60));
        return out;
    }
};

// One thing in a folder.
struct FileEntry {
    std::string name{};
    FileKind kind{FileKind::Unknown};
    FileBytes size{};
    FileTime modified{};
};

// What a folder holds, and how many names it held that cannot be shown, which are not in it.
struct DirectoryListing {
    std::vector<FileEntry> entries{};
    std::size_t left_out{};
};

// What comparing a row with the other side found. It goes by what a folder's listing says, so the same size and time is what it says and no more: nothing is read from a file.
enum class FileMatch : std::uint8_t { NotCompared, OnlyHere, NewerHere, OlderHere, SizeDiffers, KindDiffers, SameSizeAndTime, InBoth };

template <>
struct CellFormat<FileMatch> {
    [[nodiscard]] static constexpr std::string_view text(FileMatch match) noexcept {
        switch (match) {
            case FileMatch::OnlyHere: return "Only here";
            case FileMatch::NewerHere: return "Newer here";
            case FileMatch::OlderHere: return "Older here";
            case FileMatch::SizeDiffers: return "Size differs";
            case FileMatch::KindDiffers: return "Kind differs";
            case FileMatch::SameSizeAndTime: return "Same size, time";
            case FileMatch::InBoth: return "In both";
            case FileMatch::NotCompared:
            default: return "";
        }
    }
};

// A row as comparing needs it: where it is below the folder at the top of its side, as a/b/c.
struct ComparedItem {
    std::string relative{};
    FileKind kind{FileKind::Unknown};
    FileBytes size{};
    FileTime modified{};
    bool closed_folder{};
};

// What two things with the same name are to each other. Only regular files have anything to compare, and a folder or a link being in both says nothing of what it holds or points to.
[[nodiscard]] constexpr FileMatch match_of_two(const ComparedItem& here, const ComparedItem& there) noexcept {
    if (here.kind != there.kind) return FileMatch::KindDiffers;
    if (here.kind != FileKind::File || !here.size.known || !there.size.known || !here.modified.known || !there.modified.known) return FileMatch::InBoth;
    if (here.modified.seconds > there.modified.seconds) return FileMatch::NewerHere;
    if (here.modified.seconds < there.modified.seconds) return FileMatch::OlderHere;
    return here.size.count == there.size.count ? FileMatch::SameSizeAndTime : FileMatch::SizeDiffers;
}

// What a row is against the other side, whose items are sorted by their relative paths. Something not there is only here, unless a folder on the way to where it would be is closed there,
// and then nobody has looked, so it is not compared.
[[nodiscard]] constexpr FileMatch match_against(const ComparedItem& item, std::span<const ComparedItem> there) {
    const auto find = [&](std::string_view relative) -> const ComparedItem* {
        const auto at = std::ranges::lower_bound(there, relative, {}, &ComparedItem::relative);
        return at != there.end() && at->relative == relative ? &*at : nullptr;
    };
    if (const ComparedItem* same = find(item.relative); same != nullptr) return match_of_two(item, *same);
    std::string_view up{item.relative};
    while (true) {
        const std::size_t slash = up.rfind('/');
        if (slash == std::string_view::npos) return FileMatch::OnlyHere;
        up = up.substr(0, slash);
        if (const ComparedItem* above = find(up); above != nullptr) return above->closed_folder ? FileMatch::NotCompared : FileMatch::OnlyHere;
    }
}

// How a side's rows came out.
struct FileMatchCounts {
    std::uint64_t only_here{};
    std::uint64_t differ{};
    std::uint64_t same{};
    std::uint64_t in_both{};
    std::uint64_t not_looked_at{};
};

// How far a copy has got.
struct CopyProgress {
    std::uint64_t files{};
    std::uint64_t folders{};
    std::uint64_t links{};
    std::uint64_t bytes{};
    std::uint64_t bytes_checked{};  // how much of those has been read back from both sides and summed
    std::uint64_t not_copied{};     // things that are none of those, such as a pipe or a device
};

// A copy under way, done a little at a time, so the window goes on between steps. A step answers true when all of it is done. Destroying one that is not done stops it.
// A file is counted in files only after its SHA-256 has matched its original's, each read back from where it is, and one that does not match is an Error and is not kept.
template <class J>
concept FileCopyJob = std::movable<J> && requires(J& job, const J& constant) {
    { job.step() } -> std::same_as<std::expected<bool, Error>>;
    { constant.progress() } -> std::same_as<CopyProgress>;
};

// One thing that did not match, or that could not be looked at, which is not a match either.
struct MatchFinding {
    std::string where{};
    std::string why{};
};

// How far matching has got. What did not match is counted in differences and in nothing else.
struct MatchProgress {
    std::uint64_t files{};    // read from both sides, with the same SHA-256
    std::uint64_t folders{};  // looked through, whatever was found inside
    std::uint64_t links{};    // saying the same thing
    std::uint64_t bytes{};    // read from each side
    std::uint64_t differences{};
};

inline constexpr std::size_t most_match_findings = 10'000;  // listed, and the rest only counted, so a pair of very different trees cannot fill the memory

// Matching two things, done a little at a time. It only reads. A difference does not end it: it goes on to the next thing, so all of them can be listed.
template <class J>
concept FileMatchJob = std::movable<J> && requires(J& job, const J& constant) {
    { job.step() } -> std::same_as<std::expected<bool, Error>>;
    { constant.progress() } -> std::same_as<MatchProgress>;
    { constant.findings() } -> std::same_as<std::span<const MatchFinding>>;
};

// What deleting something comes to. Files here are everything that is not a folder, so links, pipes and devices too. Bytes are those of regular files.
struct DeleteCount {
    std::uint64_t files{};
    std::uint64_t folders{};
    std::uint64_t bytes{};
};

// Finding out what deleting something would remove, a little at a time. It changes nothing. Anything it cannot look at, or another mounted disk inside, is an Error, so nothing is deleted on a count that may be short.
template <class J>
concept FileCountJob = std::movable<J> && requires(J& job, const J& constant) {
    { job.step() } -> std::same_as<std::expected<bool, Error>>;
    { constant.counted() } -> std::same_as<DeleteCount>;
};

// Where files come from and go to. Each pane holds one, of its own type, so a source that reaches another machine is a type of its own and nothing is erased.
// A copy leaves the original alone and may not replace what is already there: a name that is taken is refused. Nothing here removes anything for good. move_to_trash is the one way something leaves where it
// is, and only FileTransfers calls it, after a person has been told the count and has said yes.
template <class S>
concept FileSource = FileCopyJob<typename S::CopyJob> && FileMatchJob<typename S::MatchJob> && FileCountJob<typename S::CountJob> && requires(const S& source, const std::string& path) {
    { source.read(path) } -> std::same_as<std::expected<DirectoryListing, Error>>;
    { source.start_copy(path, path) } -> std::same_as<std::expected<typename S::CopyJob, Error>>;
    { source.start_match(path, path) } -> std::same_as<std::expected<typename S::MatchJob, Error>>;
    { source.start_count(path) } -> std::same_as<std::expected<typename S::CountJob, Error>>;
    { source.move_to_trash(path) } -> std::same_as<std::expected<void, Error>>;
    { source.read_text(path) } -> std::same_as<std::expected<std::string, Error>>;
    { source.touch(path) } -> std::same_as<std::expected<void, Error>>;
    { source.editor() } -> std::same_as<std::string>;
    { source.run_in_terminal(path, std::span<const std::string>{}) } -> std::same_as<std::expected<void, Error>>;
};

inline constexpr std::size_t most_file_path_bytes = 4096;  // the longest path Linux takes in one call
inline constexpr std::size_t most_file_name_bytes = 255;   // the longest name Linux has

// False for invalid UTF-8, and for a control character of either range, so no name can break a line or a display.
[[nodiscard]] constexpr bool text_can_show(std::string_view text) noexcept {
    if (!valid_utf8(text)) return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto byte = static_cast<unsigned char>(text[i]);
        if (byte < 0x20U || byte == 0x7FU) return false;
        // Valid UTF-8, so a C2 always has a next byte. U+0080 to U+009F are the second range.
        if (byte == 0xC2U && static_cast<unsigned char>(text[i + 1]) < 0xA0U) return false;
    }
    return true;
}

// A name a listing may hold: not empty, not a way to climb out of the folder, no slash in it, within the limit, and text that can be shown.
[[nodiscard]] constexpr bool file_name_can_show(std::string_view name) noexcept {
    return !name.empty() && name != "." && name != ".." && name.size() <= most_file_name_bytes && !name.contains('/') && text_can_show(name);
}

// A path a pane may hold: absolute, within the limit, text that can be shown, with no empty, "." or ".." part. So what is above it is plain from its text.
[[nodiscard]] constexpr bool file_path_can_show(std::string_view path) noexcept {
    if (path.empty() || path.front() != '/' || path.size() > most_file_path_bytes || !text_can_show(path)) return false;
    if (path == "/") return true;
    std::string_view rest = path.substr(1);
    while (true) {
        const std::size_t slash = rest.find('/');
        const std::string_view part = rest.substr(0, slash);
        if (part.empty() || part == "." || part == "..") return false;
        if (slash == std::string_view::npos) return true;
        rest = rest.substr(slash + 1);
    }
}

// The folder above. The top has none, and is its own.
[[nodiscard]] constexpr std::string parent_of_path(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    if (slash == std::string_view::npos || slash == 0) return "/";
    return std::string{path.substr(0, slash)};
}

[[nodiscard]] constexpr std::string child_of_path(std::string_view folder, std::string_view name) {
    std::string path{folder};
    if (path.empty() || path.back() != '/') path.push_back('/');
    path.append(name);
    return path;
}

// What follows the last slash.
[[nodiscard]] constexpr std::string_view last_name_of(std::string_view path) noexcept {
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

// True for the folder itself and for anything under it, by the text alone.
[[nodiscard]] constexpr bool path_is_inside(std::string_view path, std::string_view folder) noexcept {
    if (folder == "/") return path.starts_with('/');
    return path == folder || (path.starts_with(folder) && path.size() > folder.size() && path[folder.size()] == '/');
}

// Whether something can go into this folder, as far as the text says. The source itself says whether the name is taken.
[[nodiscard]] constexpr std::expected<void, Error> check_transfer(std::string_view from, std::string_view into) {
    if (!file_path_can_show(from) || !file_path_can_show(into)) return make_error(ErrorCode::System, "That path cannot be used");
    if (from == "/") return make_error(ErrorCode::System, "The top of the disk cannot go anywhere");
    if (path_is_inside(into, from)) return make_error(ErrorCode::System, "A folder cannot go inside itself");
    if (child_of_path(into, last_name_of(from)).size() > most_file_path_bytes) return make_error(ErrorCode::SizeLimit, "That path would be too long");
    return {};
}

// Two things to match, one at a time.
struct MatchPair {
    std::string first{};
    std::string second{};
};

// What a selection comes to. Something with nothing to match it is already a finding. A refusal says why nothing can start.
struct MatchPlan {
    std::vector<MatchPair> pairs{};
    std::vector<MatchFinding> unpaired{};
    std::string refused{};
};

namespace detail {
// The first name that two of these share, or empty.
[[nodiscard]] constexpr std::string_view repeated_last_name(std::span<const std::string> paths) noexcept {
    for (std::size_t i = 0; i < paths.size(); ++i)
        for (std::size_t j = i + 1; j < paths.size(); ++j)
            if (last_name_of(paths[i]) == last_name_of(paths[j])) return last_name_of(paths[i]);
    return {};
}
}  // namespace detail

// One selected on each side: those two, whatever they are called. Several across the two sides: by name. All on one side: each against the first, to say whether they are all the same.
[[nodiscard]] constexpr MatchPlan plan_matches(std::span<const std::string> left, std::span<const std::string> right) {
    MatchPlan plan;
    if (left.size() == 1 && right.size() == 1) {
        plan.pairs.push_back(MatchPair{left.front(), right.front()});
        return plan;
    }
    if (left.empty() || right.empty()) {
        const std::span<const std::string> all = left.empty() ? right : left;
        for (std::size_t i = 1; i < all.size(); ++i) plan.pairs.push_back(MatchPair{all.front(), all[i]});
        return plan;
    }
    for (const std::span<const std::string> side : {left, right}) {
        if (const std::string_view name = detail::repeated_last_name(side); !name.empty()) {
            plan.refused = "Nothing was matched: two selected on one side have the same name, " + std::string{name};
            return plan;
        }
    }
    const auto named = [](std::span<const std::string> side, std::string_view name) { return std::ranges::any_of(side, [&](const std::string& path) { return last_name_of(path) == name; }); };
    for (const std::string& path : left) {
        const auto partner = std::ranges::find_if(right, [&](const std::string& other) { return last_name_of(other) == last_name_of(path); });
        if (partner != right.end()) {
            plan.pairs.push_back(MatchPair{path, *partner});
        } else {
            plan.unpaired.push_back(MatchFinding{std::string{last_name_of(path)}, "Nothing selected on the right has this name"});
        }
    }
    for (const std::string& path : right)
        if (!named(left, last_name_of(path))) plan.unpaired.push_back(MatchFinding{std::string{last_name_of(path)}, "Nothing selected on the left has this name"});
    return plan;
}

// --- The freedesktop.org trash ---

// When something went to the trash, as the local clock said.
struct TrashDate {
    std::int64_t year{};
    std::int64_t month{};
    std::int64_t day{};
    std::int64_t hour{};
    std::int64_t minute{};
    std::int64_t second{};
};

// A path as a trash's info file holds it: every byte but the unreserved ones and the slashes as %XX.
[[nodiscard]] constexpr std::string trash_escaped_path(std::string_view path) {
    constexpr std::string_view digits = "0123456789ABCDEF";
    std::string out;
    out.reserve(path.size());
    for (const char c : path) {
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' || c == '/';
        if (plain) {
            out.push_back(c);
        } else {
            const auto byte = static_cast<unsigned char>(c);
            out.push_back('%');
            out.push_back(digits[byte >> 4U]);
            out.push_back(digits[byte & 0x0FU]);
        }
    }
    return out;
}

// What the info file says, from which a desktop's trash can put the thing back.
[[nodiscard]] constexpr std::string trash_info_text(std::string_view path, const TrashDate& date) {
    std::string out = "[Trash Info]\nPath=" + trash_escaped_path(path) + "\nDeletionDate=" + decimal_text(static_cast<std::uint64_t>(date.year), false);
    out.append("-").append(detail::two_digits(date.month)).append("-").append(detail::two_digits(date.day)).append("T");
    out.append(detail::two_digits(date.hour)).append(":").append(detail::two_digits(date.minute)).append(":").append(detail::two_digits(date.second)).append("\n");
    return out;
}

inline constexpr std::string_view trash_info_suffix = ".trashinfo";

// The name to try in the trash, for each attempt: its own, then with .1, .2 and so on after it. It is cut to leave room for those and for the info file's suffix, on a whole character.
[[nodiscard]] constexpr std::string trash_name_to_try(std::string_view name, std::uint32_t attempt) {
    const std::string number = attempt == 0 ? std::string{} : "." + decimal_text(attempt, false);
    const std::size_t room = most_file_name_bytes - trash_info_suffix.size() - number.size();
    return std::string{utf8_prefix(name, std::min(room, name.size()))} + number;
}

// True for a trash itself and all in it, by the text alone: a part named as a disk's trash is, or anything inside the home trash.
[[nodiscard]] constexpr bool path_is_in_a_trash(std::string_view path, std::string_view home_trash) noexcept {
    if (path_is_inside(path, home_trash)) return true;
    std::string_view rest = path;
    while (!rest.empty()) {
        const std::size_t slash = rest.find('/');
        const std::string_view part = rest.substr(0, slash);
        if (part == ".Trash" || part.starts_with(".Trash-")) return true;
        if (slash == std::string_view::npos) return false;
        rest = rest.substr(slash + 1);
    }
    return false;
}

// Negative when a comes first, ignoring the case of A to Z, with the bytes themselves to settle a tie, so no two different names tie.
[[nodiscard]] constexpr int compare_file_names(std::string_view a, std::string_view b) noexcept {
    const auto folded = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    const std::size_t common = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < common; ++i) {
        const auto first = static_cast<unsigned char>(folded(a[i]));
        const auto second = static_cast<unsigned char>(folded(b[i]));
        if (first != second) return first < second ? -1 : 1;
    }
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    return a.compare(b);
}

// Folders first, then by name.
constexpr void sort_file_entries(std::vector<FileEntry>& entries) {
    std::ranges::sort(entries, [](const FileEntry& a, const FileEntry& b) {
        const bool a_folder = a.kind == FileKind::Folder;
        const bool b_folder = b.kind == FileKind::Folder;
        if (a_folder != b_folder) return a_folder;
        return compare_file_names(a.name, b.name) < 0;
    });
}

}  // namespace jgui::inline v001
