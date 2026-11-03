// File: src/monitor/file_sink.cpp
// Purpose: FileSink, for whichever tool writes through it. A whole new file is written beside the old under a temporary name, with
//          O_CREAT | O_EXCL | O_NOFOLLOW and mode 0600, and renamed over it. What stands at either name must be a regular
//          file or nothing: anything else is refused and left alone. Every failure is an Error for the monitor to keep.
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "file_io.hpp"
#include "jCppGUI/v001/base_directory.hpp"
#include "jCppGUI/v001/jtext_writer.hpp"

namespace jgui::inline v001 {

namespace {

[[nodiscard]] std::unexpected<Error> failure(std::string_view what, const std::string& name, int code) {
    return make_error(ErrorCode::System, std::string{what} + " " + name + ": " + std::generic_category().message(code));
}

enum class Found : std::uint8_t { Nothing, RegularFile };

// What stands at a name in the directory, without following a link. Anything but a regular file or nothing is an Error.
[[nodiscard]] std::expected<Found, Error> look_at(int directory, const std::string& name) {
    struct stat status {};
    if (::fstatat(directory, name.c_str(), &status, AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno == ENOENT) return Found::Nothing;
        return failure("cannot look at", name, errno);
    }
    if (!S_ISREG(status.st_mode)) return make_error(ErrorCode::System, "will not replace " + name + ": it is not a regular file");
    return Found::RegularFile;
}

// Writes the new file under its temporary name. The descriptor closes on the way out, whatever happened.
[[nodiscard]] std::expected<void, Error> write_new_file(int directory, const std::string& name, std::string_view text) {
    const detail::UniqueFd file{::openat(directory, name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, S_IRUSR | S_IWUSR)};
    if (file.get() < 0) return failure("cannot create", name, errno);
    struct stat status {};
    if (::fstat(file.get(), &status) != 0) return failure("cannot check", name, errno);
    if (!S_ISREG(status.st_mode)) return make_error(ErrorCode::System, "will not use " + name + ": it is not a regular file");
    if (auto written = detail::write_at<char>(file.get(), std::span<const char>{text}, 0); !written) {
        return make_error(ErrorCode::System, "cannot write " + name + ": " + written.error().message);
    }
    return {};
}

}  // namespace

FileSink::FileSink() : FileSink{InBaseDirectory{}, default_redraw_file_name} {}

FileSink FileSink::in_base_directory(std::string_view file_name) { return FileSink{InBaseDirectory{}, file_name}; }

FileSink::FileSink(InBaseDirectory, std::string_view file_name) : name_{file_name} {
    if (!detail::plain_file_name_fits(file_name)) {
        refused_ = Error{ErrorCode::System, "a file to save must be a plain name in its directory"};
        return;
    }
    const auto base = application_base_directory();
    if (base) {
        open_directory(*base);
    } else {
        refused_ = Error{base.error().code, "there is no base directory for " + name_ + ": " + base.error().message};
    }
}

FileSink::FileSink(std::string_view directory, std::string_view file_name) : name_{file_name} {
    if (!detail::plain_file_name_fits(file_name)) {
        refused_ = Error{ErrorCode::System, "a file to save must be a plain name in its directory"};
        return;
    }
    open_directory(std::string{directory});
}

void FileSink::open_directory(const std::string& directory) {
    if (directory.empty() || directory.contains('\0')) {
        refused_ = Error{ErrorCode::System, "the directory to save in is not a path"};
        return;
    }
    directory_ = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory_ < 0) refused_ = failure("cannot open the directory", directory, errno).error();
}

FileSink::FileSink(FileSink&& other) noexcept
    : name_{std::move(other.name_)}, directory_{std::exchange(other.directory_, -1)}, refused_{std::move(other.refused_)} {}

FileSink::~FileSink() {
    // A failed close leaves nothing to do: nothing is written through this descriptor.
    if (directory_ >= 0) static_cast<void>(::close(directory_));
}

std::expected<void, Error> FileSink::save(std::string_view text) {
    if (refused_) return std::unexpected(*refused_);
    const std::string temporary = name_ + ".tmp";

    // Checked first, so that nothing is written for a place that would be refused. The rename below could only ever
    // replace a link and never follow one, so a change between this check and that rename can point nothing anywhere.
    if (auto found = look_at(directory_, name_); !found) return std::unexpected(std::move(found.error()));
    auto leftover = look_at(directory_, temporary);
    if (!leftover) return std::unexpected(std::move(leftover.error()));
    // What a run that crashed left behind. O_EXCL below would refuse it for ever.
    if (*leftover == Found::RegularFile && ::unlinkat(directory_, temporary.c_str(), 0) != 0)
        return failure("cannot remove", temporary, errno);

    if (auto written = write_new_file(directory_, temporary, text); !written) {
        // Gone if it was made, and if it was not there is nothing to do.
        static_cast<void>(::unlinkat(directory_, temporary.c_str(), 0));
        return std::unexpected(std::move(written.error()));
    }
    if (::renameat(directory_, temporary.c_str(), directory_, name_.c_str()) != 0) {
        const int code = errno;
        static_cast<void>(::unlinkat(directory_, temporary.c_str(), 0));
        return failure("cannot replace", name_, code);
    }
    return {};
}

}  // namespace jgui::inline v001
