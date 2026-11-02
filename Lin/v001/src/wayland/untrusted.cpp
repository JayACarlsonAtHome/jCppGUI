// File: src/wayland/untrusted.cpp
// Purpose: Loads a compositor-supplied keymap: the part of the checks that needs the OS and xkbcommon.
#include "untrusted.hpp"

#include <sys/stat.h>
#include <sys/vfs.h>

#include <span>
#include <vector>

#include "file_io.hpp"

namespace jgui::inline v001::detail {

XkbContext make_keymap_context() noexcept {
    return XkbContext{xkb_context_new(static_cast<xkb_context_flags>(XKB_CONTEXT_NO_DEFAULT_INCLUDES | XKB_CONTEXT_NO_ENVIRONMENT_NAMES))};
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
