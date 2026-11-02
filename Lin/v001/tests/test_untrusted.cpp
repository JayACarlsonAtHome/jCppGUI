// File: tests/test_untrusted.cpp
// Purpose: The checks on compositor-supplied values, without a compositor: window sizes at compile
//          time, and keymaps through real file descriptors at run time. Also the typed file helper.
#include <sys/mman.h>  // for memfd_create
#include <unistd.h>

#include <fcntl.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <span>
#include <string>
#include <string_view>

#include "check.hpp"
#include "file_io.hpp"
#include "untrusted.hpp"

namespace {

using namespace jgui;
using namespace jgui::detail;

constexpr std::int32_t hi = std::numeric_limits<std::int32_t>::max();
constexpr std::int32_t lo = std::numeric_limits<std::int32_t>::min();
constexpr Size fallback{640, 480};

static_assert(configured_size(0, 0, fallback) == fallback, "zero means the client chooses");
static_assert(configured_size(-1, -1, fallback) == fallback && configured_size(lo, lo, fallback) == fallback);
static_assert(configured_size(800, 0, fallback) == Size{800, 480} && configured_size(0, 600, fallback) == Size{640, 600});
static_assert(configured_size(800, 600, fallback) == Size{800, 600});
static_assert(configured_size(hi, hi, fallback) == Size{max_buffer_side, max_buffer_side}, "clamped to the limit");
static_assert(PixelBuffer::byte_size(configured_size(hi, hi, fallback)).has_value(), "and the clamped size is accepted");

static_assert(check_keymap_header(keymap_format_xkb_v1, 1).has_value());
static_assert(check_keymap_header(keymap_format_xkb_v1, max_keymap_bytes).has_value());
static_assert(!check_keymap_header(keymap_format_xkb_v1, max_keymap_bytes + 1).has_value());
static_assert(!check_keymap_header(keymap_format_xkb_v1, 0).has_value());
static_assert(!check_keymap_header(0, 1).has_value() && !check_keymap_header(2, 1).has_value());
static_assert(!check_keymap_header(keymap_format_xkb_v1, std::numeric_limits<std::uint32_t>::max()).has_value());

constexpr std::array<char, 3> no_nul{'a', 'b', 'c'};
constexpr std::array<char, 3> nul_last{'a', 'b', '\0'};
constexpr std::array<char, 3> nul_early{'a', '\0', 'c'};
constexpr std::array<char, 2> nul_only{'\0', 'c'};
static_assert(!keymap_text_length(no_nul).has_value(), "no terminator inside the stated size");
static_assert(keymap_text_length(nul_last).value() == 2U);
static_assert(keymap_text_length(nul_early).value() == 1U, "what follows the first NUL is not text");
static_assert(!keymap_text_length(nul_only).has_value(), "nothing before the terminator");
static_assert(!keymap_text_length(std::span<const char>{}).has_value());

// --- The helper's arithmetic and its loop, with made-up system calls ---
constexpr std::uint64_t most_bytes = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
static_assert(checked_byte_count<std::uint32_t>(3).value() == 12U && checked_byte_count<char>(0).value() == 0U);
static_assert(checked_byte_count<std::uint32_t>(most_bytes / 4).has_value());
static_assert(!checked_byte_count<std::uint32_t>(most_bytes / 4 + 1).has_value());
static_assert(!checked_byte_count<std::uint32_t>(std::numeric_limits<std::size_t>::max()).has_value());
static_assert(file_range_fits(0, 0) && file_range_fits(0, most_bytes) && file_range_fits(std::numeric_limits<std::int64_t>::max(), 0));
static_assert(!file_range_fits(-1, 0) && !file_range_fits(1, most_bytes));
static_assert(!file_range_fits(0, std::numeric_limits<std::size_t>::max()));

template <std::size_t N>
constexpr bool transfer_of(std::size_t total, std::array<IoStep, N> steps, std::size_t expected_calls) {
    std::size_t calls = 0;
    std::size_t expected_done = 0;
    bool offsets_right = true;
    const auto result = transfer_all(total, [&](std::size_t done) {
        offsets_right = offsets_right && done == expected_done;
        const IoStep step = calls < N ? steps[calls] : IoStep{-1, EIO};
        ++calls;
        if (step.bytes > 0) expected_done += static_cast<std::size_t>(step.bytes);
        return step;
    });
    return result.has_value() && calls == expected_calls && offsets_right;
}
static_assert(transfer_of(0, std::array{IoStep{-1, EIO}}, 0), "nothing to move, no call");
static_assert(transfer_of(8, std::array{IoStep{8, 0}}, 1));
static_assert(transfer_of(8, std::array{IoStep{3, 0}, IoStep{1, 0}, IoStep{4, 0}}, 3), "short transfers go on, mid-element too");
static_assert(transfer_of(8, std::array{IoStep{-1, EINTR}, IoStep{5, 0}, IoStep{-1, EINTR}, IoStep{3, 0}}, 4), "an interrupted call is repeated");
static_assert(!transfer_of(8, std::array{IoStep{3, 0}, IoStep{0, 0}}, 2), "a file that ends early");
static_assert(!transfer_of(8, std::array{IoStep{-1, EIO}}, 1), "a real failure");
static_assert(!transfer_of(8, std::array{IoStep{9, 0}}, 1), "a claim of more than was asked");

// Only trivially copyable, and never into constant memory.
template <class T>
concept Readable = requires(int fd, std::span<T> data) { read_at(fd, data, 0); };
template <class T>
concept Writable = requires(int fd, std::span<const T> data) { write_at(fd, data, 0); };
static_assert(Readable<char> && Readable<std::uint32_t> && Writable<std::uint32_t>);
static_assert(!Readable<std::string> && !Writable<std::string>);
static_assert(!Readable<const char>);

static_assert(evdev_to_xkb(0) == 8U && evdev_to_xkb(1) == 9U);
static_assert(evdev_to_xkb(std::numeric_limits<std::uint32_t>::max() - 8) == std::numeric_limits<std::uint32_t>::max());
static_assert(!evdev_to_xkb(std::numeric_limits<std::uint32_t>::max() - 7).has_value());
static_assert(!evdev_to_xkb(std::numeric_limits<std::uint32_t>::max()).has_value());

UniqueFd file_with(std::string_view content) {
    UniqueFd fd{::memfd_create("jcppgui-test-keymap", MFD_CLOEXEC)};
    if (fd.get() >= 0) static_cast<void>(write_at(fd.get(), std::span{content}, 0));
    return fd;
}

bool is_closed(int fd) { return ::fcntl(fd, F_GETFD) == -1; }

void test_file_helper(jgui_test::Checker& check) {
    const UniqueFd fd = file_with("");
    const std::array<std::uint32_t, 3> out{0x11223344U, 0xAABBCCDDU, 0x01020304U};
    check(write_at<std::uint32_t>(fd.get(), out, 8).has_value(), "write at an offset");
    std::array<std::uint32_t, 3> in{};
    check(read_at<std::uint32_t>(fd.get(), in, 8).has_value() && in == out, "read back the same values");
    std::array<std::uint32_t, 1> first{};
    check(read_at<std::uint32_t>(fd.get(), first, 12).has_value() && first[0] == out[1], "an offset counts bytes");
    std::array<std::uint32_t, 4> too_many{};
    check(!read_at<std::uint32_t>(fd.get(), too_many, 8).has_value(), "reading past the end of the file is an Error");
    check(!read_at<std::uint32_t>(fd.get(), in, -1).has_value(), "a negative offset is an Error");
    check(!write_at<std::uint32_t>(fd.get(), out, std::numeric_limits<std::int64_t>::max()).has_value(), "an offset that overflows is an Error");
    check(!write_at<std::uint32_t>(-1, out, 0).has_value() && !read_at<std::uint32_t>(-1, in, 0).has_value(), "a bad descriptor is an Error");

    std::array<int, 2> pipe_ends{-1, -1};
    check(::pipe(pipe_ends.data()) == 0, "a pipe");
    const UniqueFd reader{pipe_ends[0]};
    const UniqueFd writer{pipe_ends[1]};
    check(!read_at<std::uint32_t>(reader.get(), in, 0).has_value(), "a descriptor with no offsets is refused and does not hang");
}

}  // namespace

int main() {
    jgui_test::Checker check;
    test_file_helper(check);
    const XkbContext context{xkb_context_new(XKB_CONTEXT_NO_FLAGS)};
    check(context != nullptr, "an xkb context");

    const auto refused = [&](std::uint32_t format, UniqueFd fd, std::uint32_t size) {
        const int raw = fd.get();
        const auto result = load_keymap(context.get(), format, std::move(fd), size);
        const bool code_ok = !result.has_value() && result.error().code == ErrorCode::Keymap;
        return code_ok && is_closed(raw);
    };

    check(refused(0, file_with("x"), 1), "a wrong format is refused and the descriptor closed");
    check(refused(keymap_format_xkb_v1, file_with("x"), 0), "size zero");
    check(refused(keymap_format_xkb_v1, file_with("x"), max_keymap_bytes + 1), "a size over the limit");
    check(refused(keymap_format_xkb_v1, file_with("short"), 4096), "a file shorter than its stated size");
    const std::string garbage = std::string{"this is not a keymap"} + '\0';
    check(refused(keymap_format_xkb_v1, file_with(garbage), static_cast<std::uint32_t>(garbage.size())), "garbage does not compile");
    check(refused(keymap_format_xkb_v1, file_with("no terminator"), 13), "no terminator in the file");
    check(refused(keymap_format_xkb_v1, file_with(std::string{"terminator past the size"} + '\0'), 10), "the terminator is past the stated size");
    check(refused(keymap_format_xkb_v1, file_with(std::string_view{"\0xxx", 4}), 4), "a terminator and no text");

    // A real keymap, made by xkbcommon itself, is accepted. It depends on the system's xkb data.
    const xkb_rule_names names{};
    const XkbKeymap source{xkb_keymap_new_from_names(context.get(), &names, XKB_KEYMAP_COMPILE_NO_FLAGS)};
    check(source != nullptr, "the system has keymap data");
    if (source) {
        const CResource<char, [](char* p) { std::free(p); }> text{xkb_keymap_get_as_string(source.get(), XKB_KEYMAP_FORMAT_TEXT_V1)};
        check(text != nullptr, "the keymap as text");
        if (text) {
            const std::string_view view{text.get(), std::char_traits<char>::length(text.get()) + 1};
            UniqueFd fd = file_with(view);
            const int raw = fd.get();
            const auto loaded = load_keymap(context.get(), keymap_format_xkb_v1, std::move(fd),
                                            static_cast<std::uint32_t>(view.size()));
            check(loaded.has_value(), "a valid keymap is accepted");
            check(is_closed(raw), "and its descriptor is closed");

            // Bytes past the terminator stay out of the compiler, and so do bytes past the stated size.
            std::string with_tail{view};
            with_tail += "this tail is not a keymap";
            check(load_keymap(context.get(), keymap_format_xkb_v1, file_with(with_tail),
                              static_cast<std::uint32_t>(with_tail.size())).has_value(), "a tail after the terminator is ignored");
            check(load_keymap(context.get(), keymap_format_xkb_v1, file_with(with_tail),
                              static_cast<std::uint32_t>(view.size())).has_value(), "and so is a file longer than stated");
        }
    }
    return check.finish();
}
