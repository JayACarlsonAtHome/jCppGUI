// File: src/files/count_job.cpp
// Purpose: Counts what deleting something would take to the trash, for the person being asked. It changes nothing, and walks through descriptors opened without following a link, so a link counts as itself.
//          It refuses another mounted disk inside, because moving a folder would take that with it, and anything it cannot look at, because a number that may be short must not be shown.
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "file_io.hpp"
#include "folder_names.hpp"
#include "jCppGUI/v001/local_files.hpp"

namespace jgui::inline v001 {

namespace {

using detail::reason_of;
using detail::UniqueFd;

inline constexpr std::size_t most_count_depth = 128;  // each level holds a descriptor open

// A name on a disk can be anything, and what is said of it must show.
[[nodiscard]] std::string where_inside(std::string_view where, std::string_view name) {
    std::string out{where};
    out.push_back('/');
    out.append(file_name_can_show(name) ? name : std::string_view{"(a name that cannot be shown)"});
    return out;
}

// True for the top of any mount, bind mounts of the same disk included, which a look at the device alone would miss.
[[nodiscard]] std::expected<bool, Error> is_a_mount_point(int folder, const std::string& name, const std::string& where) {
    struct statx found {};
    if (::statx(folder, name.c_str(), AT_SYMLINK_NOFOLLOW | AT_NO_AUTOMOUNT, STATX_BASIC_STATS, &found) != 0) return make_error(ErrorCode::System, "cannot look at " + where + ": " + reason_of(errno));
    if ((found.stx_attributes_mask & STATX_ATTR_MOUNT_ROOT) == 0) return make_error(ErrorCode::System, "cannot tell whether " + where + " is another disk");
    return (found.stx_attributes & STATX_ATTR_MOUNT_ROOT) != 0;
}

struct WalkLevel {
    UniqueFd folder;
    std::vector<std::string> names{};
    std::size_t at{};
    std::string where{};
};

}  // namespace

struct LocalCountJob::Impl {
    UniqueFd top_folder{};
    std::string top_name{};
    std::vector<WalkLevel> levels{};
    DeleteCount seen{};
    dev_t device{};
    bool started{};

    [[nodiscard]] std::expected<void, Error> visit(int folder, const std::string& name, std::string where) {
        struct stat found {};
        if (::fstatat(folder, name.c_str(), &found, AT_SYMLINK_NOFOLLOW) != 0) return make_error(ErrorCode::System, "cannot look at " + where + ": " + reason_of(errno));
        if (S_ISDIR(found.st_mode)) return enter(folder, name, std::move(where), found.st_dev);
        ++seen.files;
        if (S_ISREG(found.st_mode)) seen.bytes += saturate_to<std::uint64_t>(found.st_size);
        return {};
    }

    [[nodiscard]] std::expected<void, Error> enter(int folder, const std::string& name, std::string where, dev_t its_device) {
        if (levels.empty()) device = its_device;
        auto mount = is_a_mount_point(folder, name, where);
        if (!mount) return std::unexpected(std::move(mount.error()));
        if (*mount || its_device != device) return make_error(ErrorCode::System, where + " is another mounted disk, and deleting does not go into one");
        if (levels.size() >= most_count_depth) return make_error(ErrorCode::SizeLimit, where + " is too many folders deep");
        UniqueFd opened{::openat(folder, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)};
        if (opened.get() < 0) return make_error(ErrorCode::System, "cannot open " + where + ": " + reason_of(errno));
        auto names = detail::names_in(opened.get());
        if (!names) return make_error(ErrorCode::System, "cannot read " + where + ": " + names.error().text());
        levels.push_back(WalkLevel{std::move(opened), std::move(*names), 0, std::move(where)});
        ++seen.folders;
        return {};
    }

    // True when all of it is done.
    [[nodiscard]] std::expected<bool, Error> step() {
        std::expected<void, Error> went{};
        if (!started) {
            started = true;
            went = visit(top_folder.get(), top_name, top_name);
        } else if (!levels.empty()) {
            WalkLevel& level = levels.back();
            if (level.at == level.names.size()) {
                levels.pop_back();
            } else {
                // Visiting may add a level and move this one, so nothing of it is held past here.
                const std::string name = level.names[level.at];
                ++level.at;
                std::string where = where_inside(level.where, name);
                const int folder = level.folder.get();
                went = visit(folder, name, std::move(where));
            }
        }
        if (!went) return std::unexpected(std::move(went.error()));
        return levels.empty();
    }
};

void LocalCountJob::ImplDeleter::operator()(Impl* impl) const noexcept { std::default_delete<Impl>{}(impl); }

LocalCountJob::LocalCountJob(std::unique_ptr<Impl, ImplDeleter> impl) noexcept : impl_{std::move(impl)} {}

std::expected<bool, Error> LocalCountJob::step() { return impl_->step(); }

DeleteCount LocalCountJob::counted() const noexcept { return impl_->seen; }

std::expected<LocalCountJob, Error> start_local_count(const std::string& path) {
    if (!file_path_can_show(path) || path == "/") return make_error(ErrorCode::System, "That path cannot be deleted");
    std::unique_ptr<LocalCountJob::Impl, LocalCountJob::ImplDeleter> impl{std::make_unique<LocalCountJob::Impl>().release()};
    impl->top_name = last_name_of(path);
    impl->top_folder = UniqueFd{::open(parent_of_path(path).c_str(), O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_CLOEXEC)};
    if (impl->top_folder.get() < 0) return make_error(ErrorCode::System, "cannot open the folder holding " + path + ": " + reason_of(errno));
    return LocalCountJob{std::move(impl)};
}

}  // namespace jgui::inline v001
