// File: tests/fuzz/fuzz_utf8.cpp
// Purpose: Fuzzes valid_utf8, the real function. Bytes UTF-8 never contains must never be accepted.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "c_resource.hpp"
#include "jCppGUI/v001/text.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    constexpr std::size_t limit = std::size_t{1} << 16U;
    const auto bytes = jgui::detail::copy_from_c(data, size, limit);
    if (!bytes) return 0;
    std::string text;
    text.reserve(bytes->size());
    for (const std::uint8_t byte : *bytes) text.push_back(static_cast<char>(byte));
    if (jgui::valid_utf8(text)) {
        for (const std::uint8_t byte : *bytes) {
            if (byte == 0xC0 || byte == 0xC1 || byte >= 0xF5) std::abort();
        }
    }
    return 0;
}
