// File: src/wayland/untrusted.cpp
// Purpose: Loads a compositor-supplied keymap and creates window buffer files: the parts of the checks
//          that need the OS and xkbcommon.
#include "untrusted.hpp"

#include <fcntl.h>
#include <sys/mman.h>  // for memfd_create
#include <sys/stat.h>
#include <sys/vfs.h>

#include <cerrno>
#include <span>
#include <vector>

#include "file_io.hpp"

namespace jgui::inline v001::detail {

XkbContext make_keymap_context() noexcept {
    return XkbContext{xkb_context_new(static_cast<xkb_context_flags>(XKB_CONTEXT_NO_DEFAULT_INCLUDES | XKB_CONTEXT_NO_ENVIRONMENT_NAMES))};
}

std::expected<UniqueFd, Error> create_buffer_file(std::size_t bytes) {
    UniqueFd fd{::memfd_create("jcppgui-frame", MFD_CLOEXEC | MFD_NOEXEC_SEAL)};
    // A kernel too old to know the flag refuses it. Such a file keeps its exec bit, and sealing still works.
    if (fd.get() < 0 && errno == EINVAL) fd = UniqueFd{::memfd_create("jcppgui-frame", MFD_CLOEXEC | MFD_ALLOW_SEALING)};
    if (fd.get() < 0) return make_error(ErrorCode::System, "memfd_create failed");
    int reserve_failure = 0;
    do {
        reserve_failure = ::posix_fallocate(fd.get(), 0, saturate_to<::off_t>(bytes));
    } while (reserve_failure == EINTR);
    if (reserve_failure != 0) return make_error(ErrorCode::System, "the buffer file could not be sized");
    if (::fcntl(fd.get(), F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL) != 0)
        return make_error(ErrorCode::System, "the buffer file could not be sealed");
    return fd;
}

std::expected<XkbKeymap, Error> load_keymap(xkb_context* context, std::uint32_t format, UniqueFd fd,
                                            std::uint32_t size) {
    if (auto header = check_keymap_header(format, size); !header) return std::unexpected(std::move(header.error()));

    struct stat file_status {};
    struct statfs file_system {};
    if (::fstat(fd.get(), &file_status) != 0 || ::fstatfs(fd.get(), &file_system) != 0)
        return make_error(ErrorCode::Keymap, "keymap descriptor could not be examined");
    if (auto file = check_keymap_file(S_ISREG(file_status.st_mode), static_cast<std::uint64_t>(file_system.f_type), file_status.st_size, size); !file)
        return std::unexpected(std::move(file.error()));

    std::vector<char> bytes(size);
    if (auto copied = read_at(fd.get(), std::span{bytes}, 0); !copied)
        return make_error(ErrorCode::Keymap, "keymap could not be read in full");
    auto length = keymap_text_length(bytes);
    if (!length) return std::unexpected(std::move(length.error()));

    XkbKeymap keymap{xkb_keymap_new_from_buffer(context, bytes.data(), *length, XKB_KEYMAP_FORMAT_TEXT_V1,
                                                XKB_KEYMAP_COMPILE_NO_FLAGS)};
    if (!keymap) return make_error(ErrorCode::Keymap, "keymap did not compile");
    return keymap;
}

}  // namespace jgui::inline v001::detail
