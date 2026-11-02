// File: src/wayland/untrusted.cpp
// Purpose: Loads a compositor-supplied keymap: the part of the checks that needs the OS and xkbcommon.
#include "untrusted.hpp"

#include <sys/stat.h>

namespace jgui::inline v001::detail {

std::expected<XkbKeymap, Error> load_keymap(xkb_context* context, std::uint32_t format, UniqueFd fd,
                                            std::uint32_t size) {
    if (auto header = check_keymap_header(format, size); !header) return std::unexpected(std::move(header.error()));

    struct stat info {};
    if (::fstat(fd.get(), &info) != 0) return make_error(ErrorCode::Keymap, "keymap descriptor cannot be examined");
    if (auto file = check_keymap_file(std::int64_t{info.st_size}, size); !file)
        return std::unexpected(std::move(file.error()));

    auto mapped = Mapped<const char>::map(fd.get(), size, PROT_READ, MAP_PRIVATE);
    if (!mapped) return make_error(ErrorCode::Keymap, "keymap could not be mapped");

    XkbKeymap keymap{xkb_keymap_new_from_buffer(context, mapped->data(), size, XKB_KEYMAP_FORMAT_TEXT_V1,
                                                XKB_KEYMAP_COMPILE_NO_FLAGS)};
    if (!keymap) return make_error(ErrorCode::Keymap, "keymap did not compile");
    return keymap;
}

}  // namespace jgui::inline v001::detail
