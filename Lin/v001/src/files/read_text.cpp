// File: src/files/read_text.cpp
// Purpose: Reads a whole file to diff it. It must be a regular file, reached without following a link at its own name, and within the limit, so a pipe cannot hang the window and a huge file cannot fill the
//          memory. Bytes go only through read_at.
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <expected>
#include <span>
#include <string>

#include "file_io.hpp"
#include "folder_names.hpp"
#include "jCppGUI/v001/line_diff.hpp"
#include "jCppGUI/v001/local_files.hpp"

namespace jgui::inline v001 {

std::expected<std::string, Error> read_text_file(const std::string& path) {
    if (!file_path_can_show(path)) return make_error(ErrorCode::System, "That path cannot be used");
    const std::string name{last_name_of(path)};
    const detail::UniqueFd file{::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)};
    if (file.get() < 0) return make_error(ErrorCode::System, "cannot open " + name + ": " + detail::reason_of(errno));
    struct stat before {};
    if (::fstat(file.get(), &before) != 0) return make_error(ErrorCode::System, "cannot look at " + name + ": " + detail::reason_of(errno));
    if (!S_ISREG(before.st_mode)) return make_error(ErrorCode::System, name + " is not a file");
    if (saturate_to<std::size_t>(before.st_size) > most_diff_file_bytes) return make_error(ErrorCode::SizeLimit, name + " is over 16 MiB, the most that can be diffed");
    std::string text(saturate_to<std::size_t>(before.st_size), '\0');
    if (auto read = detail::read_at<char>(file.get(), std::span<char>{text}, 0); !read) return make_error(ErrorCode::System, "cannot read " + name + ": " + read.error().message);
    struct stat after {};
    if (::fstat(file.get(), &after) != 0) return make_error(ErrorCode::System, "cannot look at " + name + ": " + detail::reason_of(errno));
    if (after.st_size != before.st_size || after.st_mtim.tv_sec != before.st_mtim.tv_sec || after.st_mtim.tv_nsec != before.st_mtim.tv_nsec) return make_error(ErrorCode::System, name + " changed while it was being read");
    return text;
}

}  // namespace jgui::inline v001
