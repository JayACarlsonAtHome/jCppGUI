// File: src/jtext/read_file.cpp
// Purpose: Brings a jText file into memory we own, through read_at, for the parser. What stands at the path must be a regular file, checked
//          without following a link before it is opened, and again on what was opened, so a swap in between finds nothing to follow.
//          O_NONBLOCK keeps a pipe swapped in from holding up the open, and it changes nothing for a regular file.
#include <fcntl.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "file_io.hpp"
#include "jCppGUI/v001/jtext.hpp"

namespace jgui::inline v001 {

namespace {

[[nodiscard]] std::unexpected<Error> failure(std::string_view what, const std::string& path, int code) {
    return make_error(ErrorCode::System, std::string{what} + " " + path + ": " + std::generic_category().message(code));
}

[[nodiscard]] std::unexpected<Error> not_regular(const std::string& path) {
    return make_error(ErrorCode::System, "refused " + path + ": it is not a regular file");
}

// The whole of a file, in a string of the right size.
[[nodiscard]] std::expected<std::string, Error> whole_file(const std::string& path, std::size_t most_bytes) {
    if (path.empty() || path.contains('\0')) return make_error(ErrorCode::System, "refused a path that is empty or holds a NUL");
    struct stat looked_at {};
    if (::fstatat(AT_FDCWD, path.c_str(), &looked_at, AT_SYMLINK_NOFOLLOW) != 0) return failure("cannot look at", path, errno);
    if (!S_ISREG(looked_at.st_mode)) return not_regular(path);

    const detail::UniqueFd file{::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)};
    if (file.get() < 0) return failure("cannot open", path, errno);
    struct stat opened {};
    if (::fstat(file.get(), &opened) != 0) return failure("cannot check", path, errno);
    if (!S_ISREG(opened.st_mode)) return not_regular(path);
    if (opened.st_size < 0 || std::cmp_greater(opened.st_size, most_bytes)) {
        return make_error(ErrorCode::SizeLimit, "refused " + path + ": it is over the limit of " + decimal_text(most_bytes, false) + " bytes");
    }

    std::string text(saturate_to<std::size_t>(opened.st_size), '\0');
    if (auto read = detail::read_at<char>(file.get(), std::span<char>{text}, 0); !read) return make_error(ErrorCode::System, "cannot read " + path + ": " + read.error().message);
    return text;
}

}  // namespace

std::expected<JTextFile, Error> read_jtext_file(const std::string& path, const JTextLimits& limits) {
    auto text = whole_file(path, limits.file_bytes);
    if (!text) return std::unexpected(std::move(text.error()));
    return parse_owned_jtext(std::move(*text), limits);
}

}  // namespace jgui::inline v001
