// File: src/files/save_picture.cpp
// Purpose: Writes a screenshot to a new file. O_EXCL means it can never replace anything, and the only unlink is of the file just made here, when writing it failed. Bytes go only through write_at.
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <utility>

#include "file_io.hpp"
#include "folder_names.hpp"
#include "jCppGUI/v001/local_files.hpp"

namespace jgui::inline v001 {

std::expected<std::string, Error> local_picture_folder() {
    const char* home = ::secure_getenv("HOME");
    if (home == nullptr || home[0] != '/') return make_error(ErrorCode::System, "HOME is not set, so there is no ScreenShots folder to use");
    return child_of_path(home, "ScreenShots");
}

std::expected<std::string, Error> save_local_picture(const std::string& folder, const std::string& name, std::span<const std::uint8_t> png) {
    if (auto checked = check_given_name(folder, name); !checked) return std::unexpected(std::move(checked.error()));
    if (::mkdir(folder.c_str(), S_IRWXU) != 0 && errno != EEXIST) return make_error(ErrorCode::System, "cannot make " + folder + ": " + detail::reason_of(errno));
    // A person may have linked this folder to another disk, so a link at its own name is followed. The file itself never goes through one.
    const detail::UniqueFd holder{::open(folder.c_str(), O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_CLOEXEC)};
    if (holder.get() < 0) return make_error(ErrorCode::System, "cannot open " + folder + ": " + detail::reason_of(errno));
    const detail::UniqueFd file{::openat(holder.get(), name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, S_IRUSR | S_IWUSR)};
    if (file.get() < 0) {
        if (errno == EEXIST) return make_error(ErrorCode::System, name + " is already there, and nothing was replaced");
        return make_error(ErrorCode::System, "cannot make " + name + ": " + detail::reason_of(errno));
    }
    auto wrote = detail::write_at<std::uint8_t>(file.get(), png, 0);
    if (wrote && ::fsync(file.get()) != 0) wrote = make_error(ErrorCode::System, detail::reason_of(errno));
    if (!wrote) {
        static_cast<void>(::unlinkat(holder.get(), name.c_str(), 0));
        return make_error(ErrorCode::System, "cannot write " + name + ": " + wrote.error().text());
    }
    return child_of_path(folder, name);
}

}  // namespace jgui::inline v001
