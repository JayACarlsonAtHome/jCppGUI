// File: src/files/match_job.cpp
// Purpose: Matches two things on this machine's disks, and everything in them. It only reads, through descriptors opened without following a link. Files are read from both sides and
//          their SHA-256 sums compared, folders by what names they hold, and links by what they say. Anything that cannot be looked at is a finding, so it can never pass as a match.
//          Bytes go only through read_at.
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "c_resource.hpp"
#include "file_io.hpp"
#include "folder_names.hpp"
#include "jCppGUI/v001/local_files.hpp"
#include "jCppGUI/v001/sha256.hpp"

namespace jgui::inline v001 {

namespace {

using detail::names_in;
using detail::reason_of;
using detail::UniqueFd;

inline constexpr std::size_t match_chunk_bytes = std::size_t{1024} * 1024;
inline constexpr std::size_t most_match_depth = 128;  // each level holds two descriptors open

// A name on a disk can be anything, and a finding is text that must show.
[[nodiscard]] std::string where_inside(std::string_view where, std::string_view name) {
    std::string out{where};
    out.push_back('/');
    out.append(file_name_can_show(name) ? name : std::string_view{"(a name that cannot be shown)"});
    return out;
}

[[nodiscard]] bool same_time(const timespec& a, const timespec& b) noexcept { return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec; }

// One folder on each side being walked.
struct MatchLevel {
    UniqueFd first;
    UniqueFd second;
    std::vector<std::string> first_names{};
    std::vector<std::string> second_names{};
    std::size_t first_at{};
    std::size_t second_at{};
    std::string where{};
};

// The two files being read now.
struct MatchingFiles {
    UniqueFd first;
    UniqueFd second;
    std::string where{};
    std::int64_t size{};
    std::int64_t done{};
    timespec first_time{};
    timespec second_time{};
    Sha256 first_sum{};
    Sha256 second_sum{};
};

}  // namespace

struct LocalMatchJob::Impl {
    UniqueFd top_first{};
    UniqueFd top_second{};
    std::string first_name{};
    std::string second_name{};
    std::string first_path{};
    std::string second_path{};
    std::string top_where{};
    std::vector<MatchLevel> levels{};
    std::optional<MatchingFiles> files{};
    std::vector<char> buffer{};
    std::vector<MatchFinding> found{};
    MatchProgress progress{};
    bool started{};

    void note(std::string where, std::string why) {
        ++progress.differences;
        if (found.size() < most_match_findings) found.push_back(MatchFinding{std::move(where), std::move(why)});
    }

    void compare_items(int first_folder, const std::string& first, int second_folder, const std::string& second, std::string where) {
        struct stat a {};
        struct stat b {};
        if (::fstatat(first_folder, first.c_str(), &a, AT_SYMLINK_NOFOLLOW) != 0) {
            note(std::move(where), "Cannot look at it in " + first_path + ": " + reason_of(errno));
            return;
        }
        if (::fstatat(second_folder, second.c_str(), &b, AT_SYMLINK_NOFOLLOW) != 0) {
            note(std::move(where), "Cannot look at it in " + second_path + ": " + reason_of(errno));
            return;
        }
        if ((a.st_mode & S_IFMT) != (b.st_mode & S_IFMT)) {
            note(std::move(where), "One is not the same kind of thing as the other");
            return;
        }
        // Deleting one of these after being told they matched would lose it, so this is never a match.
        if (a.st_dev == b.st_dev && a.st_ino == b.st_ino) {
            note(std::move(where), "This is one and the same thing on both sides, not two copies");
            return;
        }
        if (S_ISREG(a.st_mode)) {
            begin_files(first_folder, first, second_folder, second, std::move(where));
        } else if (S_ISDIR(a.st_mode)) {
            begin_folders(first_folder, first, second_folder, second, std::move(where));
        } else if (S_ISLNK(a.st_mode)) {
            compare_links(first_folder, first, second_folder, second, std::move(where));
        } else {
            note(std::move(where), "A pipe, device or socket has nothing that can be checked");
        }
    }

    void begin_files(int first_folder, const std::string& first, int second_folder, const std::string& second, std::string where) {
        constexpr int how = O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC;
        UniqueFd a{::openat(first_folder, first.c_str(), how)};
        if (a.get() < 0) {
            note(std::move(where), "Cannot open it in " + first_path + ": " + reason_of(errno));
            return;
        }
        UniqueFd b{::openat(second_folder, second.c_str(), how)};
        if (b.get() < 0) {
            note(std::move(where), "Cannot open it in " + second_path + ": " + reason_of(errno));
            return;
        }
        struct stat opened_a {};
        struct stat opened_b {};
        if (::fstat(a.get(), &opened_a) != 0 || ::fstat(b.get(), &opened_b) != 0) {
            note(std::move(where), "Cannot look at it: " + reason_of(errno));
            return;
        }
        if (!S_ISREG(opened_a.st_mode) || !S_ISREG(opened_b.st_mode)) {
            note(std::move(where), "It changed into something else while it was being checked");
            return;
        }
        if (opened_a.st_size != opened_b.st_size) {
            note(std::move(where), "The sizes differ: " + CellFormat<FileBytes>::text(FileBytes{true, saturate_to<std::uint64_t>(opened_a.st_size)}) + " and " + CellFormat<FileBytes>::text(FileBytes{true, saturate_to<std::uint64_t>(opened_b.st_size)}) + " bytes");
            return;
        }
        // Out of the cache where the system lets it, so this reads what is on the disk. It is only advice.
        static_cast<void>(::posix_fadvise(a.get(), 0, 0, POSIX_FADV_DONTNEED));
        static_cast<void>(::posix_fadvise(b.get(), 0, 0, POSIX_FADV_DONTNEED));
        files.emplace(MatchingFiles{std::move(a), std::move(b), std::move(where), saturate_to<std::int64_t>(opened_a.st_size), 0, opened_a.st_mtim, opened_b.st_mtim});
    }

    void begin_folders(int first_folder, const std::string& first, int second_folder, const std::string& second, std::string where) {
        if (levels.size() >= most_match_depth) {
            note(std::move(where), "It is too many folders deep to check");
            return;
        }
        constexpr int how = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC;
        UniqueFd a{::openat(first_folder, first.c_str(), how)};
        if (a.get() < 0) {
            note(std::move(where), "Cannot open it in " + first_path + ": " + reason_of(errno));
            return;
        }
        UniqueFd b{::openat(second_folder, second.c_str(), how)};
        if (b.get() < 0) {
            note(std::move(where), "Cannot open it in " + second_path + ": " + reason_of(errno));
            return;
        }
        auto names_a = names_in(a.get());
        if (!names_a) {
            note(std::move(where), "Cannot read it in " + first_path + ": " + names_a.error().message);
            return;
        }
        auto names_b = names_in(b.get());
        if (!names_b) {
            note(std::move(where), "Cannot read it in " + second_path + ": " + names_b.error().message);
            return;
        }
        levels.push_back(MatchLevel{std::move(a), std::move(b), std::move(*names_a), std::move(*names_b), 0, 0, std::move(where)});
        ++progress.folders;
    }

    void compare_links(int first_folder, const std::string& first, int second_folder, const std::string& second, std::string where) {
        std::array<char, most_file_path_bytes> says_a{};
        std::array<char, most_file_path_bytes> says_b{};
        const ::ssize_t length_a = ::readlinkat(first_folder, first.c_str(), says_a.data(), says_a.size());
        if (length_a < 0) {
            note(std::move(where), "Cannot read the link in " + first_path + ": " + reason_of(errno));
            return;
        }
        const ::ssize_t length_b = ::readlinkat(second_folder, second.c_str(), says_b.data(), says_b.size());
        if (length_b < 0) {
            note(std::move(where), "Cannot read the link in " + second_path + ": " + reason_of(errno));
            return;
        }
        if (std::string_view{says_a.data(), saturate_to<std::size_t>(length_a)} != std::string_view{says_b.data(), saturate_to<std::size_t>(length_b)}) {
            note(std::move(where), "The two links point to different places");
            return;
        }
        ++progress.links;
    }

    void read_a_chunk() {
        MatchingFiles& open = *files;
        const std::size_t length = std::min(buffer.size(), saturate_to<std::size_t>(open.size - open.done));
        if (length != 0) {
            const std::span<char> chunk = std::span<char>{buffer}.first(length);
            if (auto read = detail::read_at<char>(open.first.get(), chunk, open.done); !read) {
                give_up_on_files("Cannot read it in " + first_path + ": " + read.error().message);
                return;
            }
            open.first_sum.update(chunk);
            if (auto read = detail::read_at<char>(open.second.get(), chunk, open.done); !read) {
                give_up_on_files("Cannot read it in " + second_path + ": " + read.error().message);
                return;
            }
            open.second_sum.update(chunk);
            open.done += saturate_to<std::int64_t>(length);
            progress.bytes += length;
            return;
        }
        struct stat a {};
        struct stat b {};
        if (::fstat(open.first.get(), &a) != 0 || ::fstat(open.second.get(), &b) != 0) {
            give_up_on_files("Cannot look at it: " + reason_of(errno));
            return;
        }
        // What was read may be half of one version and half of another, so a sum of it says nothing.
        if (a.st_size != open.size || b.st_size != open.size || !same_time(a.st_mtim, open.first_time) || !same_time(b.st_mtim, open.second_time)) {
            give_up_on_files("It changed while it was being read");
            return;
        }
        if (open.first_sum.digest() != open.second_sum.digest()) {
            give_up_on_files("The contents differ by SHA-256");
            return;
        }
        files.reset();
        ++progress.files;
    }

    void give_up_on_files(std::string why) {
        std::string where = std::move(files->where);
        files.reset();
        note(std::move(where), std::move(why));
    }

    // The next name of the folder on top: on one side only, or in both, and then compared.
    void next_in_level() {
        MatchLevel& level = levels.back();
        const bool more_first = level.first_at < level.first_names.size();
        const bool more_second = level.second_at < level.second_names.size();
        if (!more_first && !more_second) {
            levels.pop_back();
            return;
        }
        if (more_first && (!more_second || level.first_names[level.first_at] < level.second_names[level.second_at])) {
            note(where_inside(level.where, level.first_names[level.first_at]), "Only in " + first_path);
            ++level.first_at;
            return;
        }
        if (more_second && (!more_first || level.second_names[level.second_at] < level.first_names[level.first_at])) {
            note(where_inside(level.where, level.second_names[level.second_at]), "Only in " + second_path);
            ++level.second_at;
            return;
        }
        // Comparing may push a level and move this one, so nothing of it is held past here.
        const std::string name = level.first_names[level.first_at];
        ++level.first_at;
        ++level.second_at;
        std::string where = where_inside(level.where, name);
        const int first = level.first.get();
        const int second = level.second.get();
        compare_items(first, name, second, name, std::move(where));
    }

    // True when all of it is done.
    [[nodiscard]] std::expected<bool, Error> step() {
        if (!started) {
            started = true;
            compare_items(top_first.get(), first_name, top_second.get(), second_name, top_where);
        } else if (files) {
            read_a_chunk();
        } else if (!levels.empty()) {
            next_in_level();
        }
        return !files && levels.empty();
    }
};

void LocalMatchJob::ImplDeleter::operator()(Impl* impl) const noexcept { std::default_delete<Impl>{}(impl); }

LocalMatchJob::LocalMatchJob(std::unique_ptr<Impl, ImplDeleter> impl) noexcept : impl_{std::move(impl)} {}

std::expected<bool, Error> LocalMatchJob::step() { return impl_->step(); }

MatchProgress LocalMatchJob::progress() const noexcept { return impl_->progress; }

std::span<const MatchFinding> LocalMatchJob::findings() const noexcept { return impl_->found; }

std::expected<LocalMatchJob, Error> start_local_match(const std::string& first, const std::string& second) {
    if (!file_path_can_show(first) || !file_path_can_show(second) || first == "/" || second == "/") return make_error(ErrorCode::System, "Those paths cannot be matched");
    std::unique_ptr<LocalMatchJob::Impl, LocalMatchJob::ImplDeleter> impl{std::make_unique<LocalMatchJob::Impl>().release()};
    impl->first_path = first;
    impl->second_path = second;
    impl->first_name = last_name_of(first);
    impl->second_name = last_name_of(second);
    impl->top_where = impl->first_name == impl->second_name ? impl->first_name : impl->first_name + " and " + impl->second_name;
    constexpr int how = O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_CLOEXEC;
    impl->top_first = UniqueFd{::open(parent_of_path(first).c_str(), how)};
    if (impl->top_first.get() < 0) return make_error(ErrorCode::System, "cannot open the folder holding " + first + ": " + reason_of(errno));
    impl->top_second = UniqueFd{::open(parent_of_path(second).c_str(), how)};
    if (impl->top_second.get() < 0) return make_error(ErrorCode::System, "cannot open the folder holding " + second + ": " + reason_of(errno));
    impl->buffer.resize(match_chunk_bytes);
    return LocalMatchJob{std::move(impl)};
}

}  // namespace jgui::inline v001
