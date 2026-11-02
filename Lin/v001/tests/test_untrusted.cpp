// File: tests/test_untrusted.cpp
// Purpose: The checks on compositor-supplied values, without a compositor: window sizes at compile
//          time, and keymaps through real file descriptors at run time.
#include <sys/mman.h>
#include <unistd.h>

#include <fcntl.h>

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>

#include "check.hpp"
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
static_assert(check_keymap_file(100, 100).has_value() && check_keymap_file(101, 100).has_value());
static_assert(!check_keymap_file(99, 100).has_value() && !check_keymap_file(-1, 1).has_value());

static_assert(evdev_to_xkb(0) == 8U && evdev_to_xkb(1) == 9U);
static_assert(evdev_to_xkb(std::numeric_limits<std::uint32_t>::max() - 8) == std::numeric_limits<std::uint32_t>::max());
static_assert(!evdev_to_xkb(std::numeric_limits<std::uint32_t>::max() - 7).has_value());
static_assert(!evdev_to_xkb(std::numeric_limits<std::uint32_t>::max()).has_value());

UniqueFd file_with(std::string_view content) {
    UniqueFd fd{::memfd_create("jcppgui-test-keymap", MFD_CLOEXEC)};
    if (fd.get() >= 0 && !content.empty()) {
        const auto written = ::write(fd.get(), content.data(), content.size());
        static_cast<void>(written);
    }
    return fd;
}

bool is_closed(int fd) { return ::fcntl(fd, F_GETFD) == -1; }

}  // namespace

int main() {
    jgui_test::Checker check;
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
    const std::string garbage = "this is not a keymap";
    check(refused(keymap_format_xkb_v1, file_with(garbage), static_cast<std::uint32_t>(garbage.size())), "garbage does not compile");

    // A real keymap, made by xkbcommon itself, is accepted. It depends on the system's xkb data.
    const xkb_rule_names names{};
    const XkbKeymap source{xkb_keymap_new_from_names(context.get(), &names, XKB_KEYMAP_COMPILE_NO_FLAGS)};
    check(source != nullptr, "the system has keymap data");
    if (source) {
        const CResource<char, [](char* p) { std::free(p); }> text{xkb_keymap_get_as_string(source.get(), XKB_KEYMAP_FORMAT_TEXT_V1)};
        check(text != nullptr, "the keymap as text");
        if (text) {
            const std::string_view view{text.get()};
            UniqueFd fd = file_with(view);
            const int raw = fd.get();
            const auto loaded = load_keymap(context.get(), keymap_format_xkb_v1, std::move(fd),
                                            static_cast<std::uint32_t>(view.size()));
            check(loaded.has_value(), "a valid keymap is accepted");
            check(is_closed(raw), "and its descriptor is closed");
        }
    }
    return check.finish();
}
