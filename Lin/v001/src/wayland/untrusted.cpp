// File: src/wayland/untrusted.cpp
// Purpose: Loads a compositor-supplied keymap: the part of the checks that needs the OS and xkbcommon.
#include "untrusted.hpp"

#include <span>
#include <vector>

#include "file_io.hpp"

namespace jgui::inline v001::detail {

std::expected<XkbKeymap, Error> load_keymap(xkb_context* context, std::uint32_t format, UniqueFd fd,
                                            std::uint32_t size) {
    if (auto header = check_keymap_header(format, size); !header) return std::unexpected(std::move(header.error()));

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
