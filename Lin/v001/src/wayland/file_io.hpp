// File: src/wayland/file_io.hpp
// Purpose: The only place typed memory meets the OS's untyped byte interface. Typed spans go in, a
//          pointer converts implicitly at the call, and nothing here casts back. Short transfers and
//          interrupted calls are handled inside, so a caller sees all of it done or an Error.
#pragma once

#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

#include "jCppGUI/v001/error.hpp"
#include "jCppGUI/v001/geometry.hpp"

namespace jgui::inline v001::detail {

template <class T>
concept FileElement = std::is_trivially_copyable_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>;

// What one system call did: bytes moved, or a negative count with the error it reported.
struct IoStep {
    std::int64_t bytes{};
    int error{};
};

// The byte count of that many elements. Empty when it would not fit a file offset.
template <FileElement T>
[[nodiscard]] constexpr std::optional<std::size_t> checked_byte_count(std::size_t count) noexcept {
    if (std::cmp_greater(count, std::numeric_limits<std::int64_t>::max() / saturate_to<std::int64_t>(sizeof(T))))
        return std::nullopt;
    return count * sizeof(T);
}

// True when every byte of the range has a valid file offset.
[[nodiscard]] constexpr bool file_range_fits(std::int64_t offset, std::size_t bytes) noexcept {
    if (offset < 0) return false;
    return std::cmp_less_equal(bytes, std::numeric_limits<std::int64_t>::max() - offset);
}

// Repeats step(done) until the total has moved. An interrupted call is repeated, a short one goes on
// from where it stopped, and a call that moves nothing or claims too much ends it with an Error.
template <class Step>
[[nodiscard]] constexpr std::expected<void, Error> transfer_all(std::size_t total, Step step) {
    std::size_t done = 0;
    while (done < total) {
        const IoStep moved = step(done);
        if (moved.bytes < 0) {
            if (moved.error == EINTR) continue;
            return make_error(ErrorCode::System, "file transfer failed");
        }
        if (moved.bytes == 0) return make_error(ErrorCode::System, "file transfer ended early");
        if (std::cmp_greater(moved.bytes, total - done))
            return make_error(ErrorCode::System, "file transfer moved more than was asked");
        done += saturate_to<std::size_t>(moved.bytes);
    }
    return {};
}

// Fills the whole span from the file at that offset. A file that ends first is an Error.
template <FileElement T>
[[nodiscard]] std::expected<void, Error> read_at(int fd, std::span<T> data, std::int64_t offset) {
    const std::optional<std::size_t> total = checked_byte_count<T>(data.size());
    if (!total || !file_range_fits(offset, *total)) return make_error(ErrorCode::SizeLimit, "file range is out of limits");
    const std::span<std::byte> bytes = std::as_writable_bytes(data);
    return transfer_all(*total, [&](std::size_t done) {
        const ::ssize_t moved = ::pread(fd, bytes.subspan(done).data(), *total - done,
                                        saturate_to<::off_t>(offset + saturate_to<std::int64_t>(done)));
        return IoStep{moved, moved < 0 ? errno : 0};
    });
}

// Writes the whole span to the file at that offset.
template <FileElement T>
[[nodiscard]] std::expected<void, Error> write_at(int fd, std::span<const T> data, std::int64_t offset) {
    const std::optional<std::size_t> total = checked_byte_count<T>(data.size());
    if (!total || !file_range_fits(offset, *total)) return make_error(ErrorCode::SizeLimit, "file range is out of limits");
    const std::span<const std::byte> bytes = std::as_bytes(data);
    return transfer_all(*total, [&](std::size_t done) {
        const ::ssize_t moved = ::pwrite(fd, bytes.subspan(done).data(), *total - done,
                                         saturate_to<::off_t>(offset + saturate_to<std::int64_t>(done)));
        return IoStep{moved, moved < 0 ? errno : 0};
    });
}

}  // namespace jgui::inline v001::detail
