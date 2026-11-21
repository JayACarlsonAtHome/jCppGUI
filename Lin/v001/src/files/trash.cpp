// File: src/files/trash.cpp
// Purpose: Moves a thing to the freedesktop.org trash of its own disk, so a desktop's trash can show it and put it back: the home trash for what is on the same disk as that, and otherwise $top/.Trash/$uid or
//          $top/.Trash-$uid. The info file is made first with O_EXCL, which holds the name, and the thing then moves by one rename, so it is either where it was or wholly in the trash. Nothing here removes
//          anything the person had: the only unlink is of the info file just made, when the move did not happen. Bytes go only through write_at.
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "file_io.hpp"
#include "folder_names.hpp"
#include "jCppGUI/v001/local_files.hpp"

namespace jgui::inline v001 {

namespace {

using detail::reason_of;
using detail::UniqueFd;

inline constexpr std::uint32_t most_names_to_try = 100'000;

// A folder of a trash: made if it is not there, never reached through a link, this user's, and not open to everyone. A group may write to it: on a disk shared with a group, as a setgid top folder makes, those
// people can already change everything else there.
[[nodiscard]] std::expected<UniqueFd, Error> open_own_folder(int holder, const std::string& name) {
    if (::mkdirat(holder, name.c_str(), S_IRWXU) != 0 && errno != EEXIST) return make_error(ErrorCode::System, "cannot make " + name + ": " + reason_of(errno));
    UniqueFd folder{::openat(holder, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)};
    if (folder.get() < 0) return make_error(ErrorCode::System, "cannot open " + name + ": " + reason_of(errno));
    struct stat found {};
    if (::fstat(folder.get(), &found) != 0) return make_error(ErrorCode::System, "cannot look at " + name + ": " + reason_of(errno));
    if (found.st_uid != ::geteuid() || (found.st_mode & S_IWOTH) != 0) return make_error(ErrorCode::System, name + " belongs to someone else or anyone can write to it");
    return folder;
}

struct TrashFolders {
    UniqueFd files;
    UniqueFd info;
};

[[nodiscard]] std::expected<TrashFolders, Error> open_parts(int trash) {
    auto files = open_own_folder(trash, "files");
    if (!files) return std::unexpected(std::move(files.error()));
    auto info = open_own_folder(trash, "info");
    if (!info) return std::unexpected(std::move(info.error()));
    return TrashFolders{std::move(*files), std::move(*info)};
}

// The folder a disk is mounted at, going up from a path whose text holds no links, as a pane's does.
[[nodiscard]] std::expected<std::string, Error> mount_top_of(std::string folder) {
    while (folder != "/") {
        struct statx found {};
        if (::statx(AT_FDCWD, folder.c_str(), AT_SYMLINK_NOFOLLOW | AT_NO_AUTOMOUNT, STATX_BASIC_STATS, &found) != 0) return make_error(ErrorCode::System, "cannot look at " + folder + ": " + reason_of(errno));
        if ((found.stx_attributes_mask & STATX_ATTR_MOUNT_ROOT) == 0) return make_error(ErrorCode::System, "cannot tell where this disk starts");
        if ((found.stx_attributes & STATX_ATTR_MOUNT_ROOT) != 0) return folder;
        folder = parent_of_path(folder);
    }
    return folder;
}

// $top/.Trash/$uid when an administrator has made $top/.Trash as the specification asks: a real folder, sticky. Empty when that cannot be used, which is not a failure.
[[nodiscard]] std::expected<TrashFolders, Error> open_shared_trash(int top, const std::string& uid) {
    const UniqueFd shared{::openat(top, ".Trash", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)};
    if (shared.get() < 0) return make_error(ErrorCode::System, "no shared trash");
    struct stat found {};
    if (::fstat(shared.get(), &found) != 0 || (found.st_mode & S_ISVTX) == 0) return make_error(ErrorCode::System, "the shared trash is not sticky");
    auto own = open_own_folder(shared.get(), uid);
    if (!own) return std::unexpected(std::move(own.error()));
    return open_parts(own->get());
}

struct ChosenTrash {
    TrashFolders folders;
    std::string path_in_info{};
};

[[nodiscard]] std::expected<ChosenTrash, Error> trash_on_the_disk_of(const std::string& path, dev_t device, const std::string& data_home) {
    // The person's own folder may be reached through a link, as on some systems, so this is the one part opened by following one. What is inside it is not.
    std::string so_far;
    std::string_view rest{data_home};
    while (!rest.empty()) {
        const std::size_t slash = rest.find('/', 1);
        so_far.append(rest.substr(0, slash));
        rest = slash == std::string_view::npos ? std::string_view{} : rest.substr(slash);
        static_cast<void>(::mkdir(so_far.c_str(), S_IRWXU));  // already there, or the open below says why not
    }
    const UniqueFd home{::open(data_home.c_str(), O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_CLOEXEC)};
    if (home.get() < 0) return make_error(ErrorCode::System, "cannot open " + data_home + ": " + reason_of(errno));
    struct stat home_found {};
    if (::fstat(home.get(), &home_found) != 0) return make_error(ErrorCode::System, "cannot look at " + data_home + ": " + reason_of(errno));
    if (home_found.st_dev == device) {
        auto trash = open_own_folder(home.get(), "Trash");
        if (!trash) return std::unexpected(std::move(trash.error()));
        auto parts = open_parts(trash->get());
        if (!parts) return std::unexpected(std::move(parts.error()));
        return ChosenTrash{std::move(*parts), path};
    }
    auto top = mount_top_of(parent_of_path(path));
    if (!top) return std::unexpected(std::move(top.error()));
    const UniqueFd top_folder{::open(top->c_str(), O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_CLOEXEC)};
    if (top_folder.get() < 0) return make_error(ErrorCode::System, "cannot open " + *top + ": " + reason_of(errno));
    struct stat top_found {};
    if (::fstat(top_folder.get(), &top_found) != 0 || top_found.st_dev != device) return make_error(ErrorCode::System, "cannot find the top of this disk");
    // On such a disk the path is written from its top, so the disk can be mounted elsewhere later.
    std::string from_top = path.substr(*top == "/" ? 1 : top->size() + 1);
    const std::string uid = decimal_text(std::uint64_t{::geteuid()}, false);
    auto shared = open_shared_trash(top_folder.get(), uid);
    if (shared) return ChosenTrash{std::move(*shared), std::move(from_top)};
    auto own = open_own_folder(top_folder.get(), ".Trash-" + uid);
    if (!own) return make_error(ErrorCode::System, "this disk has no trash that can be used, so it was left where it is (" + own.error().text() + " in " + *top + ")");
    auto parts = open_parts(own->get());
    if (!parts) return std::unexpected(std::move(parts.error()));
    return ChosenTrash{std::move(*parts), std::move(from_top)};
}

[[nodiscard]] std::expected<TrashDate, Error> date_now() {
    const ::time_t now = ::time(nullptr);
    tm local{};
    if (::localtime_r(&now, &local) == nullptr) return make_error(ErrorCode::System, "cannot tell the time");
    return TrashDate{std::int64_t{local.tm_year} + 1900, std::int64_t{local.tm_mon} + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec};
}

// Where a name might be taken without the system saying so, as on a network disk that has no way to refuse in the rename, the name is looked at first. The trash is this user's alone, so no one else can take it between.
[[nodiscard]] int rename_without_replacing(int from_folder, const char* from, int into_folder, const char* into) noexcept {
    if (::renameat2(from_folder, from, into_folder, into, RENAME_NOREPLACE) == 0) return 0;
    if (errno != EINVAL && errno != ENOSYS) return errno;
    struct stat found {};
    if (::fstatat(into_folder, into, &found, AT_SYMLINK_NOFOLLOW) == 0) return EEXIST;
    return ::renameat(from_folder, from, into_folder, into) == 0 ? 0 : errno;
}

}  // namespace

std::expected<void, Error> move_to_trash_under(const std::string& path, const std::string& data_home) {
    if (!file_path_can_show(path) || path == "/") return make_error(ErrorCode::System, "That path cannot be deleted");
    if (!file_path_can_show(data_home) || data_home == "/") return make_error(ErrorCode::System, "The folder for the trash cannot be used");
    const std::string home_trash = child_of_path(data_home, "Trash");
    if (path_is_in_a_trash(path, home_trash)) return make_error(ErrorCode::System, "that is already in a trash, and its own desktop trash can empty it");
    if (path_is_inside(home_trash, path)) return make_error(ErrorCode::System, "the trash is inside that, so it cannot go there");
    const std::string name{last_name_of(path)};
    const UniqueFd holder{::open(parent_of_path(path).c_str(), O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_CLOEXEC)};
    if (holder.get() < 0) return make_error(ErrorCode::System, "cannot open the folder holding " + name + ": " + reason_of(errno));
    struct stat found {};
    if (::fstatat(holder.get(), name.c_str(), &found, AT_SYMLINK_NOFOLLOW) != 0) return make_error(ErrorCode::System, "cannot look at " + name + ": " + reason_of(errno));
    auto trash = trash_on_the_disk_of(path, found.st_dev, data_home);
    if (!trash) return std::unexpected(std::move(trash.error()));
    auto date = date_now();
    if (!date) return std::unexpected(std::move(date.error()));
    const std::string info_text = trash_info_text(trash->path_in_info, *date);
    for (std::uint32_t attempt = 0; attempt < most_names_to_try; ++attempt) {
        const std::string try_name = trash_name_to_try(name, attempt);
        const std::string info_name = try_name + std::string{trash_info_suffix};
        const UniqueFd info{::openat(trash->folders.info.get(), info_name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, S_IRUSR | S_IWUSR)};
        if (info.get() < 0) {
            if (errno == EEXIST) continue;
            return make_error(ErrorCode::System, "cannot write to the trash: " + reason_of(errno));
        }
        const auto forget_info = [&] { static_cast<void>(::unlinkat(trash->folders.info.get(), info_name.c_str(), 0)); };
        // It must be on the disk before the thing moves, or a crash could leave something in the trash that cannot be put back.
        if (auto wrote = detail::write_at<char>(info.get(), std::span<const char>{info_text}, 0); !wrote || ::fsync(info.get()) != 0) {
            forget_info();
            return make_error(ErrorCode::System, "cannot write to the trash");
        }
        const int moved = rename_without_replacing(holder.get(), name.c_str(), trash->folders.files.get(), try_name.c_str());
        if (moved == 0) return {};
        forget_info();
        if (moved == EEXIST) continue;  // something in the trash with no info file holds that name
        if (moved == EXDEV) return make_error(ErrorCode::System, "its trash is on another disk, so it was left where it is");
        return make_error(ErrorCode::System, "cannot move " + name + " to the trash: " + reason_of(moved));
    }
    return make_error(ErrorCode::System, "the trash holds too many things of that name");
}

std::expected<void, Error> move_to_trash(const std::string& path) {
    const char* data = ::secure_getenv("XDG_DATA_HOME");
    if (data != nullptr && data[0] == '/') return move_to_trash_under(path, data);
    const char* home = ::secure_getenv("HOME");
    if (home == nullptr || home[0] != '/') return make_error(ErrorCode::System, "HOME is not set, so there is no trash to use, and it was left where it is");
    return move_to_trash_under(path, std::string{home} + "/.local/share");
}

}  // namespace jgui::inline v001
