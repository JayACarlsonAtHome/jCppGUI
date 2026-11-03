// File: src/wayland/untrusted.hpp
// Purpose: Handling of what the compositor sends that could do harm: file descriptors, the keymap
//          and window sizes. Kept apart from the connection so tests can reach it directly.
#pragma once

#include <fcntl.h>
#include <sys/mman.h>  // for memfd_create
#include <unistd.h>

#include <algorithm>
#include <cerrno>
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
#include "file_io.hpp"
#include "jCppGUI/v001/canvas.hpp"
#include "jCppGUI/v001/error.hpp"
#include "jCppGUI/v001/events.hpp"
#include "jCppGUI/v001/geometry.hpp"
#include "jCppGUI/v001/text.hpp"

namespace jgui::inline v001::detail {

using XkbContext = CResource<xkb_context, xkb_context_unref>;
using XkbKeymap = CResource<xkb_keymap, xkb_keymap_unref>;
using XkbState = CResource<xkb_state, xkb_state_unref>;

inline constexpr std::uint32_t keymap_format_none = 0;    // WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP
inline constexpr std::uint32_t keymap_format_xkb_v1 = 1;  // WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1
inline constexpr std::uint32_t max_keymap_bytes = 1024 * 1024;

// The context every compositor-supplied keymap compiles under. It has no include path and reads no names
// from the environment, so the keymap's text cannot make xkbcommon read any file.
[[nodiscard]] XkbContext make_keymap_context() noexcept;

// The system calls a buffer file needs. They are a template parameter only so that a test can make each one fail
// or report what it did: production always uses this one, and create_buffer_file has no branch for it.
struct BufferFileCalls {
    [[nodiscard]] static int memfd_create(const char* name, unsigned int flags) noexcept { return ::memfd_create(name, flags); }
    [[nodiscard]] static int reserve(int fd, ::off_t bytes) noexcept { return ::posix_fallocate(fd, 0, bytes); }
    [[nodiscard]] static int add_seals(int fd, int seals) noexcept { return ::fcntl(fd, F_ADD_SEALS, seals); }
};

// A new file for one window buffer: not executable, with its space reserved, so a shortage is an Error
// here and not a failure later. It is sealed at that size, so nobody can truncate it under the compositor.
template <class Calls = BufferFileCalls>
[[nodiscard]] std::expected<UniqueFd, Error> create_buffer_file(std::size_t bytes) {
    UniqueFd fd{Calls::memfd_create("jcppgui-frame", MFD_CLOEXEC | MFD_NOEXEC_SEAL)};
    // A kernel too old to know the flag refuses it. Such a file keeps its exec bit, and sealing still works.
    if (fd.get() < 0 && errno == EINVAL) fd = UniqueFd{Calls::memfd_create("jcppgui-frame", MFD_CLOEXEC | MFD_ALLOW_SEALING)};
    if (fd.get() < 0) return make_error(ErrorCode::System, "memfd_create failed");
    int reserve_failure = 0;
    do {
        reserve_failure = Calls::reserve(fd.get(), saturate_to<::off_t>(bytes));
    } while (reserve_failure == EINTR);
    if (reserve_failure != 0) return make_error(ErrorCode::System, "the buffer file could not be sized");
    if (Calls::add_seals(fd.get(), F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL) != 0)
        return make_error(ErrorCode::System, "the buffer file could not be sealed");
    return fd;
}

// The checks made on a keymap before anything is read.
[[nodiscard]] constexpr std::expected<void, Error> check_keymap_header(std::uint32_t format, std::uint32_t size) {
    if (format != keymap_format_xkb_v1) return make_error(ErrorCode::Keymap, "keymap is not in xkb v1 format");
    if (size == 0 || size > max_keymap_bytes) return make_error(ErrorCode::Keymap, "keymap size is out of range");
    return {};
}

// What a keymap descriptor must be: a regular file on tmpfs, where a read cannot wait on anything outside this
// machine's memory. A pipe, a directory or a file on a FUSE mount could otherwise hold the whole program up.
inline constexpr std::uint64_t tmpfs_filesystem_type = 0x01021994;  // TMPFS_MAGIC, which memfd_create's files report too

[[nodiscard]] constexpr std::expected<void, Error> check_keymap_file(bool regular_file, std::uint64_t filesystem_type,
                                                                     std::int64_t file_bytes, std::uint32_t size) {
    if (!regular_file) return make_error(ErrorCode::Keymap, "keymap descriptor is not a regular file");
    if (filesystem_type != tmpfs_filesystem_type) return make_error(ErrorCode::Keymap, "keymap file is not on tmpfs");
    if (file_bytes < std::int64_t{size}) return make_error(ErrorCode::Keymap, "keymap file is shorter than its stated size");
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

// Accepts only the xkb v1 format and a size within the limit, and only a regular file on tmpfs, reads the bytes
// into a private copy, so that the compositor cannot change them after they are checked, and compiles that copy. A
// file shorter than the stated size is refused. The descriptor is taken by value, so it is closed on every path.
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
    return Size{width > 0 ? Width{std::min(width, max_buffer_side)} : fallback.width(),
                height > 0 ? Height{std::min(height, max_buffer_side)} : fallback.height()};
}

// What one axis of the pointer has said since the last pointer frame. Steps win over pixels, because a wheel sends both
// for one notch. Pixels arrive in the protocol's fixed point, with 8 bits of fraction, and what is left over of a pixel
// waits for the next frame, so slow smooth scrolling still moves. Each total stays within what one event can ask for, so
// no flood can overflow it.
template <class Tag>
class AxisTally {
public:
    constexpr void add_fixed_pixels(std::int32_t fixed) noexcept {
        constexpr std::int64_t most = std::int64_t{max_scroll_pixels} * 256;
        fixed_ = std::clamp(fixed_ + std::int64_t{fixed}, -most, most);
    }
    constexpr void add_steps(std::int32_t steps) noexcept {
        steps_ = std::clamp(steps_ + std::int64_t{steps}, std::int64_t{-max_scroll_steps}, std::int64_t{max_scroll_steps});
    }

    [[nodiscard]] constexpr ScrollAmount<Tag> take() noexcept {
        if (steps_ != 0) {
            const auto steps = static_cast<std::int32_t>(steps_);
            steps_ = 0;
            fixed_ = 0;
            return ScrollAmount<Tag>::of_steps(steps);
        }
        const auto pixels = static_cast<std::int32_t>(fixed_ / 256);
        fixed_ %= 256;
        return ScrollAmount<Tag>::of_pixels(pixels);
    }

private:
    std::int64_t fixed_{};
    std::int64_t steps_{};
};

struct PointerScrollTally {
    AxisTally<detail::ScrollDownTag> down{};
    AxisTally<detail::ScrollAcrossTag> across{};
};

// An evdev key code in xkb numbering, which is 8 higher. Empty for a code too large to convert.
[[nodiscard]] constexpr std::optional<std::uint32_t> evdev_to_xkb(std::uint32_t key) noexcept {
    if (key > std::numeric_limits<std::uint32_t>::max() - 8) return std::nullopt;
    return key + 8;
}

}  // namespace jgui::inline v001::detail
