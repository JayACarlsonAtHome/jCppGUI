// File: src/files/touch.cpp
// Purpose: Makes an empty file, or sets when what already has that name last changed to now. Reached through its folder's descriptor, and no link is followed. O_EXCL makes "is it there" and "make it" one act, so there is no moment in which something else could slip in.
#include <fcntl.h>
#include <sys/stat.h>

#include <cerrno>
#include <expected>
#include <string>

#include "file_io.hpp"
#include "folder_names.hpp"
#include "jCppGUI/v001/local_files.hpp"

namespace jgui::inline v001 {

std::expected<TouchOutcome, Error> touch_local_file(const std::string& folder, const std::string& name) {
    if (auto checked = check_given_name(folder, name); !checked) return std::unexpected(std::move(checked.error()));
    const detail::UniqueFd holder{::open(folder.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)};
    if (holder.get() < 0) return make_error(ErrorCode::System, "cannot open the folder to put " + name + " in: " + detail::reason_of(errno));
    constexpr mode_t usual_for_a_new_file = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;  // less what the person's umask takes away, as touch does
    const detail::UniqueFd made{::openat(holder.get(), name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, usual_for_a_new_file)};
    if (made.get() >= 0) return TouchOutcome::Made;
    if (errno != EEXIST) return make_error(ErrorCode::System, "cannot make " + name + ": " + detail::reason_of(errno));
    if (::utimensat(holder.get(), name.c_str(), nullptr, AT_SYMLINK_NOFOLLOW) != 0) return make_error(ErrorCode::System, "cannot touch " + name + ": " + detail::reason_of(errno));
    return TouchOutcome::TimeSet;
}

}  // namespace jgui::inline v001
