// File: src/files/rename.cpp
// Purpose: Gives one thing a new name in its own folder, through that folder's descriptor. RENAME_NOREPLACE makes the kernel refuse a name that is taken. Looking first and renaming after could replace what arrived between the two, so a disk without it is refused.
#include <fcntl.h>
#include <stdio.h>

#include <cerrno>
#include <expected>
#include <string>

#include "file_io.hpp"
#include "folder_names.hpp"
#include "jCppGUI/v001/local_files.hpp"

namespace jgui::inline v001 {

std::expected<void, Error> rename_local_file(const std::string& path, const std::string& new_name) {
    if (!file_path_can_show(path) || path == "/") return make_error(ErrorCode::System, "That path cannot be renamed");
    const std::string folder = parent_of_path(path);
    if (auto checked = check_given_name(folder, new_name); !checked) return std::unexpected(std::move(checked.error()));
    const std::string name{last_name_of(path)};
    const detail::UniqueFd holder{::open(folder.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)};
    if (holder.get() < 0) return make_error(ErrorCode::System, "cannot open the folder holding " + name + ": " + detail::reason_of(errno));
    if (::renameat2(holder.get(), name.c_str(), holder.get(), new_name.c_str(), RENAME_NOREPLACE) == 0) return {};
    if (errno == EEXIST) return make_error(ErrorCode::System, new_name + " is already there, and nothing was replaced");
    if (errno == EINVAL || errno == ENOSYS) return make_error(ErrorCode::System, "this disk cannot promise not to replace a name, so nothing was renamed");
    return make_error(ErrorCode::System, "cannot rename " + name + ": " + detail::reason_of(errno));
}

}  // namespace jgui::inline v001
