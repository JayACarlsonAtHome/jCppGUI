// File: src/wayland/untrusted.hpp
// Purpose: Handling of what the compositor sends that could do harm: file descriptors, the keymap
//          and window sizes. Kept apart from the connection so tests can reach it directly.
#pragma once

#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include <xkbcommon/xkbcommon.h>

#include "../c_resource.hpp"
#include "jCppGUI/v001/canvas.hpp"
#include "jCppGUI/v001/error.hpp"
#include "jCppGUI/v001/geometry.hpp"
#include "jCppGUI/v001/text.hpp"

namespace jgui::inline v001::detail {

// A file descriptor closed by RAII.
class UniqueFd {
public:
    UniqueFd() noexcept = default;
    explicit UniqueFd(int fd) noexcept : fd_{fd} {}
    UniqueFd(UniqueFd&& other) noexcept : fd_{std::exchange(other.fd_, -1)} {}
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    ~UniqueFd() { reset(); }

    [[nodiscard]] int get() const noexcept { return fd_; }

private:
    void reset() noexcept {
        if (fd_ >= 0) static_cast<void>(::close(fd_));
        fd_ = -1;
    }
    int fd_{-1};
};

using XkbContext = CResource<xkb_context, xkb_context_unref>;
using XkbKeymap = CResource<xkb_keymap, xkb_keymap_unref>;
using XkbState = CResource<xkb_state, xkb_state_unref>;

inline constexpr std::uint32_t keymap_format_none = 0;    // WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP
inline constexpr std::uint32_t keymap_format_xkb_v1 = 1;  // WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1
inline constexpr std::uint32_t max_keymap_bytes = 1024 * 1024;

// The context every compositor-supplied keymap compiles under. It has no include path and reads no names
// from the environment, so the keymap's text cannot make xkbcommon read any file.
[[nodiscard]] XkbContext make_keymap_context() noexcept;

// A new file for one window buffer: not executable, with its space reserved, so a shortage is an Error
// here and not a failure later. It is sealed at that size, so nobody can truncate it under the compositor.
[[nodiscard]] std::expected<UniqueFd, Error> create_buffer_file(std::size_t bytes);

// The checks made on a keymap before anything is read.
[[nodiscard]] constexpr std::expected<void, Error> check_keymap_header(std::uint32_t format, std::uint32_t size) {
    if (format != keymap_format_xkb_v1) return make_error(ErrorCode::Keymap, "keymap is not in xkb v1 format");
    if (size == 0 || size > max_keymap_bytes) return make_error(ErrorCode::Keymap, "keymap size is out of range");
    return {};
}

// The length of the keymap's text: everything before its first NUL, which must be inside the bytes
// the compositor announced. Nothing after that NUL is ever given to xkbcommon.
[[nodiscard]] constexpr std::expected<std::size_t, Error> keymap_text_length(std::span<const char> bytes) {
    const auto end = std::ranges::find(bytes, '\0');
    if (end == bytes.end()) return make_error(ErrorCode::Keymap, "keymap has no terminator inside its stated size");
    if (end == bytes.begin()) return make_error(ErrorCode::Keymap, "keymap is empty");
    return static_cast<std::size_t>(end - bytes.begin());
}

// Accepts only the xkb v1 format and a size within the limit, reads the bytes into a private copy, so
// that the compositor cannot change them after they are checked, and compiles that copy. A file
// shorter than the stated size is refused. The descriptor is taken by value, so it is closed on every path.
[[nodiscard]] std::expected<XkbKeymap, Error> load_keymap(xkb_context* context, std::uint32_t format, UniqueFd fd,
                                                          std::uint32_t size);

// libwayland refuses a message over 4096 bytes and the connection ends. A title or app id is sent with an
// 8-byte header, a 4-byte length and a terminating NUL.
inline constexpr std::size_t max_window_string_bytes = 4096 - 8 - 4 - 1;

// The checks on a window title or app id before it reaches libwayland. libwayland sends it up to its
// first NUL, so one inside the string would silently cut it short.
[[nodiscard]] constexpr std::expected<void, Error> check_window_string(std::string_view text) {
    if (text.size() > max_window_string_bytes) return make_error(ErrorCode::TextTooLong, "a window title or app id is too long");
    if (!valid_utf8(text) || text.contains('\0')) return make_error(ErrorCode::InvalidUtf8, "a window title or app id is not valid text");
    return {};
}

// What a configure's width and height mean. Zero means the client chooses: the fallback. A negative
// value is treated the same way, and a value over the buffer limit is clamped to it.
[[nodiscard]] constexpr Size configured_size(std::int32_t width, std::int32_t height, Size fallback) noexcept {
    const std::int32_t w = width > 0 ? std::min(width, max_buffer_side) : fallback.width();
    const std::int32_t h = height > 0 ? std::min(height, max_buffer_side) : fallback.height();
    return Size{w, h};
}

// An evdev key code in xkb numbering, which is 8 higher. Empty for a code too large to convert.
[[nodiscard]] constexpr std::optional<std::uint32_t> evdev_to_xkb(std::uint32_t key) noexcept {
    if (key > std::numeric_limits<std::uint32_t>::max() - 8) return std::nullopt;
    return key + 8;
}

}  // namespace jgui::inline v001::detail
