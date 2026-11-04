// File: src/files/copy_job.cpp
// Purpose: Copies on this machine's disks. Everything goes through descriptors opened without following a link, and everything made is made with O_EXCL, mkdirat or symlinkat, each of
//          which refuses a name that is taken, so nothing that was there can be replaced, even by a swap in between. Each file written is flushed, read back from both sides, and counted
//          only if the two SHA-256 sums match. The one thing removed is a file this job made itself and did not finish or that did not match. An original is only ever read. Bytes go
//          only through read_at and write_at.
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "c_resource.hpp"
#include "file_io.hpp"
#include "jCppGUI/v001/local_files.hpp"
#include "jCppGUI/v001/sha256.hpp"

namespace jgui::inline v001 {

namespace {

using Directory = detail::CResource<DIR, ::closedir>;
using detail::UniqueFd;

inline constexpr std::size_t copy_chunk_bytes = std::size_t{1024} * 1024;
inline constexpr std::int64_t copy_flush_bytes = std::int64_t{16} * 1024 * 1024;
inline constexpr std::size_t most_copy_depth = 128;  // each level holds two descriptors open
inline constexpr mode_t permission_bits = S_IRWXU | S_IRWXG | S_IRWXO;  // and so no set-user-ID, set-group-ID or sticky bit

// A name on a disk can be anything, and a message is text that must show.
[[nodiscard]] std::string name_for_message(std::string_view name) { return file_name_can_show(name) ? std::string{name} : std::string{"something whose name cannot be shown"}; }

[[nodiscard]] Error failure(std::string_view what, std::string_view name, int code) { return Error{ErrorCode::System, std::string{what} + " " + name_for_message(name) + ": " + std::generic_category().message(code)}; }

// One folder being copied: where its contents come from, where they go, and what the new one should end up like.
struct Level {
    Directory listing;
    UniqueFd into;
    mode_t mode{};
    std::array<timespec, 2> times{};
};

// The file being copied now, in the folder a descriptor says.
struct OpenFile {
    UniqueFd source;
    UniqueFd target;
    int folder{-1};
    std::string name{};
    std::int64_t size{};
    std::int64_t done{};
    mode_t mode{};
    std::array<timespec, 2> times{};
    std::int64_t flushed{};
    // Once it is all written: the copy opened afresh, how far both sides have been read back, and what each has summed to.
    UniqueFd read_back{};
    std::int64_t checked{};
    bool checking{};
    Sha256 original_sum{};
    Sha256 copy_sum{};
};

}  // namespace

struct LocalCopyJob::Impl {
    UniqueFd top_from{};
    UniqueFd top_into{};
    std::string top_name{};
    dev_t into_device{};
    ino_t into_inode{};
    std::vector<Level> levels{};
    std::optional<OpenFile> file{};
    std::vector<char> buffer{};
    CopyProgress progress{};
    bool started{};

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    // A file not finished is not left to look like a copy. This job made it with O_EXCL, under this name, in this folder.
    ~Impl() {
        if (file) static_cast<void>(::unlinkat(file->folder, file->name.c_str(), 0));
    }

    [[nodiscard]] std::expected<void, Error> begin_item(int from_folder, int into_folder, const std::string& name) {
        struct stat looked_at {};
        if (::fstatat(from_folder, name.c_str(), &looked_at, AT_SYMLINK_NOFOLLOW) != 0) return std::unexpected(failure("cannot look at", name, errno));
        if (S_ISREG(looked_at.st_mode)) return begin_file(from_folder, into_folder, name);
        if (S_ISDIR(looked_at.st_mode)) return begin_folder(from_folder, into_folder, name, looked_at);
        if (S_ISLNK(looked_at.st_mode)) return copy_link(from_folder, into_folder, name);
        ++progress.not_copied;
        return {};
    }

    [[nodiscard]] std::expected<void, Error> begin_file(int from_folder, int into_folder, const std::string& name) {
        UniqueFd source{::openat(from_folder, name.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)};
        if (source.get() < 0) return std::unexpected(failure("cannot open", name, errno));
        struct stat opened {};
        if (::fstat(source.get(), &opened) != 0) return std::unexpected(failure("cannot look at", name, errno));
        if (!S_ISREG(opened.st_mode)) {
            ++progress.not_copied;
            return {};
        }
        // Private until it is whole, and then what the original has.
        UniqueFd target{::openat(into_folder, name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, S_IRUSR | S_IWUSR)};
        if (target.get() < 0) return std::unexpected(failure("cannot make", name, errno));
        file.emplace(OpenFile{std::move(source), std::move(target), into_folder, name, saturate_to<std::int64_t>(opened.st_size), 0, opened.st_mode & permission_bits, {opened.st_atim, opened.st_mtim}});
        return {};
    }

    [[nodiscard]] std::expected<void, Error> begin_folder(int from_folder, int into_folder, const std::string& name, const struct stat& looked_at) {
        if (looked_at.st_dev == into_device && looked_at.st_ino == into_inode) return std::unexpected(Error{ErrorCode::System, "A folder cannot go inside itself"});
        if (levels.size() >= most_copy_depth) return std::unexpected(failure("too many folders deep at", name, ENAMETOOLONG));
        const int from = ::openat(from_folder, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        if (from < 0) return std::unexpected(failure("cannot open", name, errno));
        Directory listing{::fdopendir(from)};
        if (!listing) {
            const int why = errno;
            static_cast<void>(::close(from));
            return std::unexpected(failure("cannot read", name, why));
        }
        // Open to this user while it is filled, and what the original has at the end, so a read-only folder can still be copied.
        if (::mkdirat(into_folder, name.c_str(), S_IRWXU) != 0) return std::unexpected(failure("cannot make", name, errno));
        UniqueFd into{::openat(into_folder, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
        if (into.get() < 0) return std::unexpected(failure("cannot open", name, errno));
        levels.push_back(Level{std::move(listing), std::move(into), looked_at.st_mode & permission_bits, {looked_at.st_atim, looked_at.st_mtim}});
        ++progress.folders;
        return {};
    }

    // As a link, saying what it says, and what it points to is never touched.
    [[nodiscard]] std::expected<void, Error> copy_link(int from_folder, int into_folder, const std::string& name) {
        std::array<char, most_file_path_bytes> says{};
        const ::ssize_t length = ::readlinkat(from_folder, name.c_str(), says.data(), says.size() - 1);
        if (length < 0) return std::unexpected(failure("cannot read the link", name, errno));
        says.at(saturate_to<std::size_t>(length)) = '\0';
        if (::symlinkat(says.data(), into_folder, name.c_str()) != 0) return std::unexpected(failure("cannot make", name, errno));
        ++progress.links;
        return {};
    }

    [[nodiscard]] std::expected<void, Error> copy_a_chunk() {
        OpenFile& open = *file;
        const std::size_t length = std::min(buffer.size(), saturate_to<std::size_t>(open.size - open.done));
        if (length == 0) return begin_check();
        const std::span<char> chunk = std::span<char>{buffer}.first(length);
        if (auto read = detail::read_at<char>(open.source.get(), chunk, open.done); !read) return std::unexpected(Error{ErrorCode::System, "cannot read " + name_for_message(open.name) + ": " + read.error().message});
        if (auto wrote = detail::write_at<char>(open.target.get(), std::span<const char>{chunk}, open.done); !wrote) return std::unexpected(Error{ErrorCode::System, "cannot write " + name_for_message(open.name) + ": " + wrote.error().message});
        open.done += saturate_to<std::int64_t>(length);
        progress.bytes += length;
        // A little at a time, because one flush of a large file at the end holds up the window for seconds, and Escape with it.
        if (open.done - open.flushed >= copy_flush_bytes) {
            if (::fdatasync(open.target.get()) != 0) return std::unexpected(failure("cannot write", open.name, errno));
            open.flushed = open.done;
        }
        return {};
    }

    // To the disk first, and out of the cache where the system lets it, or reading it back would only read back what was just written to memory.
    [[nodiscard]] std::expected<void, Error> begin_check() {
        OpenFile& open = *file;
        if (::fsync(open.target.get()) != 0) return std::unexpected(failure("cannot write", open.name, errno));
        static_cast<void>(::posix_fadvise(open.target.get(), 0, 0, POSIX_FADV_DONTNEED));  // only advice: a system may keep it, and the read back is still right
        open.read_back = UniqueFd{::openat(open.folder, open.name.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)};
        if (open.read_back.get() < 0) return std::unexpected(failure("cannot read back", open.name, errno));
        struct stat written {};
        struct stat reading {};
        if (::fstat(open.target.get(), &written) != 0 || ::fstat(open.read_back.get(), &reading) != 0) return std::unexpected(failure("cannot look at", open.name, errno));
        if (written.st_dev != reading.st_dev || written.st_ino != reading.st_ino) {
            // What has this name now is not what this job made, so it is not this job's to remove.
            Error why{ErrorCode::System, "something else took the name of " + name_for_message(open.name) + " during the copy, and was left as it is"};
            file.reset();
            return std::unexpected(std::move(why));
        }
        open.checking = true;
        return {};
    }

    [[nodiscard]] std::expected<void, Error> check_a_chunk() {
        OpenFile& open = *file;
        const std::size_t length = std::min(buffer.size(), saturate_to<std::size_t>(open.size - open.checked));
        if (length != 0) {
            const std::span<char> chunk = std::span<char>{buffer}.first(length);
            if (auto read = detail::read_at<char>(open.source.get(), chunk, open.checked); !read) return std::unexpected(Error{ErrorCode::System, "cannot read back " + name_for_message(open.name) + ": " + read.error().message});
            open.original_sum.update(chunk);
            if (auto read = detail::read_at<char>(open.read_back.get(), chunk, open.checked); !read) return std::unexpected(Error{ErrorCode::System, "cannot read back the copy of " + name_for_message(open.name) + ": " + read.error().message});
            open.copy_sum.update(chunk);
            open.checked += saturate_to<std::int64_t>(length);
            progress.bytes_checked += length;
            return {};
        }
        // Each side was read only as far as the size it had at the start, so one grown since is caught by its size.
        struct stat original {};
        struct stat copy {};
        if (::fstat(open.source.get(), &original) != 0 || ::fstat(open.read_back.get(), &copy) != 0) return std::unexpected(failure("cannot look at", open.name, errno));
        if (original.st_size != open.size || copy.st_size != open.size || open.original_sum.digest() != open.copy_sum.digest()) return std::unexpected(Error{ErrorCode::System, name_for_message(open.name) + " did not match its original by SHA-256, so that copy of it was removed"});
        // Private until it has matched, and only then what the original has.
        if (::fchmod(open.target.get(), open.mode) != 0) return std::unexpected(failure("cannot set the permissions of", open.name, errno));
        if (::futimens(open.target.get(), open.times.data()) != 0) return std::unexpected(failure("cannot set the time of", open.name, errno));
        file.reset();
        ++progress.files;
        return {};
    }

    [[nodiscard]] std::expected<void, Error> finish_level() {
        const Level& level = levels.back();
        // The time last, because filling it and changing its permissions each move it.
        if (::fchmod(level.into.get(), level.mode) != 0) return std::unexpected(failure("cannot set the permissions of a folder in", top_name, errno));
        if (::futimens(level.into.get(), level.times.data()) != 0) return std::unexpected(failure("cannot set the time of a folder in", top_name, errno));
        levels.pop_back();
        return {};
    }

    // True when all of it is done.
    [[nodiscard]] std::expected<bool, Error> step() {
        if (!started) {
            started = true;
            if (auto begun = begin_item(top_from.get(), top_into.get(), top_name); !begun) return std::unexpected(std::move(begun.error()));
            return !file && levels.empty();
        }
        if (file) {
            if (auto went = file->checking ? check_a_chunk() : copy_a_chunk(); !went) return std::unexpected(std::move(went.error()));
            return !file && levels.empty();
        }
        if (levels.empty()) return true;
        errno = 0;
        const dirent* found = ::readdir(levels.back().listing.get());
        if (found == nullptr) {
            if (errno != 0) return std::unexpected(failure("cannot read a folder in", top_name, errno));
            if (auto finished = finish_level(); !finished) return std::unexpected(std::move(finished.error()));
            return levels.empty();
        }
        auto name = detail::string_from_c(found->d_name, most_file_name_bytes);
        if (!name) return std::unexpected(std::move(name.error()));
        if (*name == "." || *name == "..") return false;
        const Level& level = levels.back();
        if (auto begun = begin_item(::dirfd(level.listing.get()), level.into.get(), *name); !begun) return std::unexpected(std::move(begun.error()));
        return false;
    }
};

void LocalCopyJob::ImplDeleter::operator()(Impl* impl) const noexcept { std::default_delete<Impl>{}(impl); }

LocalCopyJob::LocalCopyJob(std::unique_ptr<Impl, ImplDeleter> impl) noexcept : impl_{std::move(impl)} {}

std::expected<bool, Error> LocalCopyJob::step() { return impl_->step(); }

CopyProgress LocalCopyJob::progress() const noexcept { return impl_->progress; }

std::expected<LocalCopyJob, Error> start_local_copy(const std::string& from, const std::string& into) {
    if (auto checked = check_transfer(from, into); !checked) return std::unexpected(std::move(checked.error()));
    std::unique_ptr<LocalCopyJob::Impl, LocalCopyJob::ImplDeleter> impl{std::make_unique<LocalCopyJob::Impl>().release()};
    impl->top_name = last_name_of(from);
    impl->top_from = UniqueFd{::open(parent_of_path(from).c_str(), O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_CLOEXEC)};
    if (impl->top_from.get() < 0) return std::unexpected(failure("cannot open the folder holding", impl->top_name, errno));
    impl->top_into = UniqueFd{::open(into.c_str(), O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_CLOEXEC)};
    if (impl->top_into.get() < 0) return std::unexpected(failure("cannot open", last_name_of(into), errno));
    struct stat target {};
    if (::fstat(impl->top_into.get(), &target) != 0) return std::unexpected(failure("cannot look at", last_name_of(into), errno));
    impl->into_device = target.st_dev;
    impl->into_inode = target.st_ino;
    struct stat there {};
    if (::fstatat(impl->top_into.get(), impl->top_name.c_str(), &there, AT_SYMLINK_NOFOLLOW) == 0) return make_error(ErrorCode::System, name_for_message(impl->top_name) + " is already in " + into + ", and nothing was changed");
    if (errno != ENOENT) return std::unexpected(failure("cannot look at", impl->top_name, errno));
    impl->buffer.resize(copy_chunk_bytes);
    return LocalCopyJob{std::move(impl)};
}

}  // namespace jgui::inline v001
