// File: src/files/read_directory.cpp
// Purpose: Asks the system what a folder holds. The path is opened once, without following a link at its end, and each thing in it is looked at through
//          that descriptor, also without following a link, so nothing a name points to is ever opened. It only reads: nothing here changes a disk.
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "c_resource.hpp"
#include "jCppGUI/v001/datagrid.hpp"
#include "jCppGUI/v001/local_files.hpp"

namespace jgui::inline v001 {

namespace {

using Directory = detail::CResource<DIR, ::closedir>;

[[nodiscard]] std::unexpected<Error> failure(std::string_view what, const std::string& path, int code) {
    return make_error(ErrorCode::System, std::string{what} + " " + path + ": " + std::generic_category().message(code));
}

[[nodiscard]] constexpr FileKind kind_of(mode_t mode) noexcept {
    if (S_ISDIR(mode)) return FileKind::Folder;
    if (S_ISREG(mode)) return FileKind::File;
    if (S_ISLNK(mode)) return FileKind::Link;
    return FileKind::Other;
}

// A thing that went between being listed and being looked at, or that may not be looked at, stays in the list as Unknown.
[[nodiscard]] FileEntry entry_for(int folder, std::string name) {
    FileEntry entry{.name = std::move(name)};
    struct stat looked_at {};
    if (::fstatat(folder, entry.name.c_str(), &looked_at, AT_SYMLINK_NOFOLLOW) != 0) return entry;
    entry.kind = kind_of(looked_at.st_mode);
    if (entry.kind == FileKind::File) entry.size = FileBytes{true, saturate_to<std::uint64_t>(looked_at.st_size)};
    entry.modified = FileTime{true, std::int64_t{looked_at.st_mtim.tv_sec}};
    return entry;
}

}  // namespace

std::expected<DirectoryListing, Error> read_directory(const std::string& path) {
    if (path.empty() || path.contains('\0')) return make_error(ErrorCode::System, "refused a path that is empty or holds a NUL");
    // O_NONBLOCK keeps a pipe swapped in from holding up the open, though O_DIRECTORY then refuses it.
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (descriptor < 0) return failure("cannot open", path, errno);
    const Directory directory{::fdopendir(descriptor)};
    if (!directory) {
        const int why = errno;
        static_cast<void>(::close(descriptor));
        return failure("cannot read", path, why);
    }

    DirectoryListing listing;
    while (true) {
        errno = 0;
        const dirent* found = ::readdir(directory.get());
        if (found == nullptr) {
            if (errno != 0) return failure("cannot read", path, errno);
            break;
        }
        auto name = detail::string_from_c(found->d_name, most_file_name_bytes);
        if (!name) {
            ++listing.left_out;
            continue;
        }
        if (*name == "." || *name == "..") continue;
        if (!file_name_can_show(*name)) {
            ++listing.left_out;
            continue;
        }
        if (listing.entries.size() == max_grid_rows) return make_error(ErrorCode::SizeLimit, "refused " + path + ": it holds more than a pane can");
        listing.entries.push_back(entry_for(descriptor, std::move(*name)));
    }
    sort_file_entries(listing.entries);
    return listing;
}

std::expected<std::string, Error> starting_folder(const std::string& path) {
    std::error_code why;
    const std::filesystem::path resolved = std::filesystem::canonical(path.empty() ? std::filesystem::path{"."} : std::filesystem::path{path}, why);
    if (why) return failure("cannot find", path, why.value());
    const bool folder = std::filesystem::is_directory(resolved, why);
    if (why) return failure("cannot look at", path, why.value());
    if (!folder) return make_error(ErrorCode::System, "refused " + path + ": it is not a folder");
    std::string text = resolved.string();
    if (!file_path_can_show(text)) return make_error(ErrorCode::InvalidUtf8, "refused a folder whose path cannot be shown");
    return text;
}

}  // namespace jgui::inline v001
