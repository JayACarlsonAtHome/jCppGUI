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

// Where files come from and go to. Each pane holds one, of its own type, so a source that reaches another machine is a type of its own and nothing is erased.
// The one way of changing anything is a copy, which leaves the original alone and may not replace what is already there: a name that is taken is refused.
// Nothing here moves or deletes. A person deletes an original by hand, after the copy has said it matched.
template <class S>
concept FileSource = FileCopyJob<typename S::CopyJob> && requires(const S& source, const std::string& path) {
    { source.read(path) } -> std::same_as<std::expected<DirectoryListing, Error>>;
    { source.start_copy(path, path) } -> std::same_as<std::expected<typename S::CopyJob, Error>>;
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
