// File: src/files/touch.cpp
// Purpose: Sets when something last changed to now. Reached through its folder's descriptor without following a link, and nothing is ever made.
#include <fcntl.h>
#include <sys/stat.h>

#include <cerrno>
#include <expected>
#include <string>

#include "file_io.hpp"
#include "folder_names.hpp"
#include "jCppGUI/v001/local_files.hpp"

namespace jgui::inline v001 {

std::expected<void, Error> touch_local_file(const std::string& path) {
    if (!file_path_can_show(path) || path == "/") return make_error(ErrorCode::System, "That path cannot be touched");
    const std::string name{last_name_of(path)};
    const detail::UniqueFd holder{::open(parent_of_path(path).c_str(), O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_CLOEXEC)};
    if (holder.get() < 0) return make_error(ErrorCode::System, "cannot open the folder holding " + name + ": " + detail::reason_of(errno));
    if (::utimensat(holder.get(), name.c_str(), nullptr, AT_SYMLINK_NOFOLLOW) != 0) return make_error(ErrorCode::System, "cannot touch " + name + ": " + detail::reason_of(errno));
    return {};
}

}  // namespace jgui::inline v001
