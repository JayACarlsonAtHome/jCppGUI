// File: tests/test_untrusted.cpp
// Purpose: The checks on compositor-supplied values, without a compositor: window sizes at compile
//          time, and keymaps through real file descriptors at run time. Also the typed file helper.
#include <sys/mman.h>  // for memfd_create
#include <sys/stat.h>
#include <unistd.h>

#include <fcntl.h>
#include <linux/magic.h>
#include <sys/vfs.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "check.hpp"
#include "file_io.hpp"
#include "jCppGUI/v001/program_arguments.hpp"
#include "untrusted.hpp"

namespace {

using namespace jgui;
using namespace jgui::detail;

constexpr std::int32_t hi = std::numeric_limits<std::int32_t>::max();
constexpr std::int32_t lo = std::numeric_limits<std::int32_t>::min();
constexpr Size fallback{Width{640}, Height{480}};

static_assert(configured_size(0, 0, fallback) == fallback, "zero means the client chooses");
static_assert(configured_size(-1, -1, fallback) == fallback && configured_size(lo, lo, fallback) == fallback);
static_assert(configured_size(800, 0, fallback) == Size{Width{800}, Height{480}} && configured_size(0, 600, fallback) == Size{Width{640}, Height{600}});
static_assert(configured_size(800, 600, fallback) == Size{Width{800}, Height{600}});
static_assert(configured_size(hi, hi, fallback) == Size{Width{max_buffer_side}, Height{max_buffer_side}}, "clamped to the limit");
static_assert(PixelBuffer::byte_size(configured_size(hi, hi, fallback)).has_value(), "and the clamped size is accepted");

static_assert(check_keymap_header(keymap_format_xkb_v1, 1).has_value());
static_assert(check_keymap_header(keymap_format_xkb_v1, max_keymap_bytes).has_value());
static_assert(!check_keymap_header(keymap_format_xkb_v1, max_keymap_bytes + 1).has_value());
static_assert(!check_keymap_header(keymap_format_xkb_v1, 0).has_value());
static_assert(!check_keymap_header(0, 1).has_value() && !check_keymap_header(2, 1).has_value());
static_assert(!check_keymap_header(keymap_format_xkb_v1, std::numeric_limits<std::uint32_t>::max()).has_value());

static_assert(tmpfs_filesystem_type == TMPFS_MAGIC);
static_assert(check_keymap_file(true, tmpfs_filesystem_type, 10, 10).has_value());
static_assert(check_keymap_file(true, tmpfs_filesystem_type, 11, 10).has_value(), "a longer file is fine");
static_assert(!check_keymap_file(true, tmpfs_filesystem_type, 9, 10).has_value(), "a shorter one is not");
static_assert(!check_keymap_file(false, tmpfs_filesystem_type, 10, 10).has_value(), "not a regular file");
static_assert(!check_keymap_file(true, 0x65735546, 10, 10).has_value(), "a FUSE mount");
static_assert(!check_keymap_file(true, 0, 10, 10).has_value());
static_assert(!check_keymap_file(true, tmpfs_filesystem_type, -1, 1).has_value());

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
        const std::size_t call = calls++;
        if (call < N && steps[call].bytes > 0) expected_done += static_cast<std::size_t>(steps[call].bytes);
        return call < N ? steps[call] : IoStep{-1, EIO};
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

// The longest string that fits libwayland's 4096-byte message, tried against the live compositor.
static_assert(max_window_string_bytes == 4083);
static_assert(check_window_string("").has_value() && check_window_string("jCppGUI").has_value());
static_assert(check_window_string("caf\xC3\xA9").has_value(), "real UTF-8 is fine");
static_assert(check_window_string("\xFF").error().code == ErrorCode::InvalidUtf8);
static_assert(check_window_string("\xC0\x80").error().code == ErrorCode::InvalidUtf8);
static_assert(check_window_string(std::string_view{"a\0b", 3}).error().code == ErrorCode::InvalidUtf8, "a NUL would cut it short");
static_assert(check_window_string(std::string(max_window_string_bytes, 't')).has_value());
static_assert(check_window_string(std::string(max_window_string_bytes + 1, 't')).error().code == ErrorCode::TextTooLong);

// --- The one helper that reads a C pointer and count ---
constexpr std::array<int, 3> c_side{10, 20, 30};
static_assert(copy_from_c(c_side.data(), 3, 3).value() == std::vector<int>{10, 20, 30}, "at the limit");
static_assert(copy_from_c(c_side.data(), 2, 3).value() == std::vector<int>{10, 20}, "only what was counted");
static_assert(copy_from_c<int>(nullptr, 0, 3).value().empty(), "a zero count never touches the pointer");
static_assert(copy_from_c<int>(nullptr, 0, 0).value().empty());
static_assert(copy_from_c(c_side.data(), 0, 0).value().empty());
static_assert(copy_from_c<int>(nullptr, 1, 3).error().code == ErrorCode::System, "null with a count");
static_assert(copy_from_c(c_side.data(), 3, 2).error().code == ErrorCode::SizeLimit, "over the limit");

// --- The one helper that walks a C string. Reading past the end of these would not compile. ---
constexpr std::array<char, 4> c_string{'a', 'b', 'c', '\0'};
constexpr std::array<char, 3> c_no_terminator{'a', 'b', 'c'};
constexpr std::array<unsigned char, 3> c_font_path{'x', 'y', '\0'};
static_assert(string_from_c(c_string.data(), 3).value() == "abc", "exactly at the limit");
static_assert(string_from_c(c_string.data(), 2).error().code == ErrorCode::SizeLimit, "one over the limit");
static_assert(string_from_c(c_no_terminator.data(), 2).error().code == ErrorCode::SizeLimit, "no terminator: the walk stops at the limit");
static_assert(string_from_c(c_font_path.data(), 2).value() == "xy", "fontconfig's text type");
static_assert(string_from_c("", 0).value().empty(), "the empty string needs no room");
static_assert(string_from_c(static_cast<const char*>(nullptr), 9).error().code == ErrorCode::System);
// Reading these would be out of bounds, which constant evaluation rejects, so they are refused before any read.
static_assert(!copy_from_c(c_side.data(), 1000, 3).has_value());
static_assert(!copy_from_c(c_side.data(), std::numeric_limits<std::size_t>::max(), 3).has_value());

// A keyboard with no keymap is its own format, handled before load_keymap, which refuses it as any other.
static_assert(keymap_format_none == 0 && !check_keymap_header(keymap_format_none, 1).has_value());

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

void test_program_arguments(jgui_test::Checker& check) {
    const std::array<const char*, 2> two{"demo", "--smoke"};
    const auto parsed = program_arguments(2, two.data());
    check(parsed.has_value() && parsed->size() == 2 && parsed->at(0) == "demo" && parsed->at(1) == "--smoke", "both arguments");
    check(program_arguments(0, nullptr).has_value() && program_arguments(0, nullptr)->empty(), "none, without touching the pointer");
    check(!program_arguments(-1, two.data()).has_value(), "a negative count is refused");
    check(!program_arguments(1, nullptr).has_value(), "a null argv with a count is refused");
    // The array has 2 entries, so ASan would report any read of this many.
    check(!program_arguments(saturate_to<int>(max_program_arguments + 1), two.data()).has_value(), "a count over the limit is refused before any read");
    const std::array<const char*, 2> holed{"demo", nullptr};
    check(!program_arguments(2, holed.data()).has_value(), "a null argument is refused");
    const std::string longest(max_program_argument_bytes, 'x');
    const std::string too_long(max_program_argument_bytes + 1, 'x');
    const std::array<const char*, 1> at_limit{longest.c_str()};
    const std::array<const char*, 1> over_limit{too_long.c_str()};
    check(program_arguments(1, at_limit.data()).has_value(), "an argument at its limit is accepted");
    check(!program_arguments(1, over_limit.data()).has_value(), "and one over it is refused");
    // What comes back owns its text: the argument's memory can change, or go, and the copy stays.
    std::array<char, 3> changing{'a', 'b', '\0'};
    const std::array<const char*, 1> pointing{changing.data()};
    const auto copied = program_arguments(1, pointing.data());
    changing[0] = 'z';
    check(copied.has_value() && copied->at(0) == "ab", "an argument is a copy");
}

void test_buffer_file(jgui_test::Checker& check) {
    constexpr std::size_t bytes = 4096;
    auto file = create_buffer_file(bytes);
    check(file.has_value(), "a buffer file");
    if (!file) return;
    const int fd = file->get();
    const int seals = ::fcntl(fd, F_GET_SEALS);
    check(seals >= 0 && (seals & F_SEAL_SHRINK) != 0 && (seals & F_SEAL_GROW) != 0 && (seals & F_SEAL_SEAL) != 0, "it is sealed");
    check((seals & F_SEAL_WRITE) == 0, "and the seals allow writing");
    struct stat info {};
    check(::fstat(fd, &info) == 0 && info.st_size == 4096, "it has its size");
    check((info.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0, "it is not executable");
    check((::fcntl(fd, F_GETFD) & FD_CLOEXEC) != 0, "it closes on exec");
    check(::ftruncate(fd, 0) != 0 && errno == EPERM, "it cannot be truncated");
    check(::ftruncate(fd, 8192) != 0 && errno == EPERM, "or extended");
    check(::posix_fallocate(fd, 0, 8192) == EPERM, "or reserved past its end");
    check(::fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE) != 0, "and no more seals can be added");
    const std::array<std::uint32_t, 2> out{1U, 2U};
    std::array<std::uint32_t, 2> in{};
    check(write_at<std::uint32_t>(fd, out, 4088).has_value(), "a write inside it works");
    check(read_at<std::uint32_t>(fd, in, 4088).has_value() && in == out, "and reads back");
    check(!write_at<std::uint32_t>(fd, out, 4092).has_value(), "a write past its end is an Error");
}

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
    test_buffer_file(check);
    test_program_arguments(check);
    const XkbContext context = make_keymap_context();
    check(context != nullptr, "the xkb context for compositor keymaps");
    const XkbContext system_context{xkb_context_new(XKB_CONTEXT_NO_FLAGS)};
    check(system_context != nullptr, "an xkb context that can read the system's keymap data");

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
    {
        std::array<int, 2> pipe_ends{-1, -1};
        check(::pipe(pipe_ends.data()) == 0, "a pipe");
        const UniqueFd writer{pipe_ends[1]};
        check(refused(keymap_format_xkb_v1, UniqueFd{pipe_ends[0]}, 16), "a pipe is refused, and the read that would hang never starts");
        check(refused(keymap_format_xkb_v1, UniqueFd{::open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC)}, 16), "a directory");
        check(refused(keymap_format_xkb_v1, UniqueFd{-1}, 16), "a bad descriptor");
    }
    {
        // A regular file on a file system that is not tmpfs, if the test runs on one. Most build trees are not on tmpfs.
        constexpr std::string_view name = "jcppgui-test-keymap-file";
        const UniqueFd made{::open(name.data(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, S_IRUSR | S_IWUSR)};
        struct statfs where {};
        check(made.get() >= 0 && ::fstatfs(made.get(), &where) == 0, "a file in the working directory");
        if (std::cmp_not_equal(where.f_type, tmpfs_filesystem_type)) {
            const std::string text = std::string{"x"} + '\0';
            check(write_at(made.get(), std::span{text}, 0).has_value(), "with something in it");
            check(refused(keymap_format_xkb_v1, UniqueFd{::dup(made.get())}, 2), "a regular file that is not on tmpfs is refused");
        }
        check(::unlink(name.data()) == 0, "removed again");
    }
    const std::string garbage = std::string{"this is not a keymap"} + '\0';
    check(refused(keymap_format_xkb_v1, file_with(garbage), static_cast<std::uint32_t>(garbage.size())), "garbage does not compile");
    check(refused(keymap_format_xkb_v1, file_with("no terminator"), 13), "no terminator in the file");
    check(refused(keymap_format_xkb_v1, file_with(std::string{"terminator past the size"} + '\0'), 10), "the terminator is past the stated size");
    check(refused(keymap_format_xkb_v1, file_with(std::string_view{"\0xxx", 4}), 4), "a terminator and no text");

    // A keymap that includes files found only on the default search path. The same text compiles under a
    // default context, so the refusal comes from the compositor context and not from the text.
    const std::string including = std::string{
        "xkb_keymap {\n"
        "  xkb_keycodes { include \"evdev+aliases(qwerty)\" };\n"
        "  xkb_types { include \"complete\" };\n"
        "  xkb_compat { include \"complete\" };\n"
        "  xkb_symbols { include \"pc+us+inet(evdev)\" };\n"
        "};\n"} + '\0';
    const auto including_size = static_cast<std::uint32_t>(including.size());
    check(load_keymap(system_context.get(), keymap_format_xkb_v1, file_with(including), including_size).has_value(),
          "a default context follows the include statements");
    check(refused(keymap_format_xkb_v1, file_with(including), including_size), "the compositor context refuses them");

    // A real keymap, made by xkbcommon itself, is accepted. It depends on the system's xkb data.
    const xkb_rule_names names{};
    const XkbKeymap source{xkb_keymap_new_from_names(system_context.get(), &names, XKB_KEYMAP_COMPILE_NO_FLAGS)};
    check(source != nullptr, "the system has keymap data");
    if (source) {
        const CResource<char, [](char* p) { std::free(p); }> text{xkb_keymap_get_as_string(source.get(), XKB_KEYMAP_FORMAT_TEXT_V1)};
        check(text != nullptr, "the keymap as text");
        if (text) {
            std::string view = string_from_c(text.get(), max_keymap_bytes).value_or("");
            view.push_back('\0');
            UniqueFd fd = file_with(view);
            const int raw = fd.get();
            const auto loaded = load_keymap(context.get(), keymap_format_xkb_v1, std::move(fd),
                                            static_cast<std::uint32_t>(view.size()));
            check(loaded.has_value(), "a valid keymap is accepted");
            check(is_closed(raw), "and its descriptor is closed");

            // Bytes past the terminator stay out of the compiler, and so do bytes past the stated size.
            std::string with_tail = view;
            with_tail += "this tail is not a keymap";
            check(load_keymap(context.get(), keymap_format_xkb_v1, file_with(with_tail),
                              static_cast<std::uint32_t>(with_tail.size())).has_value(), "a tail after the terminator is ignored");
            check(load_keymap(context.get(), keymap_format_xkb_v1, file_with(with_tail),
                              static_cast<std::uint32_t>(view.size())).has_value(), "and so is a file longer than stated");
        }
    }
    return check.finish();
}
