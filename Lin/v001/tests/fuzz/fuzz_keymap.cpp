// File: tests/fuzz/fuzz_keymap.cpp
// Purpose: Fuzzes the real load_keymap under the real compositor context, through a real descriptor: the read,
//          the NUL check, and xkbcommon compiling the text. Bytes with no NUL are tried both as they are, which
//          must be refused, and with one added, because that is what reaches the compiler.
#include <sys/mman.h>  // for memfd_create

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

#include "c_resource.hpp"
#include "file_io.hpp"
#include "untrusted.hpp"

using namespace jgui;
using namespace jgui::detail;

namespace {

bool load(xkb_context* context, std::span<const std::uint8_t> bytes) {
    UniqueFd fd{::memfd_create("jcppgui-fuzz-keymap", MFD_CLOEXEC)};
    if (fd.get() < 0 || !write_at<std::uint8_t>(fd.get(), bytes, 0)) std::abort();
    return load_keymap(context, keymap_format_xkb_v1, std::move(fd), saturate_to<std::uint32_t>(bytes.size())).has_value();
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    // The input leaves one byte of the limit for an added NUL.
    auto bytes = copy_from_c(data, size, std::size_t{max_keymap_bytes} - 1);
    if (!bytes || bytes->empty()) return 0;
    const XkbContext context = make_keymap_context();
    if (!context) std::abort();
    xkb_context_set_log_level(context.get(), XKB_LOG_LEVEL_CRITICAL);

    const bool has_nul = std::ranges::find(*bytes, std::uint8_t{0}) != bytes->end();
    const bool loaded = load(context.get(), *bytes);
    if (!has_nul) {
        if (loaded) std::abort();  // text with no terminator must never reach the compiler
        bytes->push_back(0);
        static_cast<void>(load(context.get(), *bytes));
    }
    return 0;
}
